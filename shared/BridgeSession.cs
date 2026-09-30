using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

namespace ApexSenseBridge.Common
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
        private readonly MemoryMappedFile progressMapping;
        private readonly MemoryMappedViewAccessor progressView;
        private readonly Process process;
        private bool disposed;
        private const string DiscoveryName = "Local\\ApexSenseBridge.ActiveSession.Info.v1";
        private string discoveryName = DiscoveryName;
        private MemoryMappedFile discoveryMapping;

        internal sealed class SessionInfo
        {
            public string Token { get; set; }
            public int EngineProcessId { get; set; }
            public long EngineStartedUtcTicks { get; set; }
            public string Owner { get; set; }
            public string Game { get; set; }
            public string Profile { get; set; }
            public string Controller { get; set; }
            public SessionPhase Phase { get; set; }
            public int ExitCode { get; set; }
            public string Message { get; set; }
            public uint Stages { get; set; }
            public uint Interruption { get; set; }
        }

        public SessionInfo ReadStatus()
        {
            var info = ReadStatus(statusView);
            ReadProgress(progressView, info);
            return info;
        }

        private static void ReadProgress(MemoryMappedViewAccessor view, SessionInfo info)
        {
            if (view != null && view.ReadUInt32(0) == 0x50534241 && view.ReadUInt32(4) == 1)
            {
                info.Stages = view.ReadUInt32(8);
                info.Interruption = view.ReadUInt32(12);
            }
        }

        public static SessionInfo ReadSessionStatus(string token)
        {
            if (token == null || token.Length != 32 || token.Trim("0123456789abcdefABCDEF".ToCharArray()).Length != 0) return null;
            try
            {
                using (var mapping = MemoryMappedFile.OpenExisting("Local\\ApexSenseBridge.Session." + token + ".Status", MemoryMappedFileRights.Read))
                using (var reader = mapping.CreateViewAccessor(0, StatusSize, MemoryMappedFileAccess.Read))
                {
                    var info = ReadStatus(reader);
                    try
                    {
                        using (var progress = MemoryMappedFile.OpenExisting("Local\\ApexSenseBridge.Session." + token + ".Progress", MemoryMappedFileRights.Read))
                        using (var view = progress.CreateViewAccessor(0, 16, MemoryMappedFileAccess.Read)) ReadProgress(view, info);
                    }
                    catch (FileNotFoundException) { }
                    return info;
                }
            }
            catch { return null; }
        }

        private static SessionInfo ReadStatus(MemoryMappedViewAccessor view)
        {
            if (view.ReadUInt32(0) != StatusMagic || view.ReadUInt16(4) != ProtocolVersion)
                return new SessionInfo { Phase = SessionPhase.Empty };
            var length = Math.Min(view.ReadUInt32(12), 495u);
            var bytes = new byte[(int)length];
            view.ReadArray(16, bytes, 0, bytes.Length);
            return new SessionInfo { Phase = (SessionPhase)view.ReadUInt16(6), ExitCode = view.ReadInt32(8), Message = Encoding.UTF8.GetString(bytes) };
        }

        public static SessionInfo ReadActiveSession(string discoveryName = DiscoveryName)
        {
            try
            {
                using (var mapping = MemoryMappedFile.OpenExisting(discoveryName, MemoryMappedFileRights.Read))
                using (var view = mapping.CreateViewAccessor(0, 4096, MemoryMappedFileAccess.Read))
                {
                    var length = view.ReadInt32(0);
                    if (length <= 0 || length > 4092) return null;
                    var bytes = new byte[length];
                    view.ReadArray(4, bytes, 0, length);
                    var info = new JavaScriptSerializer().Deserialize<SessionInfo>(Encoding.UTF8.GetString(bytes));
                    if (info == null || info.Token == null || info.Token.Length != 32) return null;
                    using (var engine = Process.GetProcessById(info.EngineProcessId))
                        if (engine.HasExited || engine.StartTime.ToUniversalTime().Ticks != info.EngineStartedUtcTicks) return null;
                    using (var status = MemoryMappedFile.OpenExisting("Local\\ApexSenseBridge.Session." + info.Token + ".Status", MemoryMappedFileRights.Read))
                    using (var statusReader = status.CreateViewAccessor(0, StatusSize, MemoryMappedFileAccess.Read))
                    {
                        var live = ReadStatus(statusReader);
                        info.Phase = live.Phase;
                        info.Message = live.Message;
                        info.ExitCode = live.ExitCode;
                    }
                    try
                    {
                        using (var progress = MemoryMappedFile.OpenExisting("Local\\ApexSenseBridge.Session." + info.Token + ".Progress", MemoryMappedFileRights.Read))
                        using (var reader = progress.CreateViewAccessor(0, 16, MemoryMappedFileAccess.Read)) ReadProgress(reader, info);
                    }
                    catch (FileNotFoundException) { } // Earlier launchers have no progress mapping.
                    return info;
                }
            }
            catch { return null; }
        }

        private void PublishDiscovery(string token, string game, string profile)
        {
            try
            {
                var state = ReadStatus();
                var controller = state.Message != null && state.Message.StartsWith("ASB_READY|") ? state.Message.Substring(10) : string.Empty;
                string processName;
                using (var owner = Process.GetCurrentProcess()) processName = owner.ProcessName;
                var info = new SessionInfo { Token = token, EngineProcessId = process.Id,
                    EngineStartedUtcTicks = process.StartTime.ToUniversalTime().Ticks,
                    Owner = processName.IndexOf("Playnite", StringComparison.OrdinalIgnoreCase) >= 0 ? "Playnite" :
                        processName.Equals("ApexSenseBridgeTray", StringComparison.OrdinalIgnoreCase) ? "Tray" : processName,
                    Game = game ?? string.Empty, Profile = profile ?? string.Empty, Controller = controller };
                var bytes = Encoding.UTF8.GetBytes(new JavaScriptSerializer().Serialize(info));
                if (bytes.Length > 4092) return;
                if (discoveryMapping == null) discoveryMapping = MemoryMappedFile.CreateOrOpen(discoveryName, 4096, MemoryMappedFileAccess.ReadWrite);
                using (var writer = discoveryMapping.CreateViewAccessor())
                {
                    writer.Write(0, 0);
                    writer.WriteArray(4, bytes, 0, bytes.Length);
                    writer.Write(0, bytes.Length);
                }
            }
            catch (Exception ex) { logError("Session discovery unavailable: " + ex.Message); }
        }

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
            MemoryMappedFile progressMapping,
            MemoryMappedViewAccessor progressView,
            Process process)
        {
            this.logInfo = logInfo ?? (delegate(string s) { });
            this.logError = logError ?? (delegate(string s) { });
            this.readyEvent = readyEvent;
            this.stopEvent = stopEvent;
            this.statusMapping = statusMapping;
            this.statusView = statusView;
            this.progressMapping = progressMapping;
            this.progressView = progressView;
            this.process = process;
        }

        public static BridgeSession TryStart(
            string executablePath,
            string bridgeArguments,
            TimeSpan timeout,
            Action<string> logInfo,
            Action<string> logError,
            out string error,
            string gameTitle = null,
            string profile = null,
            string discoveryName = DiscoveryName,
            Action<SessionInfo> progressChanged = null)
        {
            error = null;
            EventWaitHandle ready = null;
            EventWaitHandle stop = null;
            MemoryMappedFile mapping = null;
            MemoryMappedViewAccessor view = null;
            MemoryMappedFile progressMapping = null;
            MemoryMappedViewAccessor progressView = null;
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
                progressMapping = MemoryMappedFile.CreateNew(prefix + ".Progress", 16, MemoryMappedFileAccess.ReadWrite);
                progressView = progressMapping.CreateViewAccessor(0, 16, MemoryMappedFileAccess.ReadWrite);

                var startInfo = new ProcessStartInfo
                {
                    FileName = executablePath,
                    Arguments = bridgeArguments + " --session-token " + token +
                                " --session-owner-pid " +
                                Process.GetCurrentProcess().Id.ToString(),
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

                var session = new BridgeSession(logInfo, logError, ready, stop, mapping, view, progressMapping, progressView, process);
                session.discoveryName = discoveryName;
                ready = null;
                stop = null;
                mapping = null;
                view = null;
                progressMapping = null;
                progressView = null;
                process = null;

                if (!session.WaitUntilReady(timeout, token, gameTitle, profile, progressChanged, out error))
                {
                    session.StopAndWait(TimeSpan.FromSeconds(15));
                    session.Dispose();
                    return null;
                }
                session.PublishDiscovery(token, gameTitle, profile);
                return session;
            }
            catch (Exception exception)
            {
                error = "Impossible de démarrer ApexSenseBridge : " + exception.Message;
                if (logError != null) logError(error);
                if (process != null) process.Dispose();
                if (view != null) view.Dispose();
                if (progressView != null) progressView.Dispose();
                if (progressMapping != null) progressMapping.Dispose();
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
                if (discoveryMapping != null) discoveryMapping.Dispose();
                if (process != null) process.Dispose();
                if (statusView != null) statusView.Dispose();
                if (progressView != null) progressView.Dispose();
                if (progressMapping != null) progressMapping.Dispose();
                if (statusMapping != null) statusMapping.Dispose();
                if (stopEvent != null) stopEvent.Dispose();
                if (readyEvent != null) readyEvent.Dispose();
            }
            catch
            {
            }
        }

        private bool WaitUntilReady(TimeSpan timeout, string token, string game, string profile, Action<SessionInfo> progressChanged, out string error)
        {
            var deadline = DateTime.UtcNow + timeout;
            while (DateTime.UtcNow < deadline)
            {
                if (progressChanged != null) progressChanged(ReadStatus());
                if (discoveryMapping == null && ReadStatus().Phase == SessionPhase.Starting)
                    PublishDiscovery(token, game, profile);
                if (readyEvent.WaitOne(100))
                {
                    if (progressChanged != null) progressChanged(ReadStatus());
                    return ReadReadyStatus(out error);
                }
                if (process.HasExited)
                {
                    var finalStatus = ReadStatus();
                    if (finalStatus.Phase == SessionPhase.Failed && !string.IsNullOrWhiteSpace(finalStatus.Message))
                    {
                        error = finalStatus.Message;
                        return false;
                    }
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
