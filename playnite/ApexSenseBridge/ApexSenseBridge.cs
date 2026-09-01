using Playnite.SDK;
using Playnite.SDK.Events;
using Playnite.SDK.Models;
using Playnite.SDK.Plugins;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Controls;

namespace ApexSenseBridge
{
    public class ApexSenseBridge : GenericPlugin
    {
        private static readonly ILogger logger = LogManager.GetLogger();
        private const string UpdateNotificationId = "ApexSenseBridge_UpdateAvailable";
        private readonly object sessionLock = new object();
        private readonly ApexSenseBridgeSettingsViewModel settings;
        private BridgeSession activeSession;
        private Guid activeGameId;

        public override Guid Id { get; } = Guid.Parse("e41b1737-6753-4b59-bc65-4fdd6a7df7f4");

        public ApexSenseBridge(IPlayniteAPI api) : base(api)
        {
            settings = new ApexSenseBridgeSettingsViewModel(this);
            Properties = new GenericPluginProperties
            {
                HasSettings = true
            };
        }

        public override void OnApplicationStarted(OnApplicationStartedEventArgs args)
        {
            base.OnApplicationStarted(args);

            if (settings.Settings.AutoCheckUpdates)
            {
                Task.Run(async () =>
                {
                    try
                    {
                        await Task.Delay(3000);

                        var result = await UpdateManager.CheckForUpdateAsync();
                        settings.Settings.LastUpdateCheckUtc = DateTime.UtcNow;

                        if (result.IsUpdateAvailable && !string.IsNullOrWhiteSpace(result.SetupDownloadUrl))
                        {
                            logger.Info($"ApexSenseBridge update available: {result.TagName}");

                            PlayniteApi.Notifications.Add(new NotificationMessage(
                                UpdateNotificationId,
                                $"Une mise à jour ApexSenseBridge ({result.TagName}) est disponible. Cliquez ici pour l'installer silencieusement.",
                                NotificationType.Info,
                                () =>
                                {
                                    InstallUpdateWithProgress(result);
                                }));
                        }
                    }
                    catch (Exception ex)
                    {
                        logger.Warn(ex, "Background update check failed.");
                    }
                });
            }
        }

        private void InstallUpdateWithProgress(UpdateCheckResult result)
        {
            PlayniteApi.Dialogs.ActivateGlobalProgress(
                progressArgs =>
                {
                    progressArgs.CurrentProgressValue = 0;
                    progressArgs.Text = "Téléchargement de la mise à jour ApexSenseBridge...";

                    var progress = new Progress<double>(p =>
                    {
                        progressArgs.CurrentProgressValue = p;
                        progressArgs.Text = $"Téléchargement : {p:0}%";
                    });

                    try
                    {
                        var installerPath = UpdateManager.DownloadSetupAsync(result.SetupDownloadUrl, result.TagName, progress).GetAwaiter().GetResult();
                        progressArgs.Text = "Installation en arrière-plan...";

                        string error;
                        if (UpdateManager.ExecuteInstallerSilently(installerPath, out error))
                        {
                            PlayniteApi.Notifications.Remove(UpdateNotificationId);
                            PlayniteApi.Dialogs.ShowMessage(
                                $"La mise à jour {result.TagName} a été installée avec succès.\n\nVeuillez redémarrer Playnite pour recharger la nouvelle version de l'extension.",
                                "ApexSenseBridge - Mise à jour");
                        }
                        else if (!string.IsNullOrWhiteSpace(error))
                        {
                            PlayniteApi.Dialogs.ShowErrorMessage(error, "ApexSenseBridge");
                        }
                    }
                    catch (Exception ex)
                    {
                        logger.Error(ex, "Failed to download and install update via notification.");
                        PlayniteApi.Dialogs.ShowErrorMessage($"Erreur lors de la mise à jour : {ex.Message}", "ApexSenseBridge");
                    }
                },
                new GlobalProgressOptions("Mise à jour ApexSenseBridge", false) { IsIndeterminate = false });
        }

        public override void OnGameStarting(OnGameStartingEventArgs args)
        {
            bool automatic;
            string detectionReason;
            var profile = settings.Settings.ResolveProfile(
                args.Game, out automatic, out detectionReason);
            if (profile == null)
            {
                return;
            }

            if (automatic)
            {
                logger.Info($"ApexSenseBridge automatically selected {profile.ProfileType} for " +
                            $"{args.Game.Name} from {detectionReason}.");
            }

            lock (sessionLock)
            {
                if (activeSession != null)
                {
                    CancelStartup(args, "Une autre session ApexSenseBridge est déjà active.");
                    return;
                }

                var bridgeExecutable = ResolveBridgeExecutable(settings.Settings);
                if (string.IsNullOrWhiteSpace(bridgeExecutable))
                {
                    CancelStartup(args,
                        "L'installation ApexSenseBridge est introuvable. Lancez ApexSenseBridge-Setup.exe puis redémarrez Playnite.");
                    return;
                }

                var arguments = BuildBridgeArguments(profile, args.Game);
                string error;
                var session = BridgeSession.TryStart(
                    bridgeExecutable,
                    arguments,
                    TimeSpan.FromSeconds(settings.Settings.InitializationTimeoutSeconds),
                    logger,
                    out error);
                if (session == null)
                {
                    CancelStartup(args, error);
                    return;
                }

                activeSession = session;
                activeGameId = args.Game.Id;
                logger.Info($"ApexSenseBridge ready for {args.Game.Name} ({args.Game.Id}).");
            }
        }

        public override void OnGameStopped(OnGameStoppedEventArgs args)
        {
            StopSession(args.Game.Id, "game stopped");
        }

        public override void OnGameStartupCancelled(OnGameStartupCancelledEventArgs args)
        {
            StopSession(args.Game.Id, "game startup cancelled");
        }

        public override void OnApplicationStopped(OnApplicationStoppedEventArgs args)
        {
            StopSession(null, "Playnite stopped");
        }

        public override IEnumerable<GameMenuItem> GetGameMenuItems(GetGameMenuItemsArgs args)
        {
            var games = args.Games?.ToList() ?? new List<Game>();
            if (games.Count == 1)
            {
                var game = games[0];
                var profile = settings.Settings.FindProfile(game.Id);
                var isStandard = profile != null && profile.ProfileType == BridgeProfileType.StandardDualSense;
                var isSpiderMan2 = profile != null && profile.ProfileType == BridgeProfileType.SpiderMan2;
                var isDisabled = profile != null && profile.ProfileType == BridgeProfileType.Disabled;
                var isAutomatic = profile == null;

                yield return new GameMenuItem
                {
                    MenuSection = "ApexSenseBridge",
                    Description = (isStandard ? "✓ " : "   ") + "DualSense standard",
                    Action = action => SetProfiles(action.Games, BridgeProfileType.StandardDualSense)
                };
                yield return new GameMenuItem
                {
                    MenuSection = "ApexSenseBridge",
                    Description = (isSpiderMan2 ? "✓ " : "   ") + "Spider-Man 2",
                    Action = action => SetProfiles(action.Games, BridgeProfileType.SpiderMan2)
                };
                yield return new GameMenuItem
                {
                    MenuSection = "ApexSenseBridge",
                    Description = (isDisabled ? "✓ " : "   ") + "Désactiver pour ce jeu",
                    Action = action => DisableProfiles(action.Games)
                };
                yield return new GameMenuItem
                {
                    MenuSection = "ApexSenseBridge",
                    Description = (isAutomatic ? "✓ " : "   ") + "Utiliser la détection automatique",
                    Action = action => RestoreAutomaticProfiles(action.Games)
                };
            }
            else
            {
                yield return new GameMenuItem
                {
                    MenuSection = "ApexSenseBridge",
                    Description = "Activer le profil DualSense standard",
                    Action = action => SetProfiles(action.Games, BridgeProfileType.StandardDualSense)
                };
                yield return new GameMenuItem
                {
                    MenuSection = "ApexSenseBridge",
                    Description = "Activer le profil Spider-Man 2",
                    Action = action => SetProfiles(action.Games, BridgeProfileType.SpiderMan2)
                };
                yield return new GameMenuItem
                {
                    MenuSection = "ApexSenseBridge",
                    Description = "Désactiver pour la sélection",
                    Action = action => DisableProfiles(action.Games)
                };
                yield return new GameMenuItem
                {
                    MenuSection = "ApexSenseBridge",
                    Description = "Utiliser la détection automatique",
                    Action = action => RestoreAutomaticProfiles(action.Games)
                };
            }
        }

        public override ISettings GetSettings(bool firstRunSettings)
        {
            return settings;
        }

        public override UserControl GetSettingsView(bool firstRunSettings)
        {
            return new ApexSenseBridgeSettingsView();
        }

        internal string ResolveBridgeExecutable(ApexSenseBridgeSettings value)
        {
            return InstallLocator.ResolveEngine(value?.BridgeExecutablePath);
        }

        internal void SaveSettings(ApexSenseBridgeSettings value)
        {
            SavePluginSettings(value);
        }

        private string BuildBridgeArguments(GameBridgeProfile profile, Game game)
        {
            var arguments = new List<string> { "bridge-triggers" };

            // A manually selected Standard profile keeps the standard launch
            // compatibility path, but known games still receive their input
            // gesture mapping. This mirrors the standalone tray detector.
            var gestureProfile = profile.ProfileType;
            if (gestureProfile == BridgeProfileType.StandardDualSense &&
                AutomaticProfileDetector.TryDetect(game, out var detectedProfile, out _))
            {
                gestureProfile = detectedProfile;
            }

            switch (gestureProfile)
            {
                case BridgeProfileType.SpiderMan2:
                    arguments.Add("--touchpad-profile");
                    arguments.Add("spider-man-2");
                    break;
                case BridgeProfileType.MilesMorales:
                    arguments.Add("--touchpad-profile");
                    arguments.Add("miles-morales");
                    break;
                case BridgeProfileType.GhostOfTsushima:
                    arguments.Add("--touchpad-profile");
                    arguments.Add("ghost-of-tsushima");
                    break;
                case BridgeProfileType.Warframe:
                    arguments.Add("--touchpad-profile");
                    arguments.Add("warframe");
                    break;
                default:
                    arguments.Add("--touchpad-profile");
                    arguments.Add("none");
                    break;
            }

            if (settings.Settings.EnableRumble)
            {
                arguments.Add("--rumble");
                arguments.Add("--haptic-threshold");
                arguments.Add(settings.Settings.HapticThresholdPercent.ToString());
            }

            return string.Join(" ", arguments);
        }

        private void SetProfiles(IEnumerable<Game> games, BridgeProfileType profileType)
        {
            var list = games?.ToList() ?? new List<Game>();
            foreach (var game in list)
            {
                settings.Settings.SetProfile(game, profileType);
            }
            settings.SaveNow();
            if (list.Count > 0)
            {
                var displayName = GameBridgeProfile.GetProfileDisplayName(profileType);
                PlayniteApi.Dialogs.ShowMessage(
                    $"Profil « {displayName} » activé pour {list.Count} jeu(x).",
                    "ApexSenseBridge");
            }
        }

        private void DisableProfiles(IEnumerable<Game> games)
        {
            var list = games?.ToList() ?? new List<Game>();
            foreach (var game in list)
            {
                settings.Settings.SetProfile(game, BridgeProfileType.Disabled);
            }
            settings.SaveNow();
            if (list.Count > 0)
            {
                PlayniteApi.Dialogs.ShowMessage(
                    $"ApexSenseBridge désactivé pour {list.Count} jeu(x).",
                    "ApexSenseBridge");
            }
        }

        private void RestoreAutomaticProfiles(IEnumerable<Game> games)
        {
            var list = games?.ToList() ?? new List<Game>();
            foreach (var game in list)
            {
                settings.Settings.RemoveProfile(game.Id);
            }
            settings.SaveNow();
            if (list.Count > 0)
            {
                PlayniteApi.Dialogs.ShowMessage(
                    $"Détection automatique restaurée pour {list.Count} jeu(x).",
                    "ApexSenseBridge");
            }
        }

        private void StopSession(Guid? gameId, string reason)
        {
            BridgeSession session = null;
            lock (sessionLock)
            {
                if (activeSession == null || (gameId.HasValue && activeGameId != gameId.Value))
                {
                    return;
                }
                session = activeSession;
                activeSession = null;
                activeGameId = Guid.Empty;
            }

            logger.Info($"Stopping ApexSenseBridge session: {reason}.");
            if (!session.StopAndWait(TimeSpan.FromSeconds(15)))
            {
                logger.Error("ApexSenseBridge did not stop within 15 seconds after the stop signal.");
            }
            session.Dispose();
        }

        private void CancelStartup(OnGameStartingEventArgs args, string message)
        {
            args.CancelStartup = true;
            logger.Error($"Game startup cancelled by ApexSenseBridge: {message}");
            PlayniteApi.Dialogs.ShowErrorMessage(message, "ApexSenseBridge");
        }

    }
}
