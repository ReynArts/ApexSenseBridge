using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Management;
using System.Threading;

namespace ApexSenseBridgeTray.Services
{
    public class ProcessMonitorService : IDisposable
    {
        private static readonly TimeSpan ProcessExitGracePeriod = TimeSpan.FromSeconds(2);

        private readonly CloudGameListService gameListService;
        private readonly EngineSessionManager sessionManager;
        private readonly ExecutableLearningService learningService;
        private readonly TraySettings settings;
        private readonly Timer pollTimer;
        private readonly Dictionary<uint, DateTime> retryCooldowns = new Dictionary<uint, DateTime>();
        private readonly Dictionary<uint, long> evaluatedProcesses = new Dictionary<uint, long>();
        private readonly object sessionStateLock = new object();
        private readonly GameProcessSessionTracker processSession = new GameProcessSessionTracker();

        private ManagementEventWatcher startWatcher;
        private ManagementEventWatcher stopWatcher;

        private bool isDisposed;
        private bool isStoppingDetectedSession;
        private int isPolling;
        private DateTime nextProcessSweepUtc;
        private DateTime nextForegroundCheckUtc;

        public event Action<SupportedGame, string> GameDetected;
        public event Action<string> GameExited;

        public ProcessMonitorService(
            CloudGameListService gameListService,
            EngineSessionManager sessionManager,
            ExecutableLearningService learningService,
            TraySettings settings)
        {
            this.gameListService = gameListService;
            this.sessionManager = sessionManager;
            this.learningService = learningService;
            this.settings = settings;

            InitializeWmiWatchers();

            pollTimer = new Timer(OnPollTick, null, 100, 250);
        }

        private void InitializeWmiWatchers()
        {
            try
            {
                startWatcher = new ManagementEventWatcher(new WqlEventQuery("SELECT * FROM Win32_ProcessStartTrace"));
                startWatcher.EventArrived += OnProcessStartedWmi;
                startWatcher.Start();
            }
            catch (Exception ex)
            {
                LogEvent("WMI StartWatcher unavailable: " + ex.Message);
                if (startWatcher != null)
                {
                    try { startWatcher.Dispose(); } catch { }
                    startWatcher = null;
                }
            }

            try
            {
                stopWatcher = new ManagementEventWatcher(new WqlEventQuery("SELECT * FROM Win32_ProcessStopTrace"));
                stopWatcher.EventArrived += OnProcessStoppedWmi;
                stopWatcher.Start();
            }
            catch (Exception ex)
            {
                LogEvent("WMI StopWatcher unavailable: " + ex.Message);
                if (stopWatcher != null)
                {
                    try { stopWatcher.Dispose(); } catch { }
                    stopWatcher = null;
                }
            }
        }

        private void OnProcessStartedWmi(object sender, EventArrivedEventArgs e)
        {
            if (isDisposed) return;
            long detectionStartedAt = Stopwatch.GetTimestamp();
            DateTime? processEventUtc = null;

            try
            {
                processEventUtc = ReadWmiEventCreatedUtc(e);
                var processName = e.NewEvent.Properties["ProcessName"].Value as string;
                var pidObj = e.NewEvent.Properties["ProcessID"].Value;
                if (string.IsNullOrWhiteSpace(processName) || pidObj == null) return;

                uint pid = Convert.ToUInt32(pidObj);
                if (pid == 0 || IsTrackedProcess(pid)) return;
                if (IsSystemOrIgnoredProcess(processName)) return;

                if (settings == null || !settings.AutoDetectGames) return;
                if (settings.ForcedProfile != null && settings.ForcedProfile != "none") return;

                CheckCandidateProcess(
                    pid, processName, null, detectionStartedAt, processEventUtc, "WMI");
            }
            catch (Exception ex)
            {
                LogEvent("[OnProcessStartedWmi] " + ex.Message);
            }
        }

        private void OnProcessStoppedWmi(object sender, EventArrivedEventArgs e)
        {
            if (isDisposed) return;

            try
            {
                var pidObj = e.NewEvent.Properties["ProcessID"].Value;
                if (pidObj == null) return;

                uint pid = Convert.ToUInt32(pidObj);
                if (pid != 0)
                {
                    HandleProcessStopped(pid, "Process stopped (WMI)");
                }
            }
            catch (Exception ex)
            {
                LogEvent("[OnProcessStoppedWmi] " + ex.Message);
            }
        }

        public void ForceCheck()
        {
            ThreadPool.QueueUserWorkItem(_ => OnPollTick(null));
        }

        private void OnPollTick(object state)
        {
            if (isDisposed) return;

            if (Interlocked.CompareExchange(ref isPolling, 1, 0) != 0)
            {
                return;
            }

            try
            {
                PruneExitedTrackedProcesses();

                if (settings == null || !settings.AutoDetectGames ||
                    (settings.ForcedProfile != null && settings.ForcedProfile != "none"))
                {
                    ResetTrackedSessionOnly();
                    return;
                }

                if (HasTrackedSession() && !sessionManager.IsSessionActive)
                {
                    ResetTrackedSessionOnly();
                }

                if ((!HasTrackedSession() || IsAwaitingReplacement()) &&
                    DateTime.UtcNow >= nextProcessSweepUtc)
                {
                    nextProcessSweepUtc = DateTime.UtcNow.AddMilliseconds(250);
                    ScanRunningProcesses();
                }

                if (TryStopExpiredSession())
                {
                    return;
                }

                if (DateTime.UtcNow < nextForegroundCheckUtc) return;
                nextForegroundCheckUtc = DateTime.UtcNow.AddSeconds(1);

                var hwnd = NativeMethods.GetForegroundWindow();
                if (hwnd == IntPtr.Zero) return;

                uint pid;
                var exePath = NativeMethods.GetActiveProcessPath(hwnd, out pid);
                if (string.IsNullOrWhiteSpace(exePath) || pid == 0) return;
                if (IsTrackedProcess(pid)) return;

                var fileName = Path.GetFileName(exePath);
                if (IsSystemOrIgnoredProcess(fileName)) return;

                CheckCandidateProcess(pid, fileName, exePath);
            }
            catch (Exception ex)
            {
                LogEvent("[OnPollTick] " + ex.Message);
            }
            finally
            {
                Interlocked.Exchange(ref isPolling, 0);
            }
        }

        private bool ScanRunningProcesses()
        {
            var processIds = NativeMethods.GetProcessIds();
            if (processIds.Length == 0) return false;

            var runningPids = new HashSet<uint>();
            foreach (var pid in processIds)
            {
                if (pid == 0) continue;
                runningPids.Add(pid);
                if (IsTrackedProcess(pid)) continue;

                lock (evaluatedProcesses)
                {
                    if (evaluatedProcesses.ContainsKey(pid))
                    {
                        continue;
                    }
                    evaluatedProcesses[pid] = 0;
                }

                try
                {
                    using (var process = Process.GetProcessById((int)pid))
                    {
                        string fileName = process.ProcessName + ".exe";
                        if (IsSystemOrIgnoredProcess(fileName)) continue;

                        long startedAt;
                        try
                        {
                            startedAt = process.StartTime.ToUniversalTime().Ticks;
                        }
                        catch
                        {
                            startedAt = 0;
                        }
                        lock (evaluatedProcesses)
                        {
                            evaluatedProcesses[pid] = startedAt;
                        }

                        string exePath = null;
                        try
                        {
                            if (process.MainModule != null)
                            {
                                exePath = process.MainModule.FileName;
                                fileName = Path.GetFileName(exePath);
                            }
                        }
                        catch
                        {
                        }

                        if (IsSystemOrIgnoredProcess(fileName)) continue;

                        CheckCandidateProcess(pid, fileName, exePath);
                        if (IsTrackedProcess(pid)) return true;
                    }
                }
                catch (ArgumentException)
                {
                    lock (evaluatedProcesses)
                    {
                        evaluatedProcesses.Remove(pid);
                    }
                }
                catch (Exception ex)
                {
                    LogEvent("[ScanRunningProcesses] Candidate failed: " + ex.Message);
                }
            }

            lock (evaluatedProcesses)
            {
                var stoppedPids = new List<uint>();
                foreach (var pid in evaluatedProcesses.Keys)
                {
                    if (!runningPids.Contains(pid)) stoppedPids.Add(pid);
                }
                foreach (var pid in stoppedPids)
                {
                    evaluatedProcesses.Remove(pid);
                }
            }
            return false;
        }

        private void CheckCandidateProcess(
            uint pid,
            string fileName,
            string fullPath = null,
            long detectionStartedAt = 0,
            DateTime? processEventUtc = null,
            string detectionSource = "poll")
        {
            if (detectionStartedAt == 0) detectionStartedAt = Stopwatch.GetTimestamp();
            if (IsTrackedProcess(pid) || IsSystemOrIgnoredProcess(fileName)) return;

            lock (retryCooldowns)
            {
                if (retryCooldowns.ContainsKey(pid))
                {
                    if (DateTime.UtcNow < retryCooldowns[pid])
                    {
                        return;
                    }
                    retryCooldowns.Remove(pid);
                }
            }

            string exePath = fullPath;
            if (string.IsNullOrWhiteSpace(exePath))
            {
                try
                {
                    var proc = Process.GetProcessById((int)pid);
                    exePath = proc.MainModule != null ? proc.MainModule.FileName : fileName;
                }
                catch
                {
                    exePath = fileName;
                }
            }

            if (IsSystemOrIgnoredProcess(exePath)) return;

            var exeTitle = Path.GetFileNameWithoutExtension(exePath);
            var folderName = GetParentFolderName(exePath);

            SupportedGame matchedGame;
            string matchedBy;
            if (TryResolveGame(exePath, exeTitle, folderName, fileName, out matchedGame, out matchedBy))
            {
                if (GameActivationPolicy.ShouldActivate(
                    matchedGame, settings, exeTitle, folderName, fileName))
                {
                    HandleGameDetected(
                        matchedGame, pid, exePath, matchedBy, detectionStartedAt,
                        processEventUtc, detectionSource);
                }
            }
        }

        private bool TryResolveGame(
            string exePath,
            string exeTitle,
            string folderName,
            string fileName,
            out SupportedGame game,
            out string matchedBy)
        {
            game = null;
            matchedBy = null;
            if (gameListService == null) return false;

            if (learningService != null &&
                learningService.TryResolve(exePath, gameListService, out game))
            {
                matchedBy = "learned exact path '" + exePath + "'";
                return true;
            }

            if (gameListService.TryFindByExecutable(exePath, out game))
            {
                matchedBy = "database exact executable '" + Path.GetFileName(exePath) + "'";
                return true;
            }

            var candidates = new List<KeyValuePair<string, string>>();
            AddExecutableMetadataCandidates(exePath, candidates);
            AddCandidate(candidates, "folder name", folderName);
            AddCandidate(candidates, "executable name", exeTitle);
            AddCandidate(candidates, "file name", fileName);

            foreach (var candidate in candidates)
            {
                if (gameListService.TryFindExactGame(candidate.Value, out game))
                {
                    matchedBy = FormatMatchEvidence(candidate);
                    return true;
                }
            }

            foreach (var candidate in candidates)
            {
                if (gameListService.TryFindGame(candidate.Value, out game))
                {
                    matchedBy = FormatMatchEvidence(candidate);
                    return true;
                }
            }

            return false;
        }

        private static void AddExecutableMetadataCandidates(
            string exePath,
            ICollection<KeyValuePair<string, string>> candidates)
        {
            if (string.IsNullOrWhiteSpace(exePath) || !File.Exists(exePath)) return;

            try
            {
                var version = FileVersionInfo.GetVersionInfo(exePath);
                AddCandidate(candidates, "product name", version.ProductName);
                AddCandidate(candidates, "file description", version.FileDescription);
            }
            catch
            {
            }
        }

        private static void AddCandidate(
            ICollection<KeyValuePair<string, string>> candidates,
            string source,
            string value)
        {
            if (string.IsNullOrWhiteSpace(value)) return;

            foreach (var candidate in candidates)
            {
                if (string.Equals(candidate.Value, value, StringComparison.OrdinalIgnoreCase))
                {
                    return;
                }
            }

            candidates.Add(new KeyValuePair<string, string>(source, value.Trim()));
        }

        private static string FormatMatchEvidence(KeyValuePair<string, string> candidate)
        {
            return candidate.Key + " '" + candidate.Value + "'";
        }

        private void HandleGameDetected(
            SupportedGame game,
            uint pid,
            string exePath,
            string matchedBy,
            long detectionStartedAt,
            DateTime? processEventUtc,
            string detectionSource)
        {
            if (game == null) return;

            try
            {
                bool attachedToExistingSession = false;
                bool resumedDuringGrace = false;
                double candidateToStartMs = 0;
                double startSessionToReadyMs = 0;
                double candidateToReadyMs = 0;

                lock (sessionStateLock)
                {
                    if (isStoppingDetectedSession) return;

                    if (processSession.HasSession && !sessionManager.IsSessionActive)
                    {
                        processSession.Clear();
                    }

                    if (processSession.HasSession)
                    {
                        if (!processSession.IsSameGame(game))
                        {
                            LogDetection(string.Format(
                                "Ignored PID {0} for '{1}' while '{2}' is active.",
                                pid,
                                game.Title,
                                processSession.ActiveGame.Title));
                            return;
                        }

                        resumedDuringGrace = processSession.IsAwaitingReplacement;
                        attachedToExistingSession = processSession.TryAttach(game, pid, exePath);
                    }
                    else
                    {
                        long startSessionCalledAt = Stopwatch.GetTimestamp();
                        string error;
                        if (!sessionManager.StartSession(game.Title, game.Profile, settings, out error))
                        {
                            lock (retryCooldowns)
                            {
                                retryCooldowns[pid] = DateTime.UtcNow.AddSeconds(10);
                            }
                            lock (evaluatedProcesses)
                            {
                                evaluatedProcesses.Remove(pid);
                            }
                            return;
                        }

                        long readyAt = Stopwatch.GetTimestamp();
                        candidateToStartMs = ElapsedMilliseconds(
                            detectionStartedAt, startSessionCalledAt);
                        startSessionToReadyMs = ElapsedMilliseconds(
                            startSessionCalledAt, readyAt);
                        candidateToReadyMs = ElapsedMilliseconds(
                            detectionStartedAt, readyAt);

                        processSession.Start(game, pid, exePath);
                    }
                }

                if (learningService != null)
                {
                    learningService.BeginObservation(
                        pid,
                        exePath,
                        game,
                        matchedBy,
                        IsLearningObservationActive);
                }

                if (attachedToExistingSession)
                {
                    LogDetection(string.Format(
                        "Attached PID {0} to active game '{1}' using {2}. Path: {3}. Grace recovery: {4}",
                        pid,
                        game.Title,
                        matchedBy ?? "unknown evidence",
                        exePath,
                        resumedDuringGrace ? "yes" : "no"));
                    return;
                }

                var processEventToReadyMs = processEventUtc.HasValue
                    ? Math.Max(0, (DateTime.UtcNow - processEventUtc.Value).TotalMilliseconds)
                    : -1;
                LogDetection(string.Format(
                    CultureInfo.InvariantCulture,
                    "Matched PID {0} to '{1}' using {2}. Path: {3}. Source: {4}. " +
                    "Candidate-to-StartSession: {5:F3} ms. StartSession-to-Ready: {6:F3} ms. " +
                    "Candidate-to-Ready: {7:F3} ms. Process-event-to-Ready: {8}",
                    pid,
                    game.Title,
                    matchedBy ?? "unknown evidence",
                    exePath,
                    detectionSource ?? "unknown",
                    candidateToStartMs,
                    startSessionToReadyMs,
                    candidateToReadyMs,
                    processEventToReadyMs >= 0
                        ? processEventToReadyMs.ToString("F3", CultureInfo.InvariantCulture) + " ms"
                        : "n/a"));

                var handler = GameDetected;
                if (handler != null)
                {
                    handler(game, exePath);
                }
            }
            catch (Exception ex)
            {
                LogEvent("[HandleGameDetected] " + ex.Message);
            }
        }

        private void HandleProcessStopped(uint processId, string reason)
        {
            try
            {
                bool removed;
                bool awaitingReplacement;
                lock (sessionStateLock)
                {
                    removed = processSession.Remove(
                        processId, DateTime.UtcNow, ProcessExitGracePeriod);
                    awaitingReplacement = processSession.IsAwaitingReplacement;
                }

                if (!removed) return;

                if (learningService != null)
                {
                    learningService.CancelObservation(processId);
                }

                LogDetection(string.Format(
                    "Detached PID {0}: {1}. Waiting for same-game replacement: {2}",
                    processId,
                    reason,
                    awaitingReplacement ? "yes" : "no"));
            }
            catch (Exception ex)
            {
                LogEvent("[HandleProcessStopped] " + ex.Message);
            }
        }

        private bool TryStopExpiredSession()
        {
            string oldPath;
            lock (sessionStateLock)
            {
                if (isStoppingDetectedSession || !processSession.ShouldStop(DateTime.UtcNow))
                {
                    return false;
                }

                isStoppingDetectedSession = true;
                oldPath = processSession.LastKnownPath;
                processSession.Clear();
            }

            try
            {
                sessionManager.StopSession("No game process remained after the PID replacement grace period");

                if (!string.IsNullOrWhiteSpace(oldPath))
                {
                    var handler = GameExited;
                    if (handler != null) handler(oldPath);
                }
                return true;
            }
            finally
            {
                lock (sessionStateLock)
                {
                    isStoppingDetectedSession = false;
                    nextProcessSweepUtc = DateTime.MinValue;
                }
                lock (evaluatedProcesses)
                {
                    evaluatedProcesses.Clear();
                }
                if (!isDisposed) ForceCheck();
            }
        }

        private bool IsLearningObservationActive(uint processId, string executablePath)
        {
            lock (sessionStateLock)
            {
                return !isDisposed &&
                       processSession.Contains(processId, executablePath) &&
                       sessionManager.IsSessionHealthy;
            }
        }

        private void PruneExitedTrackedProcesses()
        {
            uint[] trackedProcessIds;
            lock (sessionStateLock)
            {
                trackedProcessIds = processSession.GetProcessIds();
            }

            foreach (var processId in trackedProcessIds)
            {
                bool hasExited = false;
                string reason = "Process terminated";
                try
                {
                    using (var process = Process.GetProcessById((int)processId))
                    {
                        hasExited = process.HasExited;
                    }
                }
                catch (ArgumentException)
                {
                    hasExited = true;
                    reason = "Process exited";
                }
                catch (Exception)
                {
                    hasExited = true;
                    reason = "Process inaccessible";
                }

                if (hasExited)
                {
                    HandleProcessStopped(processId, reason);
                }
            }
        }

        private bool IsTrackedProcess(uint processId)
        {
            lock (sessionStateLock)
            {
                return processSession.Contains(processId);
            }
        }

        private bool HasTrackedSession()
        {
            lock (sessionStateLock)
            {
                return processSession.HasSession;
            }
        }

        private bool IsAwaitingReplacement()
        {
            lock (sessionStateLock)
            {
                return processSession.IsAwaitingReplacement;
            }
        }

        private void ResetTrackedSessionOnly()
        {
            bool hadSession;
            lock (sessionStateLock)
            {
                hadSession = processSession.HasSession;
                processSession.Clear();
            }

            if (!hadSession) return;

            if (learningService != null)
            {
                learningService.CancelObservation(0);
            }
            lock (evaluatedProcesses)
            {
                evaluatedProcesses.Clear();
            }
            nextProcessSweepUtc = DateTime.MinValue;
        }

        private static DateTime? ReadWmiEventCreatedUtc(EventArrivedEventArgs eventArgs)
        {
            if (eventArgs == null || eventArgs.NewEvent == null) return null;

            try
            {
                var property = eventArgs.NewEvent.Properties["TIME_CREATED"];
                if (property == null || property.Value == null) return null;
                return DateTime.FromFileTimeUtc(Convert.ToInt64(property.Value));
            }
            catch
            {
                return null;
            }
        }

        private static double ElapsedMilliseconds(long startedAt, long endedAt)
        {
            if (startedAt <= 0 || endedAt < startedAt) return 0;
            return (endedAt - startedAt) * 1000.0 / Stopwatch.Frequency;
        }

        private static bool IsSystemOrIgnoredProcess(string fileName)
        {
            if (string.IsNullOrWhiteSpace(fileName)) return true;
            if (PlatformClientProcessFilter.IsExcluded(fileName)) return true;
            var lower = fileName.ToLowerInvariant();
            return lower == "system.exe" ||
                   lower == "registry.exe" ||
                   lower == "secure system.exe" ||
                   lower == "memory compression.exe" ||
                   lower == "explorer.exe" ||
                   lower == "taskmgr.exe" ||
                   lower == "apexsensebridge.exe" ||
                   lower == "apexsensebridgetray.exe" ||
                   lower == "apexsensebridgecontrol.exe" ||
                   lower == "applicationframehost.exe" ||
                   lower == "shellexperiencehost.exe" ||
                   lower == "systemsettings.exe" ||
                   lower == "searchhost.exe" ||
                   lower == "startmenuexperiencehost.exe" ||
                   lower == "lockapp.exe" ||
                   lower == "devenv.exe" ||
                   lower == "cmd.exe" ||
                   lower == "powershell.exe" ||
                   lower == "pwsh.exe" ||
                   lower == "conhost.exe" ||
                   lower == "windowsterminal.exe";
        }

        private static string GetParentFolderName(string filePath)
        {
            try
            {
                var dir = Path.GetDirectoryName(filePath);
                if (string.IsNullOrWhiteSpace(dir)) return string.Empty;
                return Path.GetFileName(dir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            }
            catch
            {
                return string.Empty;
            }
        }

        private static void LogEvent(string msg)
        {
            try
            {
                File.AppendAllText(
                    Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_crash.log"),
                    DateTime.Now.ToString("s") + " " + msg + "\r\n");
            }
            catch { }
        }

        private static void LogDetection(string message)
        {
            try
            {
                File.AppendAllText(
                    Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_detection.log"),
                    DateTime.Now.ToString("s") + " " + message + "\r\n");
            }
            catch
            {
            }
        }

        public void Dispose()
        {
            if (isDisposed) return;
            isDisposed = true;

            try
            {
                if (startWatcher != null)
                {
                    startWatcher.Stop();
                    startWatcher.Dispose();
                }
            }
            catch { }

            try
            {
                if (stopWatcher != null)
                {
                    stopWatcher.Stop();
                    stopWatcher.Dispose();
                }
            }
            catch { }

            if (pollTimer != null)
            {
                pollTimer.Dispose();
            }

            bool hadTrackedSession;
            lock (sessionStateLock)
            {
                hadTrackedSession = processSession.HasSession;
                processSession.Clear();
            }

            if (learningService != null)
            {
                learningService.CancelObservation(0);
            }

            if (hadTrackedSession)
            {
                try
                {
                    sessionManager.StopSession("Tray app closing");
                }
                catch { }
            }
        }
    }
}
