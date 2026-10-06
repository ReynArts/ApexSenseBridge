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
        private int scanRunning;
        private string lastStatus = string.Empty;
        private string lastCandidate = string.Empty;
        private int disposed;

        public event Action<string> StatusChanged;

        public ControllerDetectionService()
        {
            timer = new Timer(Scan, null, 250, 2000);
        }

        private void Scan(object state)
        {
            if (Interlocked.Exchange(ref scanRunning, 1) != 0) return;
            try
            {
                string candidate;
                string stateStatus = ReadCandidate(out candidate);
                if (stateStatus != null)
                {
                    lastCandidate = string.Empty;
                    Publish(stateStatus);
                }
                else if (candidate != lastCandidate || !Models.TraySettings.IsCalibrationModel(lastStatus))
                {
                    // Lock editing immediately when a different HID interface
                    // appears, before the slower identity query completes.
                    Publish("unavailable");
                    var model = DetectModel();
                    string after;
                    if (ReadCandidate(out after) != null || candidate != after)
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
