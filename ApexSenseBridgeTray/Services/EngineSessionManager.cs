using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using System;
using System.Collections.Generic;

namespace ApexSenseBridgeTray.Services
{
    public class EngineSessionManager
    {
        private readonly object syncLock = new object();
        private BridgeSession activeSession;
        private string activeGameTitle;
        private string activeProfile;

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

        public event Action<string, string> SessionStarted; // gameTitle, profile
        public event Action<string> SessionStopped; // reason
        public event Action<string> SessionError; // errorMessage
        public event Action<string> LogMessage;

        public bool StartSession(string gameTitle, string profileName, TraySettings settings, out string error)
        {
            error = null;
            lock (syncLock)
            {
                if (activeSession != null)
                {
                    error = "Une session est déjà active.";
                    return false;
                }

                var enginePath = InstallLocator.ResolveEngine();
                if (string.IsNullOrWhiteSpace(enginePath))
                {
                    error = "ApexSenseBridge.exe est introuvable. Veuillez installer ou réparer ApexSenseBridge.";
                    var errHandler = SessionError;
                    if (errHandler != null)
                    {
                        errHandler(error);
                    }
                    return false;
                }

                var args = BuildArguments(profileName, settings);
                var logHandler = LogMessage;
                if (logHandler != null)
                {
                    logHandler(string.Format("Starting bridge: {0} {1}", enginePath, args));
                }

                int timeoutSec = settings != null ? settings.InitializationTimeoutSeconds : 20;
                var session = BridgeSession.TryStart(
                    enginePath,
                    args,
                    TimeSpan.FromSeconds(timeoutSec),
                    msg =>
                    {
                        var lh = LogMessage;
                        if (lh != null) lh(msg);
                    },
                    err =>
                    {
                        var lh = LogMessage;
                        if (lh != null) lh("[ERROR] " + err);
                    },
                    out error);

                if (session == null)
                {
                    var errHandler = SessionError;
                    if (errHandler != null)
                    {
                        errHandler(error ?? "Échec du démarrage du bridge.");
                    }
                    return false;
                }

                activeSession = session;
                activeGameTitle = gameTitle;
                activeProfile = profileName;

                var startHandler = SessionStarted;
                if (startHandler != null)
                {
                    startHandler(gameTitle, profileName);
                }
                return true;
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

            var logHandler = LogMessage;
            if (logHandler != null)
            {
                logHandler(string.Format("Stopping session: {0}", reason));
            }

            if (sessionToStop != null)
            {
                sessionToStop.StopAndWait(TimeSpan.FromSeconds(15));
                sessionToStop.Dispose();
            }

            var stopHandler = SessionStopped;
            if (stopHandler != null)
            {
                stopHandler(reason);
            }
        }

        private static string BuildArguments(string profileName, TraySettings settings)
        {
            var args = new List<string> { "bridge-triggers" };

            var profile = profileName != null ? profileName.ToLowerInvariant() : "standard";
            if (profile == "spider-man-2")
            {
                args.Add("--spiderman2-wgi-fix");
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
