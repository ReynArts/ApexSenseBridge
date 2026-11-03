using ApexSenseBridge.Common;
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
    internal delegate ProcessLivenessState ProcessLivenessProbe(uint processId, long expectedCreationUtcTicks, out long creationUtcTicks);

    public class ProcessMonitorService : IDisposable
    {
        private readonly CloudGameListService gameListService;
        private readonly EngineSessionManager sessionManager;
        private readonly ExecutableLearningService learningService;
        private readonly TraySettings settings;
        private readonly Timer pollTimer;
        private readonly bool monitoringEnabled;
        private readonly Dictionary<uint, DateTime> retryCooldowns = new Dictionary<uint, DateTime>();
        private readonly Dictionary<uint, long> evaluatedProcesses = new Dictionary<uint, long>();
        private readonly HashSet<uint> manualFixWarningProcesses = new HashSet<uint>();
        private readonly object sessionStateLock = new object();
        private readonly GameProcessSessionTracker processSession = new GameProcessSessionTracker();
        private readonly StartRetryPolicy startRetries = new StartRetryPolicy();
        private readonly Dictionary<uint, Timer> quickRetryTimers = new Dictionary<uint, Timer>();
        private readonly PreparedLaunchTracker preparedLaunch = new PreparedLaunchTracker();
        internal Func<uint[]> processIdSource = NativeMethods.GetProcessIds;
        internal Func<uint, string> processPathSource = NativeMethods.GetProcessPath;
        internal Func<DateTime> retryClock = () => DateTime.UtcNow;
        internal ProcessLivenessProbe livenessSource = ProcessLiveness.Check;
        private readonly Dictionary<uint, long> trackedCreationTicks = new Dictionary<uint, long>();
        private readonly HashSet<uint> unclassifiedTrackedProcesses = new HashSet<uint>();

        private ManagementEventWatcher startWatcher;
        private ManagementEventWatcher stopWatcher;

        private bool isDisposed;
        private bool isStoppingDetectedSession;
        private int isPolling;
        private bool recoveryWasBlocked;
        private DateTime nextProcessSweepUtc;
        private DateTime nextForegroundCheckUtc;
        private DateTime nextPreparedLaunchCheckUtc;

        public event Action<SupportedGame, string> GameDetected;
        public event Action<string> GameExited;
        public event Action<SupportedGame> ManualFixRequired;
        public event Action<string, string> AutomaticStartPaused;

        public bool HasPausedAutomaticStart
        {
            get { lock (retryCooldowns) return startRetries.HasPausedGame; }
        }

        public bool HasPreparedLaunch
        {
            get { lock (sessionStateLock) return preparedLaunch.IsActive; }
        }

        public void TrackPreparedLaunch(string executablePath, string sessionTitle, uint processId)
        {
            if (string.IsNullOrWhiteSpace(executablePath)) return;
            lock (sessionStateLock) preparedLaunch.Begin(executablePath, sessionTitle, processId, DateTime.UtcNow);
            nextPreparedLaunchCheckUtc = DateTime.UtcNow.AddSeconds(1);
            LogDetection(string.Format(
                CultureInfo.InvariantCulture,
                "Tracking prepared launch '{0}' (PID {1}). The bridge stops {2:F0} s after its last process exits.",
                executablePath, processId, PreparedLaunchTracker.ExitGracePeriod.TotalSeconds));
        }

        public void CancelPreparedLaunch()
        {
            lock (sessionStateLock) preparedLaunch.Clear();
        }

        public void RetryPausedStarts()
        {
            lock (retryCooldowns)
            {
                startRetries.Reset();
                retryCooldowns.Clear();
            }
            ForceCheck();
        }

        public void ReportControllerStatus(string status)
        {
            bool sessionInProgress = SessionOwnedElsewhere();
            bool changed;
            lock (retryCooldowns) changed = startRetries.ObserveController(status, sessionInProgress);
            if (changed) RetryPausedStarts();
        }

        private bool SessionOwnedElsewhere()
        {
            return sessionManager.IsSessionActive || sessionManager.IsBusy || sessionManager.HasExternalSession;
        }

        public ProcessMonitorService(
            CloudGameListService gameListService,
            EngineSessionManager sessionManager,
            ExecutableLearningService learningService,
            TraySettings settings)
            : this(gameListService, sessionManager, learningService, settings, true) { }

        internal ProcessMonitorService(CloudGameListService gameListService,
            EngineSessionManager sessionManager, ExecutableLearningService learningService,
            TraySettings settings, bool monitoringEnabled)
        {
            this.gameListService = gameListService;
            this.sessionManager = sessionManager;
            this.learningService = learningService;
            this.settings = settings;
            this.monitoringEnabled = monitoringEnabled;

            if (monitoringEnabled)
            {
                InitializeWmiWatchers();
                pollTimer = new Timer(OnPollTick, null, 100, 250);
            }
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
                bool passiveLearning = IsManualBridgeMode();
                bool recoveryBlocked = sessionManager.AutomaticActivationBlocked;
                if (recoveryBlocked && !recoveryWasBlocked)
                {
                    lock (evaluatedProcesses) evaluatedProcesses.Clear();
                    nextProcessSweepUtc = DateTime.MinValue;
                }
                recoveryWasBlocked = recoveryBlocked;

                CheckCandidateProcess(
                    pid, processName, null, detectionStartedAt, processEventUtc, "WMI",
                    passiveLearning);
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
                    lock (manualFixWarningProcesses)
                    {
                        manualFixWarningProcesses.Remove(pid);
                    }
                    ForgetStartFailures(pid);
                    if (learningService != null)
                    {
                        learningService.CancelObservation(pid, "process stopped (WMI)");
                    }
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
            lock (evaluatedProcesses)
            {
                evaluatedProcesses.Clear();
            }
            nextProcessSweepUtc = DateTime.MinValue;
            if (!monitoringEnabled) return;
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
                PruneStartRetries();

                if (!IsManualBridgeMode()) CancelPreparedLaunch();
                else if (DateTime.UtcNow >= nextPreparedLaunchCheckUtc)
                {
                    nextPreparedLaunchCheckUtc = DateTime.UtcNow.AddSeconds(1);
                    if (TryEndPreparedLaunch(DateTime.UtcNow)) return;
                }

                if (settings == null || !settings.AutoDetectGames)
                {
                    if (!IsManualBridgeMode() && sessionManager.AutomaticActivationBlocked)
                        sessionManager.StopSession("Automatic detection disabled during recovery");
                    ResetTrackedSessionOnly();
                    ResetPendingLearning();
                    return;
                }

                bool passiveLearning = IsManualBridgeMode();
                if (passiveLearning)
                {
                    // A forced bridge already owns the engine session. Continue
                    // identifying games, but never attempt to start/stop it.
                    ResetTrackedSessionOnly();
                }

                if (!passiveLearning) ReleaseEndedSession();

                if ((passiveLearning || !HasTrackedSession() || IsAwaitingReplacement()) &&
                    DateTime.UtcNow >= nextProcessSweepUtc)
                {
                    nextProcessSweepUtc = DateTime.UtcNow.AddMilliseconds(250);
                    ScanRunningProcesses(passiveLearning);
                }

                if (!passiveLearning && TryStopExpiredSession())
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

                CheckCandidateProcess(
                    pid, fileName, exePath, 0, null, "foreground", passiveLearning);
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

        private bool ScanRunningProcesses(bool passiveLearning = false)
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

                        long startedAt = ProcessLiveness.TryGetCreationTimeUtcTicks(pid);
                        lock (evaluatedProcesses)
                        {
                            evaluatedProcesses[pid] = startedAt;
                        }

                        string exePath = null;
                        try
                        {
                            // PROCESS_QUERY_LIMITED_INFORMATION is enough for most
                            // elevated games where Process.MainModule is denied.
                            exePath = NativeMethods.GetProcessPath(pid);
                            if (string.IsNullOrWhiteSpace(exePath) && process.MainModule != null)
                                exePath = process.MainModule.FileName;
                            if (!string.IsNullOrWhiteSpace(exePath))
                                fileName = Path.GetFileName(exePath);
                        }
                        catch
                        {
                        }

                        if (IsSystemOrIgnoredProcess(fileName)) continue;

                        CheckCandidateProcess(
                            pid, fileName, exePath, 0, null, "poll", passiveLearning);
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
                    lock (manualFixWarningProcesses)
                    {
                        manualFixWarningProcesses.Remove(pid);
                    }
                    ForgetStartFailures(pid);
                    if (learningService != null)
                    {
                        learningService.CancelObservation(pid, "process no longer enumerated");
                    }
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
            string detectionSource = "poll",
            bool passiveLearning = false)
        {
            if (detectionStartedAt == 0) detectionStartedAt = Stopwatch.GetTimestamp();
            if (IsTrackedProcess(pid) || IsSystemOrIgnoredProcess(fileName)) return;

            if (!passiveLearning)
            {
                lock (retryCooldowns)
                {
                    if (retryCooldowns.ContainsKey(pid))
                    {
                        if (retryClock() < retryCooldowns[pid])
                        {
                            return;
                        }
                        retryCooldowns.Remove(pid);
                    }
                }
            }

            string exePath = fullPath;
            if (string.IsNullOrWhiteSpace(exePath))
            {
                try
                {
                    exePath = NativeMethods.GetProcessPath(pid);
                    if (string.IsNullOrWhiteSpace(exePath))
                    {
                        using (var proc = Process.GetProcessById((int)pid))
                        {
                            exePath = proc.MainModule != null ? proc.MainModule.FileName : fileName;
                        }
                    }
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
            bool matchedByInstallFolder = false;
            if (!TryResolveGame(exePath, exeTitle, folderName, fileName, out matchedGame, out matchedBy))
            {
                matchedByInstallFolder = TryMatchActiveInstallFolder(exePath, out matchedGame, out matchedBy);
            }
            if (matchedGame != null)
            {
                if (sessionManager.AutomaticActivationBlocked)
                {
                    var recovery = sessionManager.Recovery;
                    // Keep tracking only the interrupted game's lifetime (and
                    // launcher PID handoffs). Never start or take over its engine.
                    if (!IsManualBridgeMode() && string.Equals(matchedGame.Title, recovery.Game, StringComparison.OrdinalIgnoreCase))
                    {
                        lock (sessionStateLock)
                        {
                            if (!isStoppingDetectedSession)
                            {
                                if (!processSession.HasSession) processSession.Start(matchedGame, pid, exePath);
                                else if (processSession.IsSameGame(matchedGame)) processSession.TryAttach(matchedGame, pid, exePath);
                            }
                        }
                    }
                    return;
                }
                if (GameActivationPolicy.IsBlockedByManualFix(
                    matchedGame, settings, exeTitle, folderName, fileName))
                {
                    sessionManager.ReportActivationRefused(matchedGame.Title, "Loc_RefusedManualFix");
                    bool firstWarning;
                    lock (manualFixWarningProcesses)
                    {
                        firstWarning = manualFixWarningProcesses.Add(pid);
                    }
                    if (firstWarning)
                    {
                        LogDetection(string.Format(
                            "Skipped '{0}' because its selected DualSense features require a manual fix. Path: {1}",
                            matchedGame.Title,
                            exePath));
                        var manualFixHandler = ManualFixRequired;
                        if (manualFixHandler != null) manualFixHandler(matchedGame);
                    }
                    return;
                }

                if (GameActivationPolicy.ShouldActivate(
                    matchedGame, settings, exeTitle, folderName, fileName))
                {
                    if (passiveLearning)
                    {
                        HandlePassiveLearningDetected(
                            matchedGame, pid, exePath, matchedBy, detectionSource);
                    }
                    else
                    {
                        HandleGameDetected(
                            matchedGame, pid, exePath, matchedBy, detectionStartedAt,
                            processEventUtc, detectionSource, !matchedByInstallFolder);
                    }
                }
                else
                {
                    var excluded = settings.IsGameExcluded(matchedGame.Normalized) || settings.IsGameExcluded(matchedGame.Title) ||
                        settings.IsGameExcluded(exeTitle) || settings.IsGameExcluded(folderName) || settings.IsGameExcluded(fileName) ||
                        (matchedGame.SteamAppIdVerified && settings.IsGameExcluded(matchedGame.SteamAppId.ToString()));
                    sessionManager.ReportActivationRefused(matchedGame.Title, excluded ? "Loc_RefusedExcluded" : "Loc_RefusedCriteria");
                }
            }
        }

        private void HandlePassiveLearningDetected(
            SupportedGame game,
            uint pid,
            string exePath,
            string matchedBy,
            string detectionSource)
        {
            if (learningService == null || game == null) return;

            BeginExecutableObservation(pid, exePath, game, matchedBy);

            LogDetection(string.Format(
                CultureInfo.InvariantCulture,
                "Passively matched PID {0} to '{1}' using {2}. Path: {3}. Source: {4}. " +
                "The forced bridge session remains untouched.",
                pid,
                game.Title,
                matchedBy ?? "unknown evidence",
                exePath,
                detectionSource ?? "unknown"));
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

            string configuredGame;
            if (settings != null && settings.TryGetGameForExecutable(exePath, out configuredGame) &&
                gameListService.TryFindExactGame(configuredGame, out game))
            {
                matchedBy = "configured exact path '" + exePath + "'";
                return true;
            }

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
            string detectionSource,
            bool learnExecutable = true)
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
                    if (isStoppingDetectedSession || sessionManager.AutomaticActivationBlocked) return;

                    ReleaseEndedSession();

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
                        if (IsStartDeferred(game, pid)) return;
                        long startSessionCalledAt = Stopwatch.GetTimestamp();
                        string error;
                        int apexProfileSlot = settings != null
                            ? settings.GetApexProfileSlot(game.Normalized)
                            : 0;
                        // Learning is about the stability of the identified game
                        // process. It must not wait for controller initialization,
                        // which can legitimately fail or take up to the timeout.
                        if (learnExecutable) BeginExecutableObservation(pid, exePath, game, matchedBy);
                        int exitCode;
                        if (!sessionManager.StartSession(
                                game.Title, game.Profile, settings,
                                apexProfileSlot, out error, out exitCode))
                        {
                            ScheduleStartRetry(game, pid, exitCode, error);
                            return;
                        }
                        lock (retryCooldowns) retryCooldowns.Remove(pid);

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

                if (learnExecutable) BeginExecutableObservation(pid, exePath, game, matchedBy);

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

        private void ScheduleStartRetry(SupportedGame game, uint pid, int exitCode, string error)
        {
            TimeSpan? delay = StartRetryPolicy.RetryDelay;
            bool sessionOwnedElsewhere = SessionOwnedElsewhere();
            lock (retryCooldowns)
            {
                var now = retryClock();
                if (!sessionOwnedElsewhere) delay = startRetries.RecordFailure(GetRetryKey(game), exitCode, error, now, pid);
                var retryAt = delay.HasValue ? now.Add(delay.Value) : DateTime.MaxValue;
                retryCooldowns[pid] = retryAt;
                if (delay.HasValue && delay.Value < StartRetryPolicy.RetryDelay)
                {
                    Timer previous;
                    if (quickRetryTimers.TryGetValue(pid, out previous)) previous.Dispose();
                    Timer timer = null;
                    timer = new Timer(state => ReleaseRetryCooldown(pid, retryAt, timer), null,
                        Timeout.Infinite, Timeout.Infinite);
                    quickRetryTimers[pid] = timer;
                    timer.Change(delay.Value, Timeout.InfiniteTimeSpan);
                }
            }
            lock (evaluatedProcesses)
            {
                evaluatedProcesses.Remove(pid);
            }
            if (!delay.HasValue) NotifyAutomaticStartPaused(game, pid, exitCode, error);
        }

        private void NotifyAutomaticStartPaused(SupportedGame game, uint pid, int exitCode, string error)
        {
            LogDetection(string.Format(
                CultureInfo.InvariantCulture,
                "Paused automatic starts for '{1}' (PID {0}) after {2} identical failures within {3:F0} s (code {4}): {5}",
                pid, game.Title, StartRetryPolicy.IdenticalFailureLimit,
                StartRetryPolicy.IdenticalFailureWindow.TotalSeconds, exitCode, error));
            var handler = AutomaticStartPaused;
            if (handler != null) handler(game.Title, error);
        }

        private bool IsStartDeferred(SupportedGame game, uint pid)
        {
            DateTime retryAt;
            lock (retryCooldowns)
            {
                var key = GetRetryKey(game);
                if (!startRetries.IsDeferred(key, retryClock(), out retryAt)) return false;
                startRetries.Observe(key, pid);
                retryCooldowns[pid] = retryAt;
            }
            lock (evaluatedProcesses)
            {
                evaluatedProcesses.Remove(pid);
            }
            LogDetection(string.Format(
                CultureInfo.InvariantCulture,
                "Deferred automatic start for PID {0} ('{1}') until {2}: an earlier start of this game failed.",
                pid, game.Title,
                retryAt == DateTime.MaxValue ? "Retry" : retryAt.ToString("HH:mm:ss", CultureInfo.InvariantCulture)));
            return true;
        }

        private void ReleaseEndedSession()
        {
            SupportedGame game;
            uint[] processIds;
            lock (sessionStateLock)
            {
                if (!processSession.HasSession || sessionManager.IsSessionActive ||
                    sessionManager.AutomaticActivationBlocked) return;
                game = processSession.ActiveGame;
                processIds = processSession.GetProcessIds();
                processSession.Clear();
            }

            int exitCode;
            string reason;
            if (sessionManager.TryTakeUnexpectedExit(out exitCode, out reason) &&
                !sessionManager.AutomaticActivationBlocked)
            {
                RecordSessionFailure(game, processIds, exitCode, reason);
            }

            lock (evaluatedProcesses)
            {
                evaluatedProcesses.Clear();
            }
            nextProcessSweepUtc = DateTime.MinValue;
        }

        private void RecordSessionFailure(SupportedGame game, uint[] processIds, int exitCode, string reason)
        {
            TimeSpan? delay;
            lock (retryCooldowns)
            {
                var now = retryClock();
                delay = startRetries.RecordFailure(GetRetryKey(game), exitCode, reason, now, processIds);
                var retryAt = delay.HasValue ? now.Add(delay.Value) : DateTime.MaxValue;
                foreach (var pid in processIds) retryCooldowns[pid] = retryAt;
            }
            LogDetection(string.Format(
                CultureInfo.InvariantCulture,
                "The bridge for '{0}' stopped unexpectedly (code {1}): {2}. Next automatic start: {3}",
                game.Title, exitCode, reason,
                delay.HasValue ? "in " + delay.Value.TotalSeconds.ToString("F0", CultureInfo.InvariantCulture) + " s" : "paused"));
            if (!delay.HasValue)
                NotifyAutomaticStartPaused(game, processIds.Length > 0 ? processIds[0] : 0, exitCode, reason);
        }

        private static string GetRetryKey(SupportedGame game)
        {
            if (game == null) return string.Empty;
            var identity = !string.IsNullOrWhiteSpace(game.Normalized) ? game.Normalized : game.Title;
            return CloudGameListService.Normalize(identity ?? string.Empty);
        }

        private bool TryMatchActiveInstallFolder(string exePath, out SupportedGame game, out string matchedBy)
        {
            lock (sessionStateLock)
            {
                if (processSession.IsInInstallFolder(exePath))
                {
                    game = processSession.ActiveGame;
                    matchedBy = "install folder '" + processSession.InstallFolder + "' of the active game";
                    return true;
                }
            }
            game = null;
            matchedBy = null;
            return false;
        }

        internal bool TryEndPreparedLaunch(DateTime nowUtc)
        {
            string path;
            string title;
            lock (sessionStateLock)
            {
                if (!preparedLaunch.IsActive) return false;
                var running = processIdSource();
                if (running == null || running.Length == 0) return false;
                preparedLaunch.Update(new HashSet<uint>(running), processPathSource, nowUtc);
                if (!preparedLaunch.ShouldEnd(nowUtc)) return false;
                path = preparedLaunch.ExecutablePath;
                title = preparedLaunch.SessionTitle;
                preparedLaunch.Clear();
            }

            bool sessionPresent = sessionManager.IsSessionActive || sessionManager.IsBusy;
            if (sessionPresent && !string.Equals(sessionManager.ActiveGameTitle, title, StringComparison.OrdinalIgnoreCase))
                return false;

            LogDetection(string.Format(
                CultureInfo.InvariantCulture,
                "The game started with the bridge ('{0}') has no process left after {1:F0} s; leaving forced activation.",
                path, PreparedLaunchTracker.ExitGracePeriod.TotalSeconds));
            if (settings != null) settings.ForcedProfile = "none";
            if (sessionManager.IsSessionActive) sessionManager.StopSession("The game started with the bridge has exited");
            var handler = GameExited;
            if (handler != null) handler(path);
            ForceCheck();
            return true;
        }

        private void ReleaseRetryCooldown(uint pid, DateTime scheduled, Timer timer)
        {
            lock (retryCooldowns)
            {
                Timer current;
                if (quickRetryTimers.TryGetValue(pid, out current) && ReferenceEquals(current, timer))
                    quickRetryTimers.Remove(pid);
                timer.Dispose();
                DateTime retryAt;
                if (isDisposed || !retryCooldowns.TryGetValue(pid, out retryAt) || retryAt != scheduled) return;
                retryCooldowns.Remove(pid);
            }
            lock (evaluatedProcesses)
            {
                evaluatedProcesses.Remove(pid);
            }
            nextProcessSweepUtc = DateTime.MinValue;
        }

        private void PruneStartRetries()
        {
            lock (retryCooldowns)
            {
                if (retryCooldowns.Count == 0 && quickRetryTimers.Count == 0 && !startRetries.HasEntries) return;
            }
            var processIds = NativeMethods.GetProcessIds();
            if (processIds.Length == 0) return;
            PruneStartRetries(new HashSet<uint>(processIds));
        }

        private void PruneStartRetries(ICollection<uint> runningProcessIds)
        {
            lock (retryCooldowns)
            {
                var stopped = new List<uint>();
                foreach (var pid in retryCooldowns.Keys)
                    if (!runningProcessIds.Contains(pid)) stopped.Add(pid);
                foreach (var pid in stopped) retryCooldowns.Remove(pid);
                stopped.Clear();
                foreach (var pid in quickRetryTimers.Keys)
                    if (!runningProcessIds.Contains(pid)) stopped.Add(pid);
                foreach (var pid in stopped)
                {
                    quickRetryTimers[pid].Dispose();
                    quickRetryTimers.Remove(pid);
                }
                startRetries.Retain(runningProcessIds, retryClock());
            }
        }

        private void ForgetStartFailures(uint pid)
        {
            lock (retryCooldowns)
            {
                startRetries.Forget(pid, retryClock());
                retryCooldowns.Remove(pid);
            }
        }

        private void HandleProcessStopped(uint processId, string reason)
        {
            try
            {
                bool removed;
                bool awaitingReplacement;
                TimeSpan grace;
                lock (sessionStateLock)
                {
                    var now = DateTime.UtcNow;
                    grace = processSession.GetExitGracePeriod(now);
                    removed = processSession.Remove(processId, now, grace);
                    awaitingReplacement = processSession.IsAwaitingReplacement;
                }

                if (!removed) return;

                if (awaitingReplacement)
                {
                    lock (evaluatedProcesses)
                    {
                        evaluatedProcesses.Clear();
                    }
                    nextProcessSweepUtc = DateTime.MinValue;
                }

                if (learningService != null)
                {
                    learningService.CancelObservation(processId, reason);
                }

                LogDetection(string.Format(
                    CultureInfo.InvariantCulture,
                    "Detached PID {0}: {1}. Waiting for same-game replacement: {2}",
                    processId,
                    reason,
                    awaitingReplacement ? "yes (" + grace.TotalSeconds.ToString("F0", CultureInfo.InvariantCulture) + " s)" : "no"));
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

        private void BeginExecutableObservation(
            uint processId,
            string executablePath,
            SupportedGame game,
            string detectionMethod)
        {
            if (learningService == null) return;
            var observedStartTicks = ProcessLiveness.TryGetCreationTimeUtcTicks(processId);
            learningService.BeginObservation(
                processId,
                executablePath,
                game,
                detectionMethod,
                (pid, path) => IsExecutableObservationActive(
                    pid, path, observedStartTicks, game));
        }

        private bool IsExecutableObservationActive(
            uint processId,
            string executablePath,
            long observedStartTicks,
            SupportedGame game)
        {
            if (isDisposed || settings == null || !settings.AutoDetectGames)
            {
                return false;
            }

            var liveness = ProcessLiveness.Check(processId, observedStartTicks);
            if (liveness == ProcessLivenessState.Exited || liveness == ProcessLivenessState.Replaced)
            {
                return false;
            }

            var activePath = NativeMethods.GetProcessPath(processId);
            if (!string.IsNullOrWhiteSpace(activePath))
            {
                if (!string.Equals(
                    activePath, executablePath, StringComparison.OrdinalIgnoreCase))
                {
                    return false;
                }
                return IsStillEligibleForLearning(game, executablePath);
            }

            try
            {
                using (var process = Process.GetProcessById((int)processId))
                {
                    if (!string.Equals(
                            process.ProcessName,
                            Path.GetFileNameWithoutExtension(executablePath),
                            StringComparison.OrdinalIgnoreCase))
                    {
                        return false;
                    }
                    return IsStillEligibleForLearning(game, executablePath);
                }
            }
            catch
            {
                return false;
            }
        }

        private bool IsStillEligibleForLearning(SupportedGame game, string executablePath)
        {
            var fileName = Path.GetFileName(executablePath);
            return GameActivationPolicy.ShouldActivate(
                game,
                settings,
                Path.GetFileNameWithoutExtension(executablePath),
                GetParentFolderName(executablePath),
                fileName);
        }

        private bool IsManualBridgeMode()
        {
            return settings != null &&
                   !string.IsNullOrWhiteSpace(settings.ForcedProfile) &&
                   !string.Equals(settings.ForcedProfile, "none", StringComparison.OrdinalIgnoreCase);
        }

        private void PruneExitedTrackedProcesses()
        {
            uint[] trackedProcessIds;
            lock (sessionStateLock)
            {
                trackedProcessIds = processSession.GetProcessIds();
                RetainTrackedIdentities(trackedProcessIds);
            }

            foreach (var processId in trackedProcessIds)
            {
                long expectedCreationTicks;
                lock (sessionStateLock)
                {
                    trackedCreationTicks.TryGetValue(processId, out expectedCreationTicks);
                }

                long creationTicks;
                var state = livenessSource(processId, expectedCreationTicks, out creationTicks);
                if (state == ProcessLivenessState.Alive)
                {
                    if (expectedCreationTicks == 0 && creationTicks > 0)
                    {
                        lock (sessionStateLock)
                        {
                            if (processSession.Contains(processId)) trackedCreationTicks[processId] = creationTicks;
                        }
                    }
                    continue;
                }

                if (state == ProcessLivenessState.Unknown)
                {
                    bool firstReport;
                    lock (sessionStateLock)
                    {
                        firstReport = unclassifiedTrackedProcesses.Add(processId);
                    }
                    if (firstReport)
                    {
                        LogDetection(string.Format(
                            CultureInfo.InvariantCulture,
                            "PID {0} liveness could not be determined; keeping it attached.",
                            processId));
                    }
                    continue;
                }

                HandleProcessStopped(
                    processId,
                    state == ProcessLivenessState.Replaced ? "Process ID reused by another process" : "Process exited");
            }
        }

        private void RetainTrackedIdentities(uint[] trackedProcessIds)
        {
            var tracked = new HashSet<uint>(trackedProcessIds);
            var stale = new List<uint>();
            foreach (var processId in trackedCreationTicks.Keys)
                if (!tracked.Contains(processId)) stale.Add(processId);
            foreach (var processId in stale) trackedCreationTicks.Remove(processId);
            unclassifiedTrackedProcesses.RemoveWhere(processId => !tracked.Contains(processId));
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

            lock (evaluatedProcesses)
            {
                evaluatedProcesses.Clear();
            }
            nextProcessSweepUtc = DateTime.MinValue;
        }

        private void ResetPendingLearning()
        {
            if (learningService != null)
            {
                learningService.CancelObservation(0, "automatic detection disabled");
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
            AppLog.WriteLine("tray_crash.log", msg);
        }

        private static void LogDetection(string message)
        {
            AppLog.WriteLine("tray_detection.log", message);
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
            lock (retryCooldowns)
            {
                foreach (var timer in quickRetryTimers.Values) timer.Dispose();
                quickRetryTimers.Clear();
            }

            bool hadTrackedSession;
            lock (sessionStateLock)
            {
                hadTrackedSession = processSession.HasSession;
                processSession.Clear();
            }

            if (learningService != null)
            {
                learningService.CancelObservation(0, "tray app closing");
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
