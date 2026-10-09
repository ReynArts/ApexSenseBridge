using ApexSenseBridge.Common;
using ApexSenseBridgeTray.Common;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class ControllerDetectionService : IDisposable
    {
        internal const string Ambiguous = "ambiguous";
        internal const int ReverifyMilliseconds = 30000;
        internal const int FailureScans = 3;
        internal const int DisconnectScans = 2;
        private static readonly Stopwatch uptime = Stopwatch.StartNew();
        private readonly Timer timer;
        internal delegate string CandidateReader(out string candidate);
        private readonly CandidateReader candidateReader;
        private readonly Func<string> modelReader;
        private readonly Func<BridgeSession.SessionInfo> activeSessionReader;
        private readonly Func<bool> ownerProbe;
        private readonly Func<long> clock;
        private int scanRunning;
        private string lastStatus = string.Empty;
        private string verifiedCandidate = string.Empty;
        private string verifiedModel;
        private long verifiedAt;
        private int failures;
        private int disposed;

        public event Action<string> StatusChanged;

        public ControllerDetectionService() : this(ReadCandidate, DetectModel,
            () => BridgeSession.ReadActiveSession(), EngineSessionManager.IsExternalSessionActive, true) { }

        internal ControllerDetectionService(CandidateReader candidateReader, Func<string> modelReader,
            Func<BridgeSession.SessionInfo> activeSessionReader, Func<bool> ownerProbe, bool startTimer)
            : this(candidateReader, modelReader, activeSessionReader, ownerProbe,
                () => uptime.ElapsedMilliseconds, startTimer) { }

        internal ControllerDetectionService(CandidateReader candidateReader, Func<string> modelReader,
            Func<BridgeSession.SessionInfo> activeSessionReader, Func<bool> ownerProbe,
            Func<long> clock, bool startTimer)
        {
            this.candidateReader = candidateReader;
            this.modelReader = modelReader;
            this.activeSessionReader = activeSessionReader;
            this.ownerProbe = ownerProbe;
            this.clock = clock;
            timer = new Timer(Scan, null, startTimer ? 250 : Timeout.Infinite, 2000);
        }

        internal void Scan(object state)
        {
            if (Volatile.Read(ref disposed) != 0) return;
            if (Interlocked.Exchange(ref scanRunning, 1) != 0) return;
            try
            {
                string candidate;
                string stateStatus = candidateReader(out candidate);
                if (stateStatus != null)
                {
                    // A transient extra interface is tolerated only while the verified one is still listed.
                    if (stateStatus == Ambiguous && !ContainsCandidate(candidate, verifiedCandidate)) Lock("unavailable");
                    else Downgrade(stateStatus == "disconnected" ? stateStatus : "unavailable");
                    return;
                }
                // During a bridge, use its live IPC identity, not extra HID
                // readers that can steal replies/input from the active engine.
                var active = activeSessionReader();
                if (active != null || ownerProbe())
                {
                    verifiedCandidate = string.Empty; // Force verification after stop.
                    failures = 0;
                    Publish(ParseActiveModel(active));
                    return;
                }
                // Lock editing immediately when a different HID interface
                // appears, before the slower identity query completes.
                if (candidate != verifiedCandidate) Lock("unavailable");
                else if (failures == 0 && clock() - verifiedAt < ReverifyMilliseconds)
                {
                    Publish(verifiedModel);
                    return;
                }
                // Periodic recheck: a receiver can remain enumerated while the controller is asleep.
                var model = modelReader();
                string after;
                string afterStatus = candidateReader(out after);
                if (afterStatus == null && after != candidate) Lock("unavailable");
                else if (afterStatus != null || model == "unavailable")
                    Downgrade(afterStatus == "disconnected" ? afterStatus : "unavailable");
                else
                {
                    verifiedCandidate = candidate;
                    verifiedModel = model;
                    verifiedAt = clock();
                    failures = 0;
                    Publish(model);
                }
            }
            finally { Volatile.Write(ref scanRunning, 0); }
        }

        // One missed list/identify reply must not flap a verified controller.
        private void Downgrade(string status)
        {
            int limit = status == "disconnected" ? DisconnectScans : FailureScans;
            if (verifiedCandidate.Length != 0 && ++failures < limit) return;
            Lock(status);
        }

        private void Lock(string status)
        {
            verifiedCandidate = string.Empty;
            verifiedModel = null;
            failures = 0;
            Publish(status);
        }

        private static bool ContainsCandidate(string candidates, string candidate)
        {
            return candidate.Length != 0 &&
                Array.IndexOf((candidates ?? "").Split(new[] { "\n\n" }, StringSplitOptions.None), candidate) >= 0;
        }

        internal static string ParseActiveModel(BridgeSession.SessionInfo active)
        {
            return active != null && active.Phase == SessionPhase.Ready
                ? ParseVerifiedModel("Verified: " + active.Controller) : "unavailable";
        }

        private static string ReadCandidate(out string candidate)
        {
            candidate = string.Empty;
            string output;
            int exitCode;
            if (!RunEngine("list", 3000, out output, out exitCode)) return "unavailable";
            if (exitCode == 2) return "disconnected";
            if (exitCode != 0) return "unavailable";
            return ParseCandidate(output, out candidate);
        }

        internal static string ParseCandidate(string output, out string candidate)
        {
            candidate = string.Empty;
            var header = Regex.Match(output ?? "", @"\AFound (\d+) candidate\(s\):");
            if (!header.Success) return "unavailable";
            // Fingerprint only stable enumeration fields; the HID product string can fail transiently.
            var fingerprints = new List<string>();
            var blocks = Regex.Split(output.Substring(header.Length), @"^\s*\[\d+\][^\n]*$", RegexOptions.Multiline);
            for (int i = 1; i < blocks.Length; i++)
            {
                var lines = new List<string>();
                foreach (Match line in Regex.Matches(blocks[i],
                    @"^[ \t]*(?:VID:PID|Usage page|Reports|Path)\b[^\r\n]*", RegexOptions.Multiline))
                    lines.Add(Regex.Replace(line.Value.Trim(), @"\s+", " "));
                if (lines.Count == 0) return "unavailable";
                fingerprints.Add(string.Join("\n", lines));
            }
            if (fingerprints.Count == 0 || fingerprints.Count.ToString() != header.Groups[1].Value) return "unavailable";
            // More than one interface is ambiguous: never calibrate the first enumerated controller by accident.
            candidate = string.Join("\n\n", fingerprints);
            return fingerprints.Count == 1 ? null : Ambiguous;
        }

        private static string DetectModel()
        {
            string output;
            int exitCode;
            if (!RunEngine("identify", 4500, out output, out exitCode)) return "unavailable";
            if (exitCode != 0) return "unavailable";
            return ParseVerifiedModel(output);
        }

        internal static string ParseVerifiedModel(string output)
        {
            output = output ?? string.Empty;
            if (Regex.IsMatch(output, @"Verified:\s+Apex 4\b", RegexOptions.IgnoreCase)) return "apex4";
            if (Regex.IsMatch(output, @"Verified:\s+Apex 5\b", RegexOptions.IgnoreCase)) return "apex5";
            if (Regex.IsMatch(output, @"Verified:\s+Apex 6 Pro\b", RegexOptions.IgnoreCase)) return "apex6";
            return "unsupported";
        }

        private static bool RunEngine(string arguments, int timeoutMilliseconds,
                                      out string output, out int exitCode)
        {
            output = string.Empty;
            exitCode = -1;
            var engine = InstallLocator.ResolveEngine();
            if (string.IsNullOrWhiteSpace(engine) || !File.Exists(engine)) return false;
            try
            {
                var start = new ProcessStartInfo(engine, arguments)
                {
                    CreateNoWindow = true,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    WorkingDirectory = Path.GetDirectoryName(engine)
                };
                using (var process = Process.Start(start))
                {
                    if (process == null) return false;
                    // Drain both pipes asynchronously so the timeout also bounds
                    // hung identification; a blocking ReadToEnd defeated it.
                    var stdout = process.StandardOutput.ReadToEndAsync();
                    var stderr = process.StandardError.ReadToEndAsync();
                    if (!process.WaitForExit(timeoutMilliseconds))
                    {
                        try { process.Kill(); } catch { }
                        return false;
                    }
                    if (!System.Threading.Tasks.Task.WaitAll(new System.Threading.Tasks.Task[] { stdout, stderr }, 500))
                        return false;
                    output = stdout.Result;
                    exitCode = process.ExitCode;
                    return true;
                }
            }
            catch { return false; }
        }

        private void Publish(string status)
        {
            if (Volatile.Read(ref disposed) != 0) return;
            if (string.Equals(lastStatus, status, StringComparison.Ordinal)) return;
            lastStatus = status;
            var handler = StatusChanged;
            if (handler != null) handler(status);
        }

        public void Dispose() { Interlocked.Exchange(ref disposed, 1); timer.Dispose(); }
    }
}
