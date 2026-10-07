using ApexSenseBridge.Common;
using ApexSenseBridgeTray.Common;
using System;
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class ControllerDetectionService : IDisposable
    {
        private readonly Timer timer;
        internal delegate string CandidateReader(out string candidate);
        private readonly CandidateReader candidateReader;
        private readonly Func<string> modelReader;
        private readonly Func<BridgeSession.SessionInfo> activeSessionReader;
        private readonly Func<bool> ownerProbe;
        private int scanRunning;
        private string lastStatus = string.Empty;
        private string lastCandidate = string.Empty;
        private int disposed;

        public event Action<string> StatusChanged;

        public ControllerDetectionService() : this(ReadCandidate, DetectModel,
            () => BridgeSession.ReadActiveSession(), EngineSessionManager.IsExternalSessionActive, true) { }

        internal ControllerDetectionService(CandidateReader candidateReader, Func<string> modelReader,
            Func<BridgeSession.SessionInfo> activeSessionReader, Func<bool> ownerProbe, bool startTimer)
        {
            this.candidateReader = candidateReader;
            this.modelReader = modelReader;
            this.activeSessionReader = activeSessionReader;
            this.ownerProbe = ownerProbe;
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
                    lastCandidate = string.Empty;
                    Publish(stateStatus);
                }
                else
                {
                    // During a bridge, use its live IPC identity, not extra HID
                    // readers that can steal replies/input from the active engine.
                    var active = activeSessionReader();
                    if (active != null || ownerProbe())
                    {
                        Publish(ParseActiveModel(active));
                        lastCandidate = string.Empty; // Force verification after stop.
                        return;
                    }
                    // Lock editing immediately when a different HID interface
                    // appears, before the slower identity query completes.
                    if (candidate != lastCandidate) Publish("unavailable");
                    // Recheck even with an unchanged dongle path: a receiver can
                    // remain enumerated while the actual controller is asleep.
                    var model = modelReader();
                    string after;
                    if (candidateReader(out after) != null || candidate != after)
                    {
                        lastCandidate = string.Empty;
                        Publish("unavailable");
                    }
                    else
                    {
                        lastCandidate = candidate;
                        Publish(model);
                    }
                }
            }
            finally { Volatile.Write(ref scanRunning, 0); }
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
            if (!RunEngine("list", 1500, out output, out exitCode)) return "unavailable";
            if (exitCode == 2) return "disconnected";
            if (exitCode != 0) return "unavailable";
            return ParseCandidate(output, out candidate);
        }

        internal static string ParseCandidate(string output, out string candidate)
        {
            candidate = string.Empty;
            // More than one interface is ambiguous: never calibrate the first
            // enumerated controller by accident. Include path in the fingerprint.
            if (!Regex.IsMatch(output ?? "", @"\AFound 1 candidate\(s\):")) return "unavailable";
            candidate = output.Trim();
            return null;
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
