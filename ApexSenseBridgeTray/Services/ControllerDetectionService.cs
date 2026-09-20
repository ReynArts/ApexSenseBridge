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
                if (!HasCandidate())
                {
                    Publish("disconnected");
                }
                else if (lastStatus == string.Empty || lastStatus == "disconnected" ||
                         lastStatus == "unavailable")
                {
                    Publish(DetectModel());
                }
            }
            finally { Volatile.Write(ref scanRunning, 0); }
        }

        private static bool HasCandidate()
        {
            string output;
            int exitCode;
            return RunEngine("list", 1500, out output, out exitCode) && exitCode == 0;
        }

        private static string DetectModel()
        {
            string output;
            int exitCode;
            if (!RunEngine("identify", 4500, out output, out exitCode)) return "unavailable";
            if (exitCode != 0) return "disconnected";
            if (Regex.IsMatch(output, @"Verified:\s+Apex 4\b", RegexOptions.IgnoreCase)) return "apex4";
            if (Regex.IsMatch(output, @"Verified:\s+Apex 5\b", RegexOptions.IgnoreCase)) return "apex5";
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
                    output = process.StandardOutput.ReadToEnd();
                    process.StandardError.ReadToEnd();
                    if (!process.WaitForExit(timeoutMilliseconds))
                    {
                        try { process.Kill(); } catch { }
                        return false;
                    }
                    exitCode = process.ExitCode;
                    return true;
                }
            }
            catch { return false; }
        }

        private void Publish(string status)
        {
            if (string.Equals(lastStatus, status, StringComparison.Ordinal)) return;
            lastStatus = status;
            var handler = StatusChanged;
            if (handler != null) handler(status);
        }

        public void Dispose() { timer.Dispose(); }
    }
}
