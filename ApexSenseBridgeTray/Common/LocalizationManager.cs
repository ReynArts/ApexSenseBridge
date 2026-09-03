using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows;

namespace ApexSenseBridgeTray.Common
{
    public static class LocalizationManager
    {
        public const string LangEnglish = "en";
        public const string LangFrench = "fr";

        private static string currentLanguage = LangEnglish;

        public static string CurrentLanguage
        {
            get { return currentLanguage; }
        }

        public static event Action LanguageChanged;

        private static readonly Dictionary<string, string> EnglishStrings = new Dictionary<string, string>
        {
            // General & App
            { "Loc_AppName", "ApexSenseBridge" },
            { "Loc_AppSubtitle", "DualSense \u2192 Flydigi APEX 4 / APEX 5 Bridge" },
            { "Loc_AlreadyRunning", "ApexSenseBridge Tray is already running in the system tray." },
            { "Loc_StartupError", "Startup error: " },
            { "Loc_Close", "Close" },
            { "Loc_BtnClose", "Close" },
            { "Loc_Hide", "Hide" },
            { "Loc_Cancel", "Cancel" },
            { "Loc_Warning", "Warning" },

            // Tray & Notifications
            { "Loc_TrayStatusStandby", "ApexSenseBridge: Standby" },
            { "Loc_TrayStatusActive", "ApexSenseBridge: {0}" },
            { "Loc_TrayTooltipStandby", "ApexSenseBridge - Standby" },
            { "Loc_TrayOpen", "Open Interface..." },
            { "Loc_TrayAutoDetect", "Automatic Detection" },
            { "Loc_TrayCheckUpdates", "Check for Updates..." },
            { "Loc_TrayControlPanel", "Control Panel..." },
            { "Loc_TrayLanguage", "Language" },
            { "Loc_TrayExit", "Exit" },
            { "Loc_NotificationActivated", "ApexSenseBridge activated" },
            { "Loc_NotificationGameProfile", "{0}\nProfile: {1}" },
            { "Loc_NotificationGame", "Game" },
            { "Loc_NotificationProfileStandard", "Standard" },
            { "Loc_NotificationWarning", "ApexSenseBridge — Warning" },

            // Status Card
            { "Loc_StatusBadgeStandby", "● Standby" },
            { "Loc_StatusBadgeActive", "● Bridge active" },
            { "Loc_NoActiveGame", "No active game" },
            { "Loc_WaitingHint", "Waiting for a compatible game..." },
            { "Loc_ProfileStandard", "Standard" },
            { "Loc_ProfileRemapping", "Remapping active" },
            { "Loc_PillTriggers", "Triggers" },
            { "Loc_PillHaptics", "Haptics" },
            { "Loc_BtnExcludeCurrent", "Exclude this game" },
            { "Loc_MsgExcluded", "“{0}” has been excluded from automatic detection." },
            { "Loc_ManualBridgeGameTitle", "Forced manual bridge" },

            // Auto Detect Section
            { "Loc_SectionAutoDetect", "Automatic Detection" },
            { "Loc_EnableDetection", "Enable detection" },
            { "Loc_EnableDetectionHint", "Activates the bridge when a compatible game is launched" },
            { "Loc_LaunchCriteria", "Launch the bridge only for:" },
            { "Loc_CriteriaAdaptive", "Games with adaptive triggers" },
            { "Loc_CriteriaHaptic", "Games with haptic feedback" },

            // Configuration Section
            { "Loc_SectionConfig", "Configuration" },
            { "Loc_Notifications", "Notifications" },
            { "Loc_NotificationsHint", "Windows notification when the bridge is activated" },
            { "Loc_ManualBridge", "Force continuous activation" },
            { "Loc_ManualBridgeHint", "Keeps the bridge active continuously without waiting for a game" },
            { "Loc_Language", "Language" },
            { "Loc_LanguageHint", "User interface display language" },
            { "Loc_SoftwareUpdate", "Software update" },
            { "Loc_BtnCheck", "Check" },

            // Update Banner
            { "Loc_UpdateBannerTitle", "Update available!" },
            { "Loc_UpdateBannerSubtitle", "A new version is ready for download" },
            { "Loc_BtnDownload", "Download" },
            { "Loc_UpdateAvailableNotification", "Update available" },
            { "Loc_UpdateAvailableBody", "ApexSenseBridge v{0} is available." },
            { "Loc_UpdateDialogTitle", "ApexSenseBridge — Updates" },
            { "Loc_UpdateDialogAvailableTitle", "ApexSenseBridge — Update available" },
            { "Loc_UpdatePrompt", "A new version of ApexSenseBridge is available!\n\nCurrent version: v{0}\nLatest version: v{1}\n\nWould you like to download it now?" },
            { "Loc_UpdateUpToDate", "ApexSenseBridge is up to date (version v{0})." },
            { "Loc_UpdateError", "Error checking for updates: " },
            { "Loc_UpdateCheckUnavailable", "Unable to check for updates at this time.\nPlease check your Internet connection." },

            // Database Section
            { "Loc_DatabaseTitle", "PCGamingWiki Database" },
            { "Loc_CertifiedGamesSingular", "{0} certified game" },
            { "Loc_CertifiedGamesPlural", "{0} certified games" },
            { "Loc_BtnGameList", "Game list" },
            { "Loc_BtnLearnedExecutables", "Learned" },
            { "Loc_BtnSync", "Sync" },
            { "Loc_Syncing", "Synchronizing..." },
            { "Loc_SyncSuccess", "Update successful!\n{0} {1}." },
            { "Loc_SyncSuccessGamesSingular", "compatible game loaded" },
            { "Loc_SyncSuccessGamesPlural", "compatible games loaded" },
            { "Loc_SyncFailed", "Unable to download the latest list.\nPlease check your Internet connection." },
            { "Loc_LearnedWindowTitle", "Learned executables" },
            { "Loc_LearnedWindowHint", "Validated after 30 seconds of a stable bridge session. Exports never include local paths." },
            { "Loc_LearnedGame", "Game" },
            { "Loc_LearnedExecutable", "Executable" },
            { "Loc_LearnedMethod", "Detection method" },
            { "Loc_LearnedSessions", "Sessions" },
            { "Loc_LearnedLastSeen", "Last seen" },
            { "Loc_LearnedEmpty", "No executable has been learned yet." },
            { "Loc_LearnedCountSingular", "{0} learned executable" },
            { "Loc_LearnedCountPlural", "{0} learned executables" },
            { "Loc_BtnSelectAll", "Select all" },
            { "Loc_BtnDeleteLearned", "Delete" },
            { "Loc_BtnExportLearned", "Export" },
            { "Loc_LearnedDeleteConfirm", "Delete the selected learned associations ({0})?" },
            { "Loc_LearnedExportSuccess", "The selected associations were exported without local paths." },
            { "Loc_LearnedExportFailed", "The export failed: {0}" },

            // Footer
            { "Loc_FooterStatus", "ApexSenseBridge running in background" },
            { "Loc_BtnHide", "Hide" },

            // Game List Window
            { "Loc_GameListTitle", "ApexSenseBridge — Compatible Games" },
            { "Loc_GameListSubtitle", "Browse certified games and manage automatic exclusions" },
            { "Loc_SearchPlaceholder", "Search a game..." },
            { "Loc_TabAll", "All" },
            { "Loc_TabTriggers", "Triggers" },
            { "Loc_TabHaptics", "Haptics" },
            { "Loc_TabExcluded", "Excluded" },
            { "Loc_GamesDisplayedSingular", "{0} game displayed" },
            { "Loc_GamesDisplayedPlural", "{0} games displayed" },
            { "Loc_GamesExcludedSingular", "{0} excluded" },
            { "Loc_GamesExcludedPlural", "{0} excluded" },
            { "Loc_PillAdaptive", "Adaptive triggers" },
            { "Loc_PillHaptic", "Haptic feedback" },
            { "Loc_PillTouchpad", "Touchpad remapping" },
            { "Loc_StateIncluded", "Included" },
            { "Loc_StateExcluded", "Excluded" },
            { "Loc_ExclusionsSavedHint", "Exclusions are automatically saved." },
            { "Loc_NavCertifiedGames", "Certified games" },
            { "NavLearnedExecutables", "Learned executables" },
            { "Loc_NavLearnedExecutables", "Learned executables" },
            { "Loc_LearnedSubtitle", "Executables associated with compatible games on this PC" },
            { "Loc_LearnedSearchPlaceholder", "Search executable or game..." },
            { "Loc_LearnedEmptySubtitle", "When a game runs with a stable session, it will appear here for instant startup." },
            { "Loc_BtnDeleteSingle", "Delete" }
        };

        private static readonly Dictionary<string, string> FrenchStrings = new Dictionary<string, string>
        {
            // General & App
            { "Loc_AppName", "ApexSenseBridge" },
            { "Loc_AppSubtitle", "Pont DualSense \u2192 Flydigi APEX 4 / APEX 5" },
            { "Loc_AlreadyRunning", "ApexSenseBridge Tray est déjà en cours d'exécution dans la barre des tâches." },
            { "Loc_StartupError", "Erreur de démarrage : " },
            { "Loc_Close", "Fermer" },
            { "Loc_BtnClose", "Fermer" },
            { "Loc_Hide", "Masquer" },
            { "Loc_Cancel", "Annuler" },
            { "Loc_Warning", "Avertissement" },

            // Tray & Notifications
            { "Loc_TrayStatusStandby", "ApexSenseBridge : En veille" },
            { "Loc_TrayStatusActive", "ApexSenseBridge : {0}" },
            { "Loc_TrayTooltipStandby", "ApexSenseBridge - En veille" },
            { "Loc_TrayOpen", "Ouvrir l'interface..." },
            { "Loc_TrayAutoDetect", "Détection automatique" },
            { "Loc_TrayCheckUpdates", "Rechercher les mises à jour..." },
            { "Loc_TrayControlPanel", "Panneau de Contrôle..." },
            { "Loc_TrayLanguage", "Langue" },
            { "Loc_TrayExit", "Quitter" },
            { "Loc_NotificationActivated", "ApexSenseBridge activé" },
            { "Loc_NotificationGameProfile", "{0}\nProfil : {1}" },
            { "Loc_NotificationGame", "Jeu" },
            { "Loc_NotificationProfileStandard", "Standard" },
            { "Loc_NotificationWarning", "ApexSenseBridge — Avertissement" },

            // Status Card
            { "Loc_StatusBadgeStandby", "● En veille" },
            { "Loc_StatusBadgeActive", "● Pont actif" },
            { "Loc_NoActiveGame", "Aucun jeu actif" },
            { "Loc_WaitingHint", "En attente d'un jeu compatible..." },
            { "Loc_ProfileStandard", "Standard" },
            { "Loc_ProfileRemapping", "Remapping actif" },
            { "Loc_PillTriggers", "Gâchettes" },
            { "Loc_PillHaptics", "Haptique" },
            { "Loc_BtnExcludeCurrent", "Exclure ce jeu" },
            { "Loc_MsgExcluded", "« {0} » a été exclu de la détection automatique." },
            { "Loc_ManualBridgeGameTitle", "Pont manuel forcé" },

            // Auto Detect Section
            { "Loc_SectionAutoDetect", "Détection automatique" },
            { "Loc_EnableDetection", "Activer la détection" },
            { "Loc_EnableDetectionHint", "Active le pont quand un jeu compatible est lancé" },
            { "Loc_LaunchCriteria", "Lancer le pont uniquement pour :" },
            { "Loc_CriteriaAdaptive", "Jeux avec gâchettes adaptatives" },
            { "Loc_CriteriaHaptic", "Jeux avec retour haptique" },

            // Configuration Section
            { "Loc_SectionConfig", "Configuration" },
            { "Loc_Notifications", "Notifications" },
            { "Loc_NotificationsHint", "Alerte Windows lors de l'activation du pont" },
            { "Loc_ManualBridge", "Forcer l'activation permanente" },
            { "Loc_ManualBridgeHint", "Active le pont en continu sans attendre la détection d'un jeu" },
            { "Loc_Language", "Langue" },
            { "Loc_LanguageHint", "Langue d'affichage de l'interface" },
            { "Loc_SoftwareUpdate", "Mise à jour du logiciel" },
            { "Loc_BtnCheck", "Vérifier" },

            // Update Banner
            { "Loc_UpdateBannerTitle", "Mise à jour disponible !" },
            { "Loc_UpdateBannerSubtitle", "Une nouvelle version est prête au téléchargement" },
            { "Loc_BtnDownload", "Télécharger" },
            { "Loc_UpdateAvailableNotification", "Mise à jour disponible" },
            { "Loc_UpdateAvailableBody", "ApexSenseBridge v{0} est disponible." },
            { "Loc_UpdateDialogTitle", "ApexSenseBridge — Mises à jour" },
            { "Loc_UpdateDialogAvailableTitle", "ApexSenseBridge — Mise à jour disponible" },
            { "Loc_UpdatePrompt", "Une nouvelle version d'ApexSenseBridge est disponible !\n\nVersion actuelle : v{0}\nDernière version : v{1}\n\nSouhaitez-vous la télécharger maintenant ?" },
            { "Loc_UpdateUpToDate", "ApexSenseBridge est à jour (version v{0})." },
            { "Loc_UpdateError", "Erreur lors de la vérification : " },
            { "Loc_UpdateCheckUnavailable", "Impossible de vérifier les mises à jour pour le moment.\nVérifiez votre connexion Internet." },

            // Database Section
            { "Loc_DatabaseTitle", "Base de données PCGamingWiki" },
            { "Loc_CertifiedGamesSingular", "{0} jeu certifié" },
            { "Loc_CertifiedGamesPlural", "{0} jeux certifiés" },
            { "Loc_BtnGameList", "Liste des jeux" },
            { "Loc_BtnLearnedExecutables", "Appris" },
            { "Loc_BtnSync", "Synchroniser" },
            { "Loc_Syncing", "Synchronisation..." },
            { "Loc_SyncSuccess", "Mise à jour réussie !\n{0} {1}." },
            { "Loc_SyncSuccessGamesSingular", "jeu compatible chargé" },
            { "Loc_SyncSuccessGamesPlural", "jeux compatibles chargés" },
            { "Loc_SyncFailed", "Impossible de télécharger la dernière liste.\nVérifiez votre connexion Internet." },
            { "Loc_LearnedWindowTitle", "Exécutables appris" },
            { "Loc_LearnedWindowHint", "Validés après 30 secondes de session stable. Les exports ne contiennent jamais les chemins locaux." },
            { "Loc_LearnedGame", "Jeu" },
            { "Loc_LearnedExecutable", "Exécutable" },
            { "Loc_LearnedMethod", "Méthode de détection" },
            { "Loc_LearnedSessions", "Sessions" },
            { "Loc_LearnedLastSeen", "Dernière utilisation" },
            { "Loc_LearnedEmpty", "Aucun exécutable n'a encore été appris." },
            { "Loc_LearnedCountSingular", "{0} exécutable appris" },
            { "Loc_LearnedCountPlural", "{0} exécutables appris" },
            { "Loc_BtnSelectAll", "Tout sélectionner" },
            { "Loc_BtnDeleteLearned", "Supprimer" },
            { "Loc_BtnExportLearned", "Exporter" },
            { "Loc_LearnedDeleteConfirm", "Supprimer les associations apprises sélectionnées ({0}) ?" },
            { "Loc_LearnedExportSuccess", "Les associations sélectionnées ont été exportées sans chemins locaux." },
            { "Loc_LearnedExportFailed", "L'export a échoué : {0}" },

            // Footer
            { "Loc_FooterStatus", "ApexSenseBridge actif en arrière-plan" },
            { "Loc_BtnHide", "Masquer" },

            // Game List Window
            { "Loc_GameListTitle", "ApexSenseBridge — Jeux compatibles" },
            { "Loc_GameListSubtitle", "Consultez les jeux certifiés et gérez les exclusions automatiques" },
            { "Loc_SearchPlaceholder", "Rechercher un jeu..." },
            { "Loc_TabAll", "Tous" },
            { "Loc_TabTriggers", "Gâchettes" },
            { "Loc_TabHaptics", "Haptique" },
            { "Loc_TabExcluded", "Exclus" },
            { "Loc_GamesDisplayedSingular", "{0} jeu affiché" },
            { "Loc_GamesDisplayedPlural", "{0} jeux affichés" },
            { "Loc_GamesExcludedSingular", "{0} exclu" },
            { "Loc_GamesExcludedPlural", "{0} exclus" },
            { "Loc_PillAdaptive", "Gâchettes adaptatives" },
            { "Loc_PillHaptic", "Retour haptique" },
            { "Loc_PillTouchpad", "Remapping pavé tactile" },
            { "Loc_StateIncluded", "Inclus" },
            { "Loc_StateExcluded", "Exclu" },
            { "Loc_ExclusionsSavedHint", "Les exclusions sont automatiquement sauvegardées." },
            { "Loc_NavCertifiedGames", "Jeux certifiés" },
            { "Loc_NavLearnedExecutables", "Exécutables appris" },
            { "Loc_LearnedSubtitle", "Exécutables associés aux jeux compatibles sur ce PC" },
            { "Loc_LearnedSearchPlaceholder", "Rechercher un exécutable ou un jeu..." },
            { "Loc_LearnedEmptySubtitle", "Lorsqu'un jeu tourne en session stable, son association apparaîtra ici." },
            { "Loc_BtnDeleteSingle", "Supprimer" }
        };

        public static void Initialize(string preferredLanguage)
        {
            string lang = preferredLanguage;
            if (string.IsNullOrWhiteSpace(lang) || string.Equals(lang, "auto", StringComparison.OrdinalIgnoreCase))
            {
                lang = DetectSystemLanguage();
            }
            SetLanguage(lang);
        }

        public static string DetectSystemLanguage()
        {
            try
            {
                var culture = CultureInfo.CurrentUICulture;
                if (culture.TwoLetterISOLanguageName.Equals("fr", StringComparison.OrdinalIgnoreCase))
                {
                    return LangFrench;
                }
            }
            catch { }
            return LangEnglish;
        }

        public static void SetLanguage(string lang)
        {
            if (string.Equals(lang, LangFrench, StringComparison.OrdinalIgnoreCase))
            {
                currentLanguage = LangFrench;
            }
            else
            {
                currentLanguage = LangEnglish;
            }

            ApplyResources();

            var handler = LanguageChanged;
            if (handler != null)
            {
                handler();
            }
        }

        public static string Get(string key)
        {
            var dict = currentLanguage == LangFrench ? FrenchStrings : EnglishStrings;
            string value;
            if (dict.TryGetValue(key, out value))
            {
                return value;
            }
            if (EnglishStrings.TryGetValue(key, out value))
            {
                return value;
            }
            return key;
        }

        public static string Format(string key, params object[] args)
        {
            string template = Get(key);
            try
            {
                return string.Format(template, args);
            }
            catch
            {
                return template;
            }
        }

        private static void ApplyResources()
        {
            if (Application.Current == null) return;
            var res = Application.Current.Resources;
            if (res == null) return;

            var dict = currentLanguage == LangFrench ? FrenchStrings : EnglishStrings;
            foreach (var kvp in dict)
            {
                res[kvp.Key] = kvp.Value;
            }
        }
    }
}
