using ApexSenseBridge.Common;
using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using System;
using System.Threading;

namespace ApexSenseBridgeTray.Services
{
    public class EngineSessionManager
    {
        private const string EngineSessionMutexName =
            @"Local\ApexSenseBridge.ActiveSession.Owner.v1";
        private readonly object syncLock = new object();
        private BridgeSession activeSession;
        private string activeGameTitle;
        private string activeProfile;
        private bool isStarting;
        private bool isStopping;
        private bool stopAfterStart;
        private string lastReason;
        private bool failed;
        private string controller;
        private readonly Timer healthTimer;
        private int checkingHealth;
        private string activeProfileName;
        private int activeApexSlot;
        private readonly SessionRecoveryState recovery = new SessionRecoveryState();
        private BridgeSession.SessionInfo observedExternalSession;
        private readonly Func<string> engineLocator;
        private readonly Func<bool> externalSessionProbe;
        private readonly string sessionDiscoveryName;
        public SessionRecoveryState Recovery { get { lock (syncLock) return recovery.Snapshot(); } }
        public bool AutomaticActivationBlocked { get { lock (syncLock) return recovery.BlocksAutomaticActivation; } }
        public void UpdateRecoveryController(bool available) { lock (syncLock) recovery.ControllerDetected(available); }
        public void DismissRecovery() { lock (syncLock) recovery.Dismiss(); StateChanged?.Invoke(); }

        public bool ResumeSession(TraySettings settings, out string error)
        {
            string game, profile;
            int slot;
            lock (syncLock)
            {
                if (activeSession != null || isStarting || isStopping || !recovery.Begin())
                { error = "Loc_RecoveryBusy"; return false; }
                game = recovery.Game; profile = recovery.Profile; slot = recovery.ApexSlot;
            }
            StateChanged?.Invoke();
            bool success = false;
            try { success = StartSessionCore(game, profile, settings, slot, true, out error); return success; }
            finally { lock (syncLock) { if (recovery.Resuming) recovery.Complete(success); } StateChanged?.Invoke(); }
        }

        public EngineSessionManager() : this(() => InstallLocator.ResolveEngine(),
            "Local\\ApexSenseBridge.ActiveSession.Info.v1", IsExternalSessionActive) { }

        internal EngineSessionManager(Func<string> engineLocator, string discoveryName, Func<bool> externalSessionProbe)
        {
            this.engineLocator = engineLocator;
            this.externalSessionProbe = externalSessionProbe;
            sessionDiscoveryName = discoveryName;
            healthTimer = new Timer(CheckHealth, null, 500, 500);
        }

        public string StateName { get { lock (syncLock) { return isStarting ? "Starting" : isStopping ? "Stopping" : activeSession != null ? activeSession.ReadStatus().Phase.ToString() : failed ? "Failed" : "Stopped"; } } }
        public string LastReason { get { lock (syncLock) { return lastReason; } } }
        public string ActiveController { get { lock (syncLock) { return controller; } } }
        public bool HasExternalSession { get { return externalSessionProbe(); } }
        public event Action StateChanged;

        private void CheckHealth(object unused)
        {
            if (Interlocked.Exchange(ref checkingHealth, 1) != 0) return;
            try
            {
                string exitReason = null;
                lock (syncLock)
                {
                    if (activeSession == null)
                    {
                        if (isStarting || isStopping) return;
                        var external = BridgeSession.ReadActiveSession(sessionDiscoveryName);
                        if (external != null && external.Owner != "Tray")
                        {
                            observedExternalSession = external;
                            if (external.Phase == SessionPhase.Ready) recovery.Clear();
                            return;
                        }
                        if (observedExternalSession == null) return;
                        var previous = observedExternalSession;
                        observedExternalSession = null;
                        var final = BridgeSession.ReadSessionStatus(previous.Token);
                        if (final == null || final.Phase != SessionPhase.Failed ||
                            !recovery.Offer(previous.Game, previous.Profile, 0, final.Interruption, final.Stages, previous.Owner)) return;
                        activeGameTitle = previous.Game;
                        activeProfile = previous.Profile;
                        controller = previous.Controller;
                        failed = true;
                        lastReason = final.Message;
                        exitReason = lastReason;
                    }
                    else
                    {
                        var status = activeSession.ReadStatus();
                        if (activeSession.ProcessId != 0) return;
                        failed = status.Phase != SessionPhase.Stopped;
                        lastReason = status.Message;
                        if (string.IsNullOrWhiteSpace(lastReason) || status.Phase == SessionPhase.Ready || status.Phase == SessionPhase.Stopping)
                            lastReason = "Le moteur s'est arrêté de façon inattendue.";
                        exitReason = lastReason;
                        if (!recovery.Offer(activeGameTitle, activeProfileName, activeApexSlot, status.Interruption, status.Stages)) recovery.Clear();
                        activeSession.Dispose();
                        activeSession = null;
                    }
                }
                StateChanged?.Invoke();
                SessionStopped?.Invoke(exitReason);
            }
            catch (Exception ex) { RaiseLogMessage("Session health: " + ex.Message); }
            finally { Interlocked.Exchange(ref checkingHealth, 0); }
        }

        public void Dispose() { healthTimer.Dispose(); }

        public void ReportActivationRefused(string gameTitle, string reason)
        {
            lock (syncLock)
            {
                if (activeSession != null || isStarting || isStopping || recovery.BlocksAutomaticActivation) return;
                activeGameTitle = gameTitle;
                activeProfile = null;
                lastReason = reason;
                failed = true;
            }
            StateChanged?.Invoke();
        }

        public bool IsSessionActive
        {
            get
            {
                lock (syncLock)
                {
                    return activeSession != null;
                }
            }
        }

        public bool IsSessionHealthy
        {
            get
            {
                lock (syncLock)
                {
                    return activeSession != null && activeSession.ProcessId != 0;
                }
            }
        }

        public string ActiveGameTitle
        {
            get
            {
                lock (syncLock)
                {
                    return activeGameTitle ?? "Aucun";
                }
            }
        }

        public string ActiveProfile
        {
            get
            {
                lock (syncLock)
                {
                    return activeProfile ?? "none";
                }
            }
        }

        public event Action<string, string> SessionStarted;
        public event Action<string> SessionStopped;
        public event Action<string> SessionError;
        public event Action<string> LogMessage;

        public bool StartSession(string gameTitle, string profileName, TraySettings settings, out string error)
        {
            return StartSession(gameTitle, profileName, settings, 0, out error);
        }

        public bool StartSession(string gameTitle, string profileName, TraySettings settings,
                                 int apexProfileSlot, out string error)
        {
            return StartSessionCore(gameTitle, profileName, settings, apexProfileSlot, false, out error);
        }

        private bool StartSessionCore(string gameTitle, string profileName, TraySettings settings,
                                 int apexProfileSlot, bool resuming, out string error)
        {
            error = null;

            lock (syncLock)
            {
                if (activeSession != null || isStarting || isStopping)
                {
                    error = "Une session est déjà active ou en cours d'initialisation.";
                    return false;
                }
                if (!resuming && recovery.BlocksAutomaticActivation)
                { error = "Loc_RecoveryBusy"; return false; }
                if (!resuming) recovery.Clear();
                isStarting = true;
                stopAfterStart = false;
                activeGameTitle = gameTitle;
                activeProfile = profileName + (apexProfileSlot > 0 ? " / APEX " + apexProfileSlot : "");
                lastReason = null;
                failed = false;
                controller = null;
                activeProfileName = profileName;
                activeApexSlot = apexProfileSlot;
            }
            StateChanged?.Invoke();

            try
            {
                if (externalSessionProbe())
                {
                    error = "Une session ApexSenseBridge gérée par Playnite ou une autre application est déjà active.";
                    RaiseLogMessage(error + " Le Tray laisse cette session intacte.");
                    RaiseSessionError(error);
                    return false;
                }

                var enginePath = engineLocator();
                if (string.IsNullOrWhiteSpace(enginePath))
                {
                    error = "ApexSenseBridge.exe est introuvable. Veuillez installer ou réparer ApexSenseBridge.";
                    RaiseSessionError(error);
                    return false;
                }

                var args = BuildArguments(profileName, settings, apexProfileSlot);
                RaiseLogMessage(string.Format("Starting bridge: {0} {1}", enginePath, args));

                int timeoutSec = settings != null ? settings.InitializationTimeoutSeconds : 20;
                var session = BridgeSession.TryStart(
                    enginePath,
                    args,
                    TimeSpan.FromSeconds(timeoutSec),
                    msg => RaiseLogMessage(msg),
                    err => RaiseLogMessage("[ERROR] " + err),
                    out error, gameTitle, activeProfile, sessionDiscoveryName, progressChanged: state =>
                    {
                        lock (syncLock) recovery.Observe(state.Stages);
                    });

                if (session == null)
                {
                    RaiseSessionError(error ?? "Échec du démarrage du bridge.");
                    return false;
                }

                bool cancelled;
                lock (syncLock)
                {
                    cancelled = stopAfterStart;
                    if (!cancelled)
                    {
                        activeSession = session;
                        activeGameTitle = gameTitle;
                        activeProfile = profileName + (apexProfileSlot > 0 ? " / APEX " + apexProfileSlot : "");
                        var status = session.ReadStatus();
                        controller = status.Message != null && status.Message.StartsWith("ASB_READY|") ? status.Message.Substring(10) : null;
                    }
                }
                if (cancelled)
                {
                    session.StopAndWait(TimeSpan.FromSeconds(15));
                    session.Dispose();
                    error = "Initialisation annulée.";
                    return false;
                }

                var startHandler = SessionStarted;
                if (startHandler != null)
                {
                    startHandler(gameTitle, profileName);
                }
                return true;
            }
            finally
            {
                lock (syncLock)
                {
                    isStarting = false;
                }
                StateChanged?.Invoke();
            }
        }

        public void StopSession(string reason)
        {
            BridgeSession sessionToStop = null;
            lock (syncLock)
            {
                recovery.Clear();
                if (isStarting && activeSession == null) stopAfterStart = true;
                if (activeSession == null) return;
                sessionToStop = activeSession;
                activeSession = null;
                isStopping = true;
                activeGameTitle = null;
                activeProfile = null;
                failed = false;
                lastReason = reason;
            }

            RaiseLogMessage(string.Format("Stopping session: {0}", reason));

            StateChanged?.Invoke();
            if (sessionToStop != null)
            {
                var clean = sessionToStop.StopAndWait(TimeSpan.FromSeconds(15));
                var final = sessionToStop.ReadStatus();
                lock (syncLock)
                {
                    isStopping = false;
                    if (!clean || final.Phase == SessionPhase.Failed)
                    {
                        failed = true;
                        lastReason = !clean ? "L'arrêt du moteur n'a pas été confirmé." : final.Message;
                    }
                }
                sessionToStop.Dispose();
            }
            StateChanged?.Invoke();

            var stopHandler = SessionStopped;
            if (stopHandler != null)
            {
                stopHandler(reason);
            }
        }

        private void RaiseSessionError(string err)
        {
            lock (syncLock) { lastReason = err; failed = true; }
            StateChanged?.Invoke();
            var errHandler = SessionError;
            if (errHandler != null)
            {
                errHandler(err);
            }
        }

        private static bool IsExternalSessionActive()
        {
            try
            {
                using (var sessionMutex = Mutex.OpenExisting(EngineSessionMutexName))
                {
                    try
                    {
                        if (!sessionMutex.WaitOne(0)) return true;
                        sessionMutex.ReleaseMutex();
                        return false;
                    }
                    catch (AbandonedMutexException)
                    {
                        // The previous engine crashed. This thread now owns the
                        // abandoned mutex; release it and let the engine's
                        // recovery marker perform its normal cleanup.
                        sessionMutex.ReleaseMutex();
                        return false;
                    }
                }
            }
            catch (WaitHandleCannotBeOpenedException)
            {
                return false;
            }
            catch (UnauthorizedAccessException)
            {
                // Fail closed: launching a second engine is unsafe if ownership
                // cannot be inspected.
                return true;
            }
        }

        private void RaiseLogMessage(string msg)
        {
            var logHandler = LogMessage;
            if (logHandler != null)
            {
                logHandler(msg);
            }
        }

        private static string BuildArguments(string profileName, TraySettings settings,
                                             int apexProfileSlot)
        {
            return BridgeArguments.Build(
                profileName,
                settings != null && settings.EnableRumble,
                settings != null ? settings.HapticThresholdPercent : 12,
                settings != null && settings.SyncLightbar,
                apexProfileSlot,
                settings != null ? settings.TriggerStrengthPercent : 100,
                settings != null ? settings.VibrationStrengthPercent : 100);
        }
    }
}
