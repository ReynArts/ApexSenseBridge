using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Threading;

namespace ApexSenseBridgeTray.Common
{
    internal enum SessionPhase : ushort
    {
        Empty = 0,
        Starting = 1,
        Ready = 2,
        Stopping = 3,
        Stopped = 4,
        Failed = 5
    }

    internal sealed class BridgeSession : IDisposable
    {
        private const uint StatusMagic = 0x53425341;
        private const ushort ProtocolVersion = 1;
        private const int StatusSize = 512;

        private readonly Action<string> logInfo;
        private readonly Action<string> logError;
        private readonly EventWaitHandle readyEvent;
        private readonly EventWaitHandle stopEvent;
        private readonly MemoryMappedFile statusMapping;
        private readonly MemoryMappedViewAccessor statusView;
        private readonly Process process;
        private bool disposed;

        public int ProcessId
        {
            get
            {
                return (process != null && !process.HasExited) ? process.Id : 0;
            }
        }

        private BridgeSession(
            Action<string> logInfo,
            Action<string> logError,
            EventWaitHandle readyEvent,
            EventWaitHandle stopEvent,
            MemoryMappedFile statusMapping,
            MemoryMappedViewAccessor statusView,
            Process process)
        {
            this.logInfo = logInfo ?? (delegate(string s) { });
            this.logError = logError ?? (delegate(string s) { });
            this.readyEvent = readyEvent;
            this.stopEvent = stopEvent;
            this.statusMapping = statusMapping;
            this.statusView = statusView;
            this.process = process;
        }

        public static BridgeSession TryStart(
            string executablePath,
            string bridgeArguments,
            TimeSpan timeout,
            Action<string> logInfo,
            Action<string> logError,
            out string error)
        {
            error = null;
            EventWaitHandle ready = null;
            EventWaitHandle stop = null;
            MemoryMappedFile mapping = null;
            MemoryMappedViewAccessor view = null;
            Process process = null;
            try
            {
                var token = Guid.NewGuid().ToString("N");
                var prefix = "Local\\ApexSenseBridge.Session." + token;
                bool readyCreated;
                bool stopCreated;
                ready = new EventWaitHandle(false, EventResetMode.ManualReset,
                                            prefix + ".Ready", out readyCreated);
                stop = new EventWaitHandle(false, EventResetMode.ManualReset,
                                           prefix + ".Stop", out stopCreated);
                if (!readyCreated || !stopCreated)
                {
                    throw new InvalidOperationException("IPC event name collision.");
                }
                mapping = MemoryMappedFile.CreateNew(prefix + ".Status", StatusSize,
                                                     MemoryMappedFileAccess.ReadWrite);
                view = mapping.CreateViewAccessor(0, StatusSize, MemoryMappedFileAccess.ReadWrite);

                var startInfo = new ProcessStartInfo
                {
                    FileName = executablePath,
                    Arguments = bridgeArguments + " --session-token " + token,
                    WorkingDirectory = Path.GetDirectoryName(executablePath),
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                };
                process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
                process.OutputDataReceived += (sender, args) =>
                {
                    if (!string.IsNullOrWhiteSpace(args.Data))
                    {
                        if (logInfo != null) logInfo("[bridge] " + args.Data);
                    }
                };
                process.ErrorDataReceived += (sender, args) =>
                {
                    if (!string.IsNullOrWhiteSpace(args.Data))
                    {
                        if (logError != null) logError("[bridge] " + args.Data);
                    }
                };
                if (!process.Start())
                {
                    throw new InvalidOperationException("ApexSenseBridge process did not start.");
                }
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();

                var session = new BridgeSession(logInfo, logError, ready, stop, mapping, view, process);
                ready = null;
                stop = null;
                mapping = null;
                view = null;
                process = null;

                if (!session.WaitUntilReady(timeout, out error))
                {
                    session.StopAndWait(TimeSpan.FromSeconds(15));
                    session.Dispose();
                    return null;
                }
                return session;
            }
            catch (Exception exception)
            {
                error = "Impossible de démarrer ApexSenseBridge : " + exception.Message;
                if (logError != null) logError(error);
                if (process != null) process.Dispose();
                if (view != null) view.Dispose();
                if (mapping != null) mapping.Dispose();
                if (stop != null) stop.Dispose();
                if (ready != null) ready.Dispose();
                return null;
            }
        }

        public bool StopAndWait(TimeSpan timeout)
        {
            if (disposed)
            {
                return true;
            }
            try
            {
                stopEvent.Set();
                if (process.HasExited)
                {
                    return true;
                }
                return process.WaitForExit((int)timeout.TotalMilliseconds);
            }
            catch (Exception exception)
            {
                if (logError != null) logError("Failed to stop ApexSenseBridge session: " + exception.Message);
                return false;
            }
        }

        public void Dispose()
        {
            if (disposed)
            {
                return;
            }
            disposed = true;
            try
            {
                if (process != null) process.Dispose();
                if (statusView != null) statusView.Dispose();
                if (statusMapping != null) statusMapping.Dispose();
                if (stopEvent != null) stopEvent.Dispose();
                if (readyEvent != null) readyEvent.Dispose();
            }
            catch
            {
            }
        }

        private bool WaitUntilReady(TimeSpan timeout, out string error)
        {
            var deadline = DateTime.UtcNow + timeout;
            while (DateTime.UtcNow < deadline)
            {
                if (readyEvent.WaitOne(100))
                {
                    return ReadReadyStatus(out error);
                }
                if (process.HasExited)
                {
                    error = "ApexSenseBridge s'est arrêté avant de signaler qu'il était prêt " +
                            "(code " + process.ExitCode + ").";
                    return false;
                }
            }
            error = "ApexSenseBridge n'a pas terminé son initialisation dans le délai de " +
                    ((int)timeout.TotalSeconds) + " secondes.";
            return false;
        }

        private bool ReadReadyStatus(out string error)
        {
            var magic = statusView.ReadUInt32(0);
            var version = statusView.ReadUInt16(4);
            var phase = (SessionPhase)statusView.ReadUInt16(6);
            var exitCode = statusView.ReadInt32(8);
            var messageLength = Math.Min(statusView.ReadUInt32(12), 495u);
            var messageBytes = new byte[(int)messageLength];
            statusView.ReadArray(16, messageBytes, 0, messageBytes.Length);
            var message = Encoding.UTF8.GetString(messageBytes);

            if (magic != StatusMagic || version != ProtocolVersion)
            {
                error = "Le bridge a renvoyé un statut IPC incompatible.";
                return false;
            }
            if (phase == SessionPhase.Ready)
            {
                error = null;
                return true;
            }
            error = string.IsNullOrWhiteSpace(message)
                ? "Initialisation du bridge échouée (code " + exitCode + ")."
                : message;
            return false;
        }
    }
}
