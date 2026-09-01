using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace ApexSenseBridgeTray.Services
{
    public class EngineSessionManager
    {
        private readonly object syncLock = new object();
        private BridgeSession activeSession;
        private string activeGameTitle;
        private string activeProfile;
        private bool isStarting;

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
            error = null;

            lock (syncLock)
            {
                if (activeSession != null || isStarting)
                {
                    error = "Une session est déjà active ou en cours d'initialisation.";
                    return false;
                }
                isStarting = true;
            }

            try
            {
                var enginePath = InstallLocator.ResolveEngine();
                if (string.IsNullOrWhiteSpace(enginePath))
                {
                    error = "ApexSenseBridge.exe est introuvable. Veuillez installer ou réparer ApexSenseBridge.";
                    RaiseSessionError(error);
                    return false;
                }

                var args = BuildArguments(profileName, settings);
                RaiseLogMessage(string.Format("Starting bridge: {0} {1}", enginePath, args));

                KillOrphanProcesses();

                int timeoutSec = settings != null ? settings.InitializationTimeoutSeconds : 20;
                var session = BridgeSession.TryStart(
                    enginePath,
                    args,
                    TimeSpan.FromSeconds(timeoutSec),
                    msg => RaiseLogMessage(msg),
                    err => RaiseLogMessage("[ERROR] " + err),
                    out error);

                if (session == null)
                {
                    RaiseSessionError(error ?? "Échec du démarrage du bridge.");
                    KillOrphanProcesses();
                    return false;
                }

                lock (syncLock)
                {
                    activeSession = session;
                    activeGameTitle = gameTitle;
                    activeProfile = profileName;
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
            }
        }

        public void StopSession(string reason)
        {
            BridgeSession sessionToStop = null;
            lock (syncLock)
            {
                if (activeSession == null) return;
                sessionToStop = activeSession;
                activeSession = null;
                activeGameTitle = null;
                activeProfile = null;
            }

            RaiseLogMessage(string.Format("Stopping session: {0}", reason));

            if (sessionToStop != null)
            {
                sessionToStop.StopAndWait(TimeSpan.FromSeconds(15));
                sessionToStop.Dispose();
            }

            KillOrphanProcesses();

            var stopHandler = SessionStopped;
            if (stopHandler != null)
            {
                stopHandler(reason);
            }
        }

        public static void KillOrphanProcesses()
        {
            try
            {
                int currentProcId = Process.GetCurrentProcess().Id;
                Process[] runningEngines = Process.GetProcessesByName("ApexSenseBridge");
                for (int i = 0; i < runningEngines.Length; i++)
                {
                    Process p = runningEngines[i];
                    if (p.Id != currentProcId)
                    {
                        try
                        {
                            p.Kill();
                            p.WaitForExit(1000);
                        }
                        catch { }
                        finally { p.Dispose(); }
                    }
                }

                Process[] runningViipers = Process.GetProcessesByName("viiper");
                for (int i = 0; i < runningViipers.Length; i++)
                {
                    Process p = runningViipers[i];
                    try
                    {
                        p.Kill();
                        p.WaitForExit(1000);
                    }
                    catch { }
                    finally { p.Dispose(); }
                }
            }
            catch { }
        }

        private void RaiseSessionError(string err)
        {
            var errHandler = SessionError;
            if (errHandler != null)
            {
                errHandler(err);
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

        private static string BuildArguments(string profileName, TraySettings settings)
        {
            var args = new List<string> { "bridge-triggers" };

            var profile = profileName != null ? profileName.ToLowerInvariant() : "standard";
            if (profile == "spider-man-2")
            {
                args.Add("--touchpad-profile spider-man-2");
            }
            else if (profile == "miles-morales")
            {
                args.Add("--touchpad-profile miles-morales");
            }
            else if (profile == "ghost-of-tsushima")
            {
                args.Add("--touchpad-profile ghost-of-tsushima");
            }
            else if (profile == "warframe")
            {
                args.Add("--touchpad-profile warframe");
            }
            else
            {
                args.Add("--touchpad-profile none");
            }

            if (settings != null && settings.EnableRumble)
            {
                args.Add("--rumble");
                args.Add("--haptic-threshold");
                args.Add(settings.HapticThresholdPercent.ToString());
            }

            return string.Join(" ", args.ToArray());
        }
    }
}
