using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace ApexSenseBridgeTray
{
    public partial class MainWindow : Window
    {
        private readonly CloudGameListService gameListService;
        private readonly EngineSessionManager sessionManager;
        private readonly ProcessMonitorService monitorService;
        private readonly TraySettings settings;
        private bool isInitialized;

        public MainWindow(
            CloudGameListService gameListService,
            EngineSessionManager sessionManager,
            ProcessMonitorService monitorService,
            TraySettings settings)
        {
            this.gameListService = gameListService;
            this.sessionManager = sessionManager;
            this.monitorService = monitorService;
            this.settings = settings;

            InitializeComponent();

            ChkAutoDetect.IsChecked = settings.AutoDetectGames;
            ChkTriggerAdaptive.IsChecked = settings.TriggerOnAdaptiveTriggers;
            ChkTriggerHaptic.IsChecked = settings.TriggerOnHapticFeedback;
            PnlTriggerCriteria.IsEnabled = settings.AutoDetectGames;
            PnlTriggerCriteria.Opacity = settings.AutoDetectGames ? 1.0 : 0.4;
            ChkNotifications.IsChecked = settings.EnableNotifications;
            ChkManualBridge.IsChecked = settings.ForcedProfile == "standard";

            UpdateDatabaseCount();
            UpdateSessionStatus();

            isInitialized = true;

            sessionManager.SessionStarted += (game, profile) => Dispatcher.Invoke(new Action(UpdateSessionStatus));
            sessionManager.SessionStopped += (reason) => Dispatcher.Invoke(new Action(UpdateSessionStatus));
            sessionManager.SessionError += (err) => Dispatcher.Invoke(new Action(() =>
            {
                UpdateSessionStatus();
                MessageBox.Show(this, err, "ApexSenseBridge — Erreur", MessageBoxButton.OK, MessageBoxImage.Warning);
            }));
            gameListService.GamesUpdated += () => Dispatcher.Invoke(new Action(UpdateDatabaseCount));
            ThemeManager.ThemeChanged += () => Dispatcher.Invoke(new Action(UpdateSessionStatus));
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left)
            {
                DragMove();
            }
        }

        protected override void OnClosing(CancelEventArgs e)
        {
            e.Cancel = true;
            Hide();
        }

        public void UpdateSessionStatus()
        {
            if (sessionManager.IsSessionActive)
            {
                BadgeStatus.SetResourceReference(Border.BackgroundProperty, "BadgeActiveBg");
                TxtStatusBadge.SetResourceReference(TextBlock.ForegroundProperty, "BadgeActiveFg");
                TxtStatusBadge.Text = "● Pont actif";

                TxtActiveGame.Text = sessionManager.ActiveGameTitle;
                TxtActiveGame.FontSize = 16;

                string prof = sessionManager.ActiveProfile;
                if (string.Equals(prof, "standard", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(prof, "none", StringComparison.OrdinalIgnoreCase) ||
                    string.IsNullOrWhiteSpace(prof))
                {
                    TxtActiveProfile.Text = "Standard";
                }
                else
                {
                    TxtActiveProfile.Text = "Remapping actif";
                }

                TxtWaitHint.Visibility = Visibility.Collapsed;

                PillTriggers.Opacity = 1.0;
                PillHaptics.Opacity = 1.0;
            }
            else
            {
                BadgeStatus.SetResourceReference(Border.BackgroundProperty, "BadgeStandbyBg");
                TxtStatusBadge.SetResourceReference(TextBlock.ForegroundProperty, "BadgeStandbyFg");
                TxtStatusBadge.Text = "● En veille";

                TxtActiveGame.Text = "Aucun jeu actif";
                TxtActiveGame.FontSize = 15;
                TxtActiveProfile.Text = "Standard";
                TxtWaitHint.Visibility = Visibility.Visible;

                PillTriggers.Opacity = 0.4;
                PillHaptics.Opacity = 0.4;
            }
        }

        public void UpdateDatabaseCount()
        {
            int count = gameListService.TotalGamesLoaded;
            TxtDatabaseInfo.Text = string.Format("{0} {1} certifié{2}", count, count > 1 ? "jeux" : "jeu", count > 1 ? "s" : "");
        }

        private void OnAutoDetectChanged(object sender, RoutedEventArgs e)
        {
            if (!isInitialized) return;
            settings.AutoDetectGames = ChkAutoDetect.IsChecked == true;
            PnlTriggerCriteria.IsEnabled = settings.AutoDetectGames;
            PnlTriggerCriteria.Opacity = settings.AutoDetectGames ? 1.0 : 0.4;
            settings.Save();

            if (settings.AutoDetectGames)
            {
                monitorService.ForceCheck();
            }
            else if (sessionManager.IsSessionActive && settings.ForcedProfile == "none")
            {
                sessionManager.StopSession("Auto-detect disabled by user");
            }
        }

        private void OnTriggerCriteriaChanged(object sender, RoutedEventArgs e)
        {
            if (!isInitialized) return;
            settings.TriggerOnAdaptiveTriggers = ChkTriggerAdaptive.IsChecked == true;
            settings.TriggerOnHapticFeedback = ChkTriggerHaptic.IsChecked == true;
            settings.Save();
            if (settings.AutoDetectGames)
            {
                monitorService.ForceCheck();
            }
        }

        private void OnNotificationsChanged(object sender, RoutedEventArgs e)
        {
            if (!isInitialized) return;
            settings.EnableNotifications = ChkNotifications.IsChecked == true;
            settings.Save();
        }

        private void OnManualBridgeChanged(object sender, RoutedEventArgs e)
        {
            if (!isInitialized) return;
            bool isManual = ChkManualBridge.IsChecked == true;
            settings.ForcedProfile = isManual ? "standard" : "none";
            settings.Save();

            if (isManual)
            {
                sessionManager.StopSession("Switching to manual bridge mode");
                string error;
                sessionManager.StartSession("Pont manuel forcé", "standard", settings, out error);
            }
            else
            {
                if (sessionManager.IsSessionActive)
                {
                    sessionManager.StopSession("Exited manual bridge mode");
                }
                monitorService.ForceCheck();
            }
        }

        private async void OnUpdateDatabaseClick(object sender, RoutedEventArgs e)
        {
            TxtDatabaseInfo.Text = "Synchronisation...";
            var success = await gameListService.FetchLatestFromCloudAsync();
            if (success)
            {
                int count = gameListService.TotalGamesLoaded;
                string gameWord = count > 1 ? "jeux compatibles chargés" : "jeu compatible chargé";
                MessageBox.Show(this, string.Format("Mise à jour réussie !\n{0} {1}.", count, gameWord),
                                "ApexSenseBridge", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            else
            {
                UpdateDatabaseCount();
                MessageBox.Show(this, "Impossible de télécharger la dernière liste.\nVérifiez votre connexion Internet.",
                                "ApexSenseBridge", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        private void OnOpenGameListClick(object sender, RoutedEventArgs e)
        {
            var win = new GameListWindow(gameListService, settings);
            win.Owner = this;
            win.ShowDialog();
        }

        private void OnHideWindowClick(object sender, RoutedEventArgs e)
        {
            Hide();
        }
    }
}
