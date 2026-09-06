using Playnite.SDK;
using Playnite.SDK.Data;
using Playnite.SDK.Models;
using System;
using System.Collections.Generic;
using System.IO;
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

    public enum BridgeActivationMode
    {
        Automatic,
        Enabled,
        Disabled
    }

    public enum TouchpadRemappingMode
    {
        Automatic,
        None,
        SpiderMan2,
        MilesMorales,
        GhostOfTsushima,
        Warframe
    }

    public class GameBridgeProfile : ObservableObject
    {
        private Guid gameId;
        private string gameName = string.Empty;
        private BridgeProfileType profileType;
        private int configurationVersion;
        private BridgeActivationMode activationMode;
        private TouchpadRemappingMode remappingMode;
        private int apexProfileSlot;

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
        public int ConfigurationVersion { get => configurationVersion; set => SetValue(ref configurationVersion, value); }
        public BridgeActivationMode ActivationMode
        {
            get => activationMode;
            set
            {
                SetValue(ref activationMode, value);
                OnPropertyChanged(nameof(ActivationDisplayName));
            }
        }
        public TouchpadRemappingMode RemappingMode
        {
            get => remappingMode;
            set
            {
                SetValue(ref remappingMode, value);
                OnPropertyChanged(nameof(RemappingDisplayName));
            }
        }
        // 0 keeps the currently active onboard profile; 1..4 select a slot.
        public int ApexProfileSlot
        {
            get => apexProfileSlot;
            set
            {
                SetValue(ref apexProfileSlot, value);
                OnPropertyChanged(nameof(ApexProfileDisplayName));
            }
        }

        [DontSerialize]
        public string ProfileTypeDisplayName => GetProfileDisplayName(ProfileType);

        [DontSerialize]
        public string ActivationDisplayName => GetActivationDisplayName(ActivationMode);

        [DontSerialize]
        public string RemappingDisplayName => GetRemappingDisplayName(RemappingMode);

        [DontSerialize]
        public string ApexProfileDisplayName => ApexProfileSlot == 0
            ? "Conserver le profil actuel"
            : "Profil " + ApexProfileSlot;

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

        public static string GetActivationDisplayName(BridgeActivationMode mode)
        {
            switch (mode)
            {
                case BridgeActivationMode.Enabled:
                    return "Activé";
                case BridgeActivationMode.Disabled:
                    return "Désactivé";
                default:
                    return "Automatique";
            }
        }

        public static string GetRemappingDisplayName(TouchpadRemappingMode mode)
        {
            switch (mode)
            {
                case TouchpadRemappingMode.None:
                    return "Aucun";
                case TouchpadRemappingMode.SpiderMan2:
                    return "Spider-Man 2";
                case TouchpadRemappingMode.MilesMorales:
                    return "Spider-Man: Miles Morales";
                case TouchpadRemappingMode.GhostOfTsushima:
                    return "Ghost of Tsushima";
                case TouchpadRemappingMode.Warframe:
                    return "Warframe";
                default:
                    return "Automatique";
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

        // Optional explicit override for portable and custom installations.
        // When empty, the extension uses machine-wide discovery.
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
            var configured = FindProfile(game.Id);
            BridgeProfileType detectedProfile;
            var detected = AutomaticProfileDetector.TryDetect(
                game, out detectedProfile, out reason);
            var activation = configured?.ActivationMode ?? BridgeActivationMode.Automatic;
            automatic = activation == BridgeActivationMode.Automatic;

            var enabled = activation == BridgeActivationMode.Enabled ||
                (activation == BridgeActivationMode.Automatic &&
                 EnableAutomaticProfiles && detected);
            if (!enabled || activation == BridgeActivationMode.Disabled)
            {
                return null;
            }

            var remapping = configured?.RemappingMode ?? TouchpadRemappingMode.Automatic;
            var effectiveProfile = remapping == TouchpadRemappingMode.Automatic
                ? (detected ? detectedProfile : BridgeProfileType.StandardDualSense)
                : ToBridgeProfileType(remapping);
            if (!detected && activation == BridgeActivationMode.Enabled)
            {
                reason = "activation manuelle";
            }

            return new GameBridgeProfile
            {
                GameId = game.Id,
                GameName = game.Name,
                ConfigurationVersion = 2,
                ActivationMode = activation,
                RemappingMode = remapping,
                ApexProfileSlot = configured?.ApexProfileSlot ?? 0,
                ProfileType = effectiveProfile
            };
        }

        public void MigrateLegacyProfiles()
        {
            foreach (var profile in Profiles)
            {
                if (profile.ConfigurationVersion >= 2)
                {
                    profile.ApexProfileSlot = Math.Max(0, Math.Min(4, profile.ApexProfileSlot));
                    continue;
                }

                profile.ActivationMode = profile.ProfileType == BridgeProfileType.Disabled
                    ? BridgeActivationMode.Disabled
                    : BridgeActivationMode.Enabled;
                profile.RemappingMode = FromLegacyProfileType(profile.ProfileType);
                profile.ApexProfileSlot = 0;
                profile.ConfigurationVersion = 2;
            }
        }

        public void SetActivation(Game game, BridgeActivationMode mode)
        {
            var profile = GetOrCreateProfile(game);
            profile.ActivationMode = mode;
            PruneEmptyProfile(profile);
            OnPropertyChanged(nameof(Profiles));
        }

        public void SetRemapping(Game game, TouchpadRemappingMode mode)
        {
            var profile = GetOrCreateProfile(game);
            profile.RemappingMode = mode;
            PruneEmptyProfile(profile);
            OnPropertyChanged(nameof(Profiles));
        }

        public void SetApexProfile(Game game, int slot)
        {
            var profile = GetOrCreateProfile(game);
            profile.ApexProfileSlot = Math.Max(0, Math.Min(4, slot));
            PruneEmptyProfile(profile);
            OnPropertyChanged(nameof(Profiles));
        }

        // Compatibility entry point retained for settings created by 0.6.1.
        public void SetProfile(Game game, BridgeProfileType profileType)
        {
            if (profileType == BridgeProfileType.Disabled)
            {
                SetActivation(game, BridgeActivationMode.Disabled);
                return;
            }

            var profile = GetOrCreateProfile(game);
            profile.ActivationMode = BridgeActivationMode.Enabled;
            profile.RemappingMode = FromLegacyProfileType(profileType);
            profile.ProfileType = profileType;
            OnPropertyChanged(nameof(Profiles));
        }

        public void RemoveProfile(Guid gameId)
        {
            Profiles.RemoveAll(profile => profile.GameId == gameId);
            OnPropertyChanged(nameof(Profiles));
        }

        private GameBridgeProfile GetOrCreateProfile(Game game)
        {
            var profile = FindProfile(game.Id);
            if (profile == null)
            {
                profile = new GameBridgeProfile
                {
                    GameId = game.Id,
                    ConfigurationVersion = 2,
                    ActivationMode = BridgeActivationMode.Automatic,
                    RemappingMode = TouchpadRemappingMode.Automatic
                };
                Profiles.Add(profile);
            }
            profile.GameName = game.Name;
            return profile;
        }

        private void PruneEmptyProfile(GameBridgeProfile profile)
        {
            if (profile.ActivationMode == BridgeActivationMode.Automatic &&
                profile.RemappingMode == TouchpadRemappingMode.Automatic &&
                profile.ApexProfileSlot == 0)
            {
                Profiles.Remove(profile);
            }
        }

        private static TouchpadRemappingMode FromLegacyProfileType(BridgeProfileType type)
        {
            switch (type)
            {
                case BridgeProfileType.SpiderMan2:
                    return TouchpadRemappingMode.SpiderMan2;
                case BridgeProfileType.MilesMorales:
                    return TouchpadRemappingMode.MilesMorales;
                case BridgeProfileType.GhostOfTsushima:
                    return TouchpadRemappingMode.GhostOfTsushima;
                case BridgeProfileType.Warframe:
                    return TouchpadRemappingMode.Warframe;
                default:
                    // Standard already auto-detected special remappings in 0.6.1.
                    return TouchpadRemappingMode.Automatic;
            }
        }

        private static BridgeProfileType ToBridgeProfileType(TouchpadRemappingMode mode)
        {
            switch (mode)
            {
                case TouchpadRemappingMode.SpiderMan2:
                    return BridgeProfileType.SpiderMan2;
                case TouchpadRemappingMode.MilesMorales:
                    return BridgeProfileType.MilesMorales;
                case TouchpadRemappingMode.GhostOfTsushima:
                    return BridgeProfileType.GhostOfTsushima;
                case TouchpadRemappingMode.Warframe:
                    return BridgeProfileType.Warframe;
                default:
                    return BridgeProfileType.StandardDualSense;
            }
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

        public string ConfiguredExecutablePath
        {
            get => Settings?.BridgeExecutablePath ?? string.Empty;
            set
            {
                if (Settings == null || string.Equals(Settings.BridgeExecutablePath, value, StringComparison.Ordinal))
                {
                    return;
                }

                Settings.BridgeExecutablePath = value ?? string.Empty;
                RefreshInstallationStatus();
            }
        }

        public string InstalledExecutablePath
        {
            get
            {
                var path = plugin.ResolveBridgeExecutable(Settings);
                return string.IsNullOrWhiteSpace(path) ? "Non installé" : path;
            }
        }

        public string InstallationStatus
        {
            get
            {
                if (InstallLocator.IsEngine(ConfiguredExecutablePath))
                {
                    return "Exécutable ApexSenseBridge configuré manuellement";
                }

                return string.IsNullOrWhiteSpace(plugin.ResolveBridgeExecutable(Settings))
                    ? "Installation ApexSenseBridge introuvable"
                    : "Installation détectée automatiquement";
            }
        }

        public bool IsCheckingForUpdate { get => isCheckingForUpdate; set => SetValue(ref isCheckingForUpdate, value); }
        public string UpdateCheckStatus { get => updateCheckStatus; set => SetValue(ref updateCheckStatus, value); }
        public bool IsUpdateAvailable { get => isUpdateAvailable; set => SetValue(ref isUpdateAvailable, value); }
        public string AvailableUpdateVersion { get => availableUpdateVersion; set => SetValue(ref availableUpdateVersion, value); }
        public string AvailableUpdateSetupUrl { get => availableUpdateSetupUrl; set => SetValue(ref availableUpdateSetupUrl, value); }
        public bool IsDownloadingUpdate { get => isDownloadingUpdate; set => SetValue(ref isDownloadingUpdate, value); }
        public double DownloadProgress { get => downloadProgress; set => SetValue(ref downloadProgress, value); }

        public RelayCommand CheckForUpdatesCommand { get; }
        public RelayCommand DownloadAndInstallUpdateCommand { get; }
        public RelayCommand BrowseForExecutableCommand { get; }
        public RelayCommand UseAutomaticInstallationCommand { get; }

        public ApexSenseBridgeSettingsViewModel(ApexSenseBridge plugin)
        {
            this.plugin = plugin;
            Settings = plugin.LoadPluginSettings<ApexSenseBridgeSettings>() ??
                       new ApexSenseBridgeSettings();
            Settings.MigrateLegacyProfiles();

            CheckForUpdatesCommand = new RelayCommand(async () => await CheckForUpdatesAsync());
            DownloadAndInstallUpdateCommand = new RelayCommand(async () => await DownloadAndInstallUpdateAsync(), () => IsUpdateAvailable && !IsDownloadingUpdate && !string.IsNullOrWhiteSpace(AvailableUpdateSetupUrl));
            BrowseForExecutableCommand = new RelayCommand(BrowseForExecutable);
            UseAutomaticInstallationCommand = new RelayCommand(() => ConfiguredExecutablePath = string.Empty);
        }

        private void BrowseForExecutable()
        {
            var selectedPath = plugin.PlayniteApi.Dialogs.SelectFile(
                "ApexSenseBridge|ApexSenseBridge.exe|Exécutables Windows|*.exe");
            if (string.IsNullOrWhiteSpace(selectedPath))
            {
                return;
            }

            if (!InstallLocator.IsEngine(selectedPath))
            {
                plugin.PlayniteApi.Dialogs.ShowErrorMessage(
                    "Sélectionnez le fichier ApexSenseBridge.exe du dossier d'installation ou de la version portable.",
                    "ApexSenseBridge");
                return;
            }

            ConfiguredExecutablePath = Path.GetFullPath(selectedPath);
        }

        private void RefreshInstallationStatus()
        {
            OnPropertyChanged(nameof(ConfiguredExecutablePath));
            OnPropertyChanged(nameof(InstalledExecutablePath));
            OnPropertyChanged(nameof(InstallationStatus));
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
            RefreshInstallationStatus();
            OnPropertyChanged(nameof(CurrentVersionDisplay));
        }

        public void CancelEdit()
        {
            Settings = editingClone;
            RefreshInstallationStatus();
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
                errors.Add("ApexSenseBridge est introuvable. Installez-le ou sélectionnez ApexSenseBridge.exe dans les réglages de l'extension.");
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
