using Playnite.SDK;
using Playnite.SDK.Data;
using Playnite.SDK.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace ApexSenseBridge
{
    public enum BridgeProfileType
    {
        StandardDualSense,
        SpiderMan2,
        MilesMorales,
        GhostOfTsushima,
        Warframe,
        Disabled
    }

    public class GameBridgeProfile : ObservableObject
    {
        private Guid gameId;
        private string gameName = string.Empty;
        private BridgeProfileType profileType;

        public Guid GameId { get => gameId; set => SetValue(ref gameId, value); }
        public string GameName { get => gameName; set => SetValue(ref gameName, value); }
        public BridgeProfileType ProfileType
        {
            get => profileType;
            set
            {
                SetValue(ref profileType, value);
                OnPropertyChanged(nameof(ProfileTypeDisplayName));
            }
        }

        [DontSerialize]
        public string ProfileTypeDisplayName => GetProfileDisplayName(ProfileType);

        public static string GetProfileDisplayName(BridgeProfileType type)
        {
            switch (type)
            {
                case BridgeProfileType.StandardDualSense:
                    return "DualSense standard";
                case BridgeProfileType.SpiderMan2:
                    return "Spider-Man 2";
                case BridgeProfileType.MilesMorales:
                    return "Spider-Man: Miles Morales";
                case BridgeProfileType.GhostOfTsushima:
                    return "Ghost of Tsushima";
                case BridgeProfileType.Warframe:
                    return "Warframe (layout Xbox par défaut)";
                case BridgeProfileType.Disabled:
                    return "Désactivé (override manuel)";
                default:
                    return type.ToString();
            }
        }
    }

    public class ApexSenseBridgeSettings : ObservableObject
    {
        private string bridgeExecutablePath = string.Empty;
        private bool enableRumble = true;
        private int hapticThresholdPercent = 12;
        private int initializationTimeoutSeconds = 20;
        private string xinputIndex = string.Empty;
        private bool autoCheckUpdates = true;
        private bool enableAutomaticProfiles = true;
        private DateTime? lastUpdateCheckUtc;
        private List<GameBridgeProfile> profiles = new List<GameBridgeProfile>();

        // Kept solely so Playnite can deserialize settings created by older
        // versions. Normal sessions discover the machine installation.
        public string BridgeExecutablePath { get => bridgeExecutablePath; set => SetValue(ref bridgeExecutablePath, value); }
        public bool EnableRumble { get => enableRumble; set => SetValue(ref enableRumble, value); }
        public int HapticThresholdPercent { get => hapticThresholdPercent; set => SetValue(ref hapticThresholdPercent, value); }
        public int InitializationTimeoutSeconds { get => initializationTimeoutSeconds; set => SetValue(ref initializationTimeoutSeconds, value); }
        public string XInputIndex { get => xinputIndex; set => SetValue(ref xinputIndex, value); }
        public bool AutoCheckUpdates { get => autoCheckUpdates; set => SetValue(ref autoCheckUpdates, value); }
        public bool EnableAutomaticProfiles { get => enableAutomaticProfiles; set => SetValue(ref enableAutomaticProfiles, value); }
        public DateTime? LastUpdateCheckUtc { get => lastUpdateCheckUtc; set => SetValue(ref lastUpdateCheckUtc, value); }
        public List<GameBridgeProfile> Profiles
        {
            get => profiles;
            set => SetValue(ref profiles, value ?? new List<GameBridgeProfile>());
        }

        public GameBridgeProfile FindProfile(Guid gameId)
        {
            return Profiles.FirstOrDefault(profile => profile.GameId == gameId);
        }

        public GameBridgeProfile ResolveProfile(Game game, out bool automatic, out string reason)
        {
            automatic = false;
            reason = null;
            var configured = FindProfile(game.Id);
            if (configured != null)
            {
                return configured.ProfileType == BridgeProfileType.Disabled ? null : configured;
            }
            if (!EnableAutomaticProfiles ||
                !AutomaticProfileDetector.TryDetect(game, out var detectedType, out reason))
            {
                return null;
            }

            automatic = true;
            return new GameBridgeProfile
            {
                GameId = game.Id,
                GameName = game.Name,
                ProfileType = detectedType
            };
        }

        public void SetProfile(Game game, BridgeProfileType profileType)
        {
            var profile = FindProfile(game.Id);
            if (profile == null)
            {
                profile = new GameBridgeProfile { GameId = game.Id };
                Profiles.Add(profile);
            }
            profile.GameName = game.Name;
            profile.ProfileType = profileType;
            OnPropertyChanged(nameof(Profiles));
        }

        public void RemoveProfile(Guid gameId)
        {
            Profiles.RemoveAll(profile => profile.GameId == gameId);
            OnPropertyChanged(nameof(Profiles));
        }
    }

    public class ApexSenseBridgeSettingsViewModel : ObservableObject, ISettings
    {
        private static readonly ILogger logger = LogManager.GetLogger();
        private readonly ApexSenseBridge plugin;
        private ApexSenseBridgeSettings editingClone;
        private ApexSenseBridgeSettings settings;

        private bool isCheckingForUpdate;
        private string updateCheckStatus = string.Empty;
        private bool isUpdateAvailable;
        private string availableUpdateVersion = string.Empty;
        private string availableUpdateSetupUrl = string.Empty;
        private bool isDownloadingUpdate;
        private double downloadProgress;

        public ApexSenseBridgeSettings Settings
        {
            get => settings;
            set
            {
                settings = value;
                OnPropertyChanged();
            }
        }

        public string CurrentVersionDisplay => "v" + UpdateManager.GetCurrentVersion().ToString(3);

        public string InstalledExecutablePath
        {
            get
            {
                var path = plugin.ResolveBridgeExecutable(Settings);
                return string.IsNullOrWhiteSpace(path) ? "Non installé" : path;
            }
        }

        public string InstallationStatus =>
            string.IsNullOrWhiteSpace(plugin.ResolveBridgeExecutable(Settings))
                ? "Installation ApexSenseBridge introuvable"
                : "Installation détectée automatiquement";

        public bool IsCheckingForUpdate { get => isCheckingForUpdate; set => SetValue(ref isCheckingForUpdate, value); }
        public string UpdateCheckStatus { get => updateCheckStatus; set => SetValue(ref updateCheckStatus, value); }
        public bool IsUpdateAvailable { get => isUpdateAvailable; set => SetValue(ref isUpdateAvailable, value); }
        public string AvailableUpdateVersion { get => availableUpdateVersion; set => SetValue(ref availableUpdateVersion, value); }
        public string AvailableUpdateSetupUrl { get => availableUpdateSetupUrl; set => SetValue(ref availableUpdateSetupUrl, value); }
        public bool IsDownloadingUpdate { get => isDownloadingUpdate; set => SetValue(ref isDownloadingUpdate, value); }
        public double DownloadProgress { get => downloadProgress; set => SetValue(ref downloadProgress, value); }

        public RelayCommand CheckForUpdatesCommand { get; }
        public RelayCommand DownloadAndInstallUpdateCommand { get; }

        public ApexSenseBridgeSettingsViewModel(ApexSenseBridge plugin)
        {
            this.plugin = plugin;
            Settings = plugin.LoadPluginSettings<ApexSenseBridgeSettings>() ??
                       new ApexSenseBridgeSettings();

            CheckForUpdatesCommand = new RelayCommand(async () => await CheckForUpdatesAsync());
            DownloadAndInstallUpdateCommand = new RelayCommand(async () => await DownloadAndInstallUpdateAsync(), () => IsUpdateAvailable && !IsDownloadingUpdate && !string.IsNullOrWhiteSpace(AvailableUpdateSetupUrl));
        }

        public async Task CheckForUpdatesAsync()
        {
            if (IsCheckingForUpdate) return;

            IsCheckingForUpdate = true;
            UpdateCheckStatus = "Recherche de mise à jour sur GitHub...";

            try
            {
                var result = await UpdateManager.CheckForUpdateAsync();
                Settings.LastUpdateCheckUtc = DateTime.UtcNow;

                if (!string.IsNullOrWhiteSpace(result.ErrorMessage))
                {
                    UpdateCheckStatus = result.ErrorMessage;
                    IsUpdateAvailable = false;
                }
                else if (result.IsUpdateAvailable)
                {
                    IsUpdateAvailable = true;
                    AvailableUpdateVersion = result.TagName;
                    AvailableUpdateSetupUrl = result.SetupDownloadUrl;
                    UpdateCheckStatus = $"Mise à jour disponible : {result.TagName}";
                }
                else
                {
                    IsUpdateAvailable = false;
                    UpdateCheckStatus = $"Vous utilisez la dernière version ({CurrentVersionDisplay}).";
                }
            }
            catch (Exception ex)
            {
                UpdateCheckStatus = $"Erreur : {ex.Message}";
                logger.Error(ex, "Failed manual update check.");
            }
            finally
            {
                IsCheckingForUpdate = false;
            }
        }

        public async Task DownloadAndInstallUpdateAsync()
        {
            if (IsDownloadingUpdate || string.IsNullOrWhiteSpace(AvailableUpdateSetupUrl)) return;

            IsDownloadingUpdate = true;
            DownloadProgress = 0;
            UpdateCheckStatus = "Téléchargement de l'installateur...";

            try
            {
                var progress = new Progress<double>(p =>
                {
                    DownloadProgress = p;
                    UpdateCheckStatus = $"Téléchargement en cours : {p:0}%";
                });

                var installerPath = await UpdateManager.DownloadSetupAsync(AvailableUpdateSetupUrl, AvailableUpdateVersion, progress);
                UpdateCheckStatus = "Lancement de l'installation silencieuse...";

                string error;
                if (UpdateManager.ExecuteInstallerSilently(installerPath, out error))
                {
                    UpdateCheckStatus = "Mise à jour installée avec succès. Redémarrez Playnite pour finaliser.";
                    plugin.PlayniteApi.Dialogs.ShowMessage(
                        "La mise à jour d'ApexSenseBridge a été installée avec succès.\n\nVeuillez redémarrer Playnite pour recharger la nouvelle version de l'extension.",
                        "ApexSenseBridge - Mise à jour");
                }
                else
                {
                    UpdateCheckStatus = error ?? "Échec du lancement de l'installation.";
                }
            }
            catch (Exception ex)
            {
                UpdateCheckStatus = $"Erreur lors du téléchargement/installation : {ex.Message}";
                logger.Error(ex, "Failed to download and install update.");
            }
            finally
            {
                IsDownloadingUpdate = false;
            }
        }

        public void BeginEdit()
        {
            editingClone = Serialization.GetClone(Settings);
            OnPropertyChanged(nameof(InstalledExecutablePath));
            OnPropertyChanged(nameof(InstallationStatus));
            OnPropertyChanged(nameof(CurrentVersionDisplay));
        }

        public void CancelEdit()
        {
            Settings = editingClone;
        }

        public void EndEdit()
        {
            SaveNow();
        }

        public bool VerifySettings(out List<string> errors)
        {
            errors = new List<string>();
            if (string.IsNullOrWhiteSpace(plugin.ResolveBridgeExecutable(Settings)))
            {
                errors.Add("ApexSenseBridge n'est pas installé. Lancez ApexSenseBridge-Setup.exe puis rouvrez Playnite.");
            }
            if (Settings.HapticThresholdPercent < 0 || Settings.HapticThresholdPercent > 95)
            {
                errors.Add("Le seuil haptique doit être compris entre 0 et 95.");
            }
            if (Settings.InitializationTimeoutSeconds < 5 || Settings.InitializationTimeoutSeconds > 60)
            {
                errors.Add("Le délai d'initialisation doit être compris entre 5 et 60 secondes.");
            }
            return errors.Count == 0;
        }

        internal void SaveNow()
        {
            plugin.SaveSettings(Settings);
        }
    }
}
