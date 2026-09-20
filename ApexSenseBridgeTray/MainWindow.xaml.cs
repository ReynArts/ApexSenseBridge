using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Effects;

namespace ApexSenseBridgeTray
{
    public partial class MainWindow : Window
    {
        private readonly CloudGameListService gameListService;
        private readonly EngineSessionManager sessionManager;
        private readonly ExecutableLearningService learningService;
        private readonly ProcessMonitorService monitorService;
        private readonly UpdateCheckerService updateChecker;
        private readonly TraySettings settings;
        private readonly GamepadNavigationService gamepadNav;
        private bool isInitialized;
        private UpdateInfo latestUpdateInfo;

        // Structured gamepad navigation
        private readonly List<Border> navItems = new List<Border>();
        private int navIndex = -1;
        private bool isGamepadMode;

        public MainWindow(
            CloudGameListService gameListService,
            EngineSessionManager sessionManager,
            ExecutableLearningService learningService,
            ProcessMonitorService monitorService,
            UpdateCheckerService updateChecker,
            TraySettings settings)
        {
            this.gameListService = gameListService;
            this.sessionManager = sessionManager;
            this.learningService = learningService;
            this.monitorService = monitorService;
            this.updateChecker = updateChecker;
            this.settings = settings;

            InitializeComponent();

            gamepadNav = new GamepadNavigationService(this);
            gamepadNav.UpPressed += OnGamepadUp;
            gamepadNav.DownPressed += OnGamepadDown;
            gamepadNav.ActionPressed += OnGamepadAction;
            gamepadNav.ScrollRequested += OnGamepadScroll;
            gamepadNav.InputModeChanged += OnGamepadModeChanged;
            gamepadNav.ConnectionChanged += OnGamepadConnectionChanged;
            UpdateGamepadHudVisibility(gamepadNav.IsGamepadActive);
            UpdateGamepadConnectionVisibility(gamepadNav.IsControllerConnected);

            // Build navigation index
            navItems.Add(NavAutoDetect);
            navItems.Add(NavCriteriaAdaptive);
            navItems.Add(NavCriteriaHaptic);
            navItems.Add(NavNotifications);
            navItems.Add(NavSyncLightbar);
            navItems.Add(NavManualBridge);
            navItems.Add(NavLanguage);
            navItems.Add(NavTestController);
            navItems.Add(NavDatabase);
            navItems.Add(NavUpdate);

            UpdateLanguageRadios();

            ChkAutoDetect.IsChecked = settings.AutoDetectGames;
            ChkTriggerAdaptive.IsChecked = settings.TriggerOnAdaptiveTriggers;
            ChkTriggerHaptic.IsChecked = settings.TriggerOnHapticFeedback;
            UpdateCriteriaState();
            ChkNotifications.IsChecked = settings.EnableNotifications;
            ChkSyncLightbar.IsChecked = settings.SyncLightbar;
            ChkManualBridge.IsChecked = settings.ForcedProfile == "standard";

            if (updateChecker != null)
            {
                TxtVersionInfo.Text = string.Format("ApexSenseBridge v{0}", updateChecker.GetCurrentVersion());
                updateChecker.UpdateAvailable += (info) => Dispatcher.BeginInvoke(new Action(() => ShowUpdateBanner(info)));
            }

            UpdateDatabaseCount();
            UpdateSessionStatus();

            isInitialized = true;

            sessionManager.SessionStarted += (game, profile) => Dispatcher.BeginInvoke(new Action(() =>
            {
                try { UpdateSessionStatus(); } catch { }
            }));
            sessionManager.SessionStopped += (reason) => Dispatcher.BeginInvoke(new Action(() =>
            {
                try { UpdateSessionStatus(); } catch { }
            }));
            sessionManager.SessionError += (err) => Dispatcher.BeginInvoke(new Action(() =>
            {
                try
                {
                    UpdateSessionStatus();
                }
                catch { }
            }));
            gameListService.GamesUpdated += () => Dispatcher.BeginInvoke(new Action(() =>
            {
                try { UpdateDatabaseCount(); } catch { }
            }));
            if (learningService != null)
            {
                learningService.BindingsChanged += () => Dispatcher.BeginInvoke(new Action(() =>
                {
                    try { UpdateDatabaseCount(); } catch { }
                }));
            }
            ThemeManager.ThemeChanged += () => Dispatcher.BeginInvoke(new Action(() =>
            {
                try { UpdateSessionStatus(); } catch { }
            }));
            LocalizationManager.LanguageChanged += () => Dispatcher.BeginInvoke(new Action(() =>
            {
                try
                {
                    UpdateLanguageRadios();
                    UpdateSessionStatus();
                    UpdateDatabaseCount();
                    if (latestUpdateInfo != null)
                    {
                        ShowUpdateBanner(latestUpdateInfo);
                    }
                }
                catch { }
            }));
        }

        private void UpdateCriteriaState()
        {
            bool enabled = settings.AutoDetectGames;
            NavCriteriaAdaptive.IsEnabled = enabled;
            NavCriteriaHaptic.IsEnabled = enabled;
            NavCriteriaAdaptive.Opacity = enabled ? 1.0 : 0.4;
            NavCriteriaHaptic.Opacity = enabled ? 1.0 : 0.4;
        }

        private void UpdateLanguageRadios()
        {
            string lang = LocalizationManager.CurrentLanguage;
            if (RadLangEn != null) RadLangEn.IsChecked = (lang == LocalizationManager.LangEnglish);
            if (RadLangFr != null) RadLangFr.IsChecked = (lang == LocalizationManager.LangFrench);
            if (RadLangEs != null) RadLangEs.IsChecked = (lang == LocalizationManager.LangSpanish);
            if (RadLangZh != null) RadLangZh.IsChecked = (lang == LocalizationManager.LangChinese);
        }

        private void OnLanguageOptionChecked(object sender, RoutedEventArgs e)
        {
            if (!isInitialized) return;
            string newLang = LocalizationManager.LangEnglish;
            if (RadLangFr != null && RadLangFr.IsChecked == true) newLang = LocalizationManager.LangFrench;
            else if (RadLangEs != null && RadLangEs.IsChecked == true) newLang = LocalizationManager.LangSpanish;
            else if (RadLangZh != null && RadLangZh.IsChecked == true) newLang = LocalizationManager.LangChinese;

            if (newLang != LocalizationManager.CurrentLanguage)
            {
                settings.Language = newLang;
                settings.Save();
                LocalizationManager.SetLanguage(newLang);
            }
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
                TxtStatusBadge.Text = LocalizationManager.Get("Loc_StatusBadgeActive");

                TxtActiveGame.Text = sessionManager.ActiveGameTitle;
                TxtActiveGame.FontSize = 16;

                string prof = sessionManager.ActiveProfile;
                if (string.Equals(prof, "standard", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(prof, "none", StringComparison.OrdinalIgnoreCase) ||
                    string.IsNullOrWhiteSpace(prof))
                {
                    TxtActiveProfile.Text = LocalizationManager.Get("Loc_ProfileStandard");
                }
                else
                {
                    TxtActiveProfile.Text = LocalizationManager.Get("Loc_ProfileRemapping");
                }

                PillTriggers.Opacity = 1.0;
                PillHaptics.Opacity = 1.0;

                string manualTitle = LocalizationManager.Get("Loc_ManualBridgeGameTitle");
                bool isForced = sessionManager.ActiveGameTitle == "Pont manuel forcé" ||
                                sessionManager.ActiveGameTitle == "Forced manual bridge" ||
                                sessionManager.ActiveGameTitle == manualTitle;
                BtnExcludeCurrentGame.Visibility = isForced ? Visibility.Collapsed : Visibility.Visible;
            }
            else
            {
                BadgeStatus.SetResourceReference(Border.BackgroundProperty, "BadgeStandbyBg");
                TxtStatusBadge.SetResourceReference(TextBlock.ForegroundProperty, "BadgeStandbyFg");
                TxtStatusBadge.Text = LocalizationManager.Get("Loc_StatusBadgeStandby");

                TxtActiveGame.Text = LocalizationManager.Get("Loc_NoActiveGame");
                TxtActiveGame.FontSize = 15;
                TxtActiveProfile.Text = LocalizationManager.Get("Loc_ProfileStandard");

                PillTriggers.Opacity = 0.4;
                PillHaptics.Opacity = 0.4;

                BtnExcludeCurrentGame.Visibility = Visibility.Collapsed;
            }
        }

        private void OnExcludeCurrentGameClick(object sender, RoutedEventArgs e)
        {
            var gameTitle = sessionManager.ActiveGameTitle;
            if (string.IsNullOrWhiteSpace(gameTitle) || gameTitle == "Aucun" || gameTitle == "None" ||
                gameTitle == "Pont manuel forcé" || gameTitle == "Forced manual bridge") return;

            SupportedGame game;
            if (gameListService.TryFindGame(gameTitle, out game) && game != null)
            {
                settings.SetGameExcluded(game.Normalized, true);
                settings.SetGameExcluded(game.Title, true);
            }
            else
            {
                settings.SetGameExcluded(gameTitle, true);
            }
            settings.Save();

            sessionManager.StopSession("Game excluded by user");
            UpdateSessionStatus();

            MessageBox.Show(this, LocalizationManager.Format("Loc_MsgExcluded", gameTitle),
                            LocalizationManager.Get("Loc_AppName"), MessageBoxButton.OK, MessageBoxImage.Information);
        }

        public void UpdateDatabaseCount()
        {
            int count = gameListService != null ? gameListService.TotalGamesLoaded : 0;
            string key = count > 1 ? "Loc_CertifiedGamesPlural" : "Loc_CertifiedGamesSingular";
            string baseStr = LocalizationManager.Format(key, count);

            int learned = learningService != null ? learningService.Count : 0;
            if (learned > 0)
            {
                string learnedKey = learned > 1 ? "Loc_LearnedCountPlural" : "Loc_LearnedCountSingular";
                TxtDatabaseInfo.Text = string.Format("{0} • {1}", baseStr, LocalizationManager.Format(learnedKey, learned));
            }
            else
            {
                TxtDatabaseInfo.Text = baseStr;
            }
        }

        private void OnAutoDetectChanged(object sender, RoutedEventArgs e)
        {
            if (!isInitialized) return;
            settings.AutoDetectGames = ChkAutoDetect.IsChecked == true;
            UpdateCriteriaState();
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

        private void OnSyncLightbarChanged(object sender, RoutedEventArgs e)
        {
            if (!isInitialized) return;
            settings.SyncLightbar = ChkSyncLightbar.IsChecked == true;
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
                sessionManager.StartSession(LocalizationManager.Get("Loc_ManualBridgeGameTitle"), "standard", settings, out error);
                monitorService.ForceCheck();
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

        // Click handlers for navigable items (mouse click on the entire row)
        private void OnNavItemAutoDetectClick(object sender, MouseButtonEventArgs e)
        {
            ChkAutoDetect.IsChecked = !ChkAutoDetect.IsChecked;
        }

        private void OnNavItemAdaptiveClick(object sender, MouseButtonEventArgs e)
        {
            if (!settings.AutoDetectGames) return;
            ChkTriggerAdaptive.IsChecked = !ChkTriggerAdaptive.IsChecked;
        }

        private void OnNavItemHapticClick(object sender, MouseButtonEventArgs e)
        {
            if (!settings.AutoDetectGames) return;
            ChkTriggerHaptic.IsChecked = !ChkTriggerHaptic.IsChecked;
        }

        private void OnNavItemNotificationsClick(object sender, MouseButtonEventArgs e)
        {
            ChkNotifications.IsChecked = !ChkNotifications.IsChecked;
        }

        private void OnNavItemSyncLightbarClick(object sender, MouseButtonEventArgs e)
        {
            ChkSyncLightbar.IsChecked = !ChkSyncLightbar.IsChecked;
        }

        private void OnNavItemManualBridgeClick(object sender, MouseButtonEventArgs e)
        {
            ChkManualBridge.IsChecked = !ChkManualBridge.IsChecked;
        }

        private async void OnUpdateDatabaseClick(object sender, RoutedEventArgs e)
        {
            TxtDatabaseInfo.Text = LocalizationManager.Get("Loc_Syncing");
            var success = await gameListService.FetchLatestFromCloudAsync();
            if (success)
            {
                int count = gameListService.TotalGamesLoaded;
                string gameWord = count > 1 ? LocalizationManager.Get("Loc_SyncSuccessGamesPlural") : LocalizationManager.Get("Loc_SyncSuccessGamesSingular");
                MessageBox.Show(this, LocalizationManager.Format("Loc_SyncSuccess", count, gameWord),
                                LocalizationManager.Get("Loc_AppName"), MessageBoxButton.OK, MessageBoxImage.Information);
            }
            else
            {
                UpdateDatabaseCount();
                MessageBox.Show(this, LocalizationManager.Get("Loc_SyncFailed"),
                                LocalizationManager.Get("Loc_AppName"), MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        private async void OnCheckUpdatesClick(object sender, RoutedEventArgs e)
        {
            if (updateChecker == null) return;
            UpdateInfo info = await updateChecker.CheckForUpdatesAsync(false);
            ShowUpdateBanner(info);
        }

        private void ShowUpdateBanner(UpdateInfo info)
        {
            latestUpdateInfo = info;
            if (info != null && info.HasUpdate)
            {
                TxtUpdateTitle.Text = LocalizationManager.Format("Loc_UpdateBannerTitle") + string.Format(" (v{0})", info.LatestVersion);
                TxtUpdateSubtitle.Text = LocalizationManager.Get("Loc_UpdateBannerSubtitle");
                BannerUpdate.Visibility = Visibility.Visible;
            }
            else
            {
                BannerUpdate.Visibility = Visibility.Collapsed;
            }
        }

        private void OnDownloadUpdateClick(object sender, RoutedEventArgs e)
        {
            if (updateChecker != null && latestUpdateInfo != null)
            {
                updateChecker.DownloadOrOpenRelease(latestUpdateInfo);
            }
        }

        private void OnOpenGameListClick(object sender, RoutedEventArgs e)
        {
            var win = new GameListWindow(gameListService, settings, learningService, sessionManager, monitorService, updateChecker, initialTab: "games");
            win.Owner = this;
            win.ShowDialog();
            UpdateDatabaseCount();
        }

        private void OnOpenLearnedExecutablesClick(object sender, RoutedEventArgs e)
        {
            var win = new GameListWindow(gameListService, settings, learningService, sessionManager, monitorService, updateChecker, initialTab: "learned");
            win.Owner = this;
            win.ShowDialog();
            UpdateDatabaseCount();
        }

        private void OnOpenControllerTestClick(object sender, RoutedEventArgs e)
        {
            var win = new ControllerTestWindow(settings);
            win.Owner = this;
            win.ShowDialog();
        }

        private void OnHideWindowClick(object sender, RoutedEventArgs e)
        {
            Hide();
        }

        #region Gamepad Navigation (Structured D-Pad)

        private void OnGamepadModeChanged(bool isGamepad)
        {
            isGamepadMode = isGamepad;
            UpdateGamepadHudVisibility(isGamepad);
            if (isGamepad)
            {
                if (navIndex < 0) SetNavFocus(0);
            }
            else ClearNavFocus();
        }

        private void UpdateGamepadHudVisibility(bool isGamepad)
        {
            if (PnlGamepadHudMain != null)
            {
                PnlGamepadHudMain.Opacity = isGamepad ? 1.0 : 0.5;
            }
        }

        private void OnGamepadConnectionChanged(bool isConnected)
        {
            if (Dispatcher.CheckAccess()) UpdateGamepadConnectionVisibility(isConnected);
            else Dispatcher.BeginInvoke(new Action(() => UpdateGamepadConnectionVisibility(isConnected)));
        }

        private void UpdateGamepadConnectionVisibility(bool isConnected)
        {
            if (PnlGamepadHudMain != null)
            {
                PnlGamepadHudMain.Visibility = isConnected ? Visibility.Visible : Visibility.Collapsed;
            }
        }

        private void OnGamepadUp()
        {
            if (navIndex <= 0) return;
            int target = navIndex - 1;
            while (target >= 0 && !navItems[target].IsEnabled)
            {
                target--;
            }
            if (target >= 0) SetNavFocus(target);
        }

        private void OnGamepadDown()
        {
            if (navIndex >= navItems.Count - 1) return;
            int target = navIndex + 1;
            while (target < navItems.Count && !navItems[target].IsEnabled)
            {
                target++;
            }
            if (target < navItems.Count) SetNavFocus(target);
        }

        private void SetNavFocus(int index)
        {
            // Clear previous
            if (navIndex >= 0 && navIndex < navItems.Count)
            {
                var prev = navItems[navIndex];
                prev.BorderBrush = Brushes.Transparent;
                prev.Effect = null;
            }

            navIndex = Math.Max(0, Math.Min(navItems.Count - 1, index));

            // Highlight current
            var current = navItems[navIndex];
            current.BorderBrush = (Brush)FindResource("GamepadFocusBorder");
            current.Effect = new DropShadowEffect
            {
                BlurRadius = 14,
                ShadowDepth = 0,
                Direction = 0,
                Color = Color.FromRgb(0x00, 0x70, 0xD1),
                Opacity = 0.7
            };

            // Scroll into view
            if (ScrollMain != null)
            {
                var transform = current.TransformToAncestor(ScrollMain);
                var position = transform.Transform(new Point(0, 0));
                double itemTop = position.Y + ScrollMain.VerticalOffset;
                double itemBottom = itemTop + current.ActualHeight;
                double viewTop = ScrollMain.VerticalOffset;
                double viewBottom = viewTop + ScrollMain.ActualHeight;

                if (itemTop < viewTop)
                {
                    ScrollMain.ScrollToVerticalOffset(itemTop - 8);
                }
                else if (itemBottom > viewBottom)
                {
                    ScrollMain.ScrollToVerticalOffset(itemBottom - ScrollMain.ActualHeight + 8);
                }
            }
        }

        private void ClearNavFocus()
        {
            if (navIndex >= 0 && navIndex < navItems.Count)
            {
                var item = navItems[navIndex];
                item.BorderBrush = Brushes.Transparent;
                item.Effect = null;
            }
            navIndex = -1;
        }

        private void OnGamepadAction(GamepadButtonAction action)
        {
            switch (action)
            {
                case GamepadButtonAction.Back:
                    Hide();
                    break;
                case GamepadButtonAction.ActionY:
                    OnOpenGameListClick(null, null);
                    break;
                case GamepadButtonAction.ActionX:
                    if (BtnExcludeCurrentGame != null && BtnExcludeCurrentGame.Visibility == Visibility.Visible)
                        OnExcludeCurrentGameClick(null, null);
                    break;
                case GamepadButtonAction.Select:
                    ActivateCurrentNavItem();
                    break;
            }
        }

        private void ActivateCurrentNavItem()
        {
            if (navIndex < 0 || navIndex >= navItems.Count) return;
            var item = navItems[navIndex];

            if (item == NavAutoDetect)
            {
                ChkAutoDetect.IsChecked = !ChkAutoDetect.IsChecked;
            }
            else if (item == NavCriteriaAdaptive)
            {
                if (settings.AutoDetectGames)
                    ChkTriggerAdaptive.IsChecked = !ChkTriggerAdaptive.IsChecked;
            }
            else if (item == NavCriteriaHaptic)
            {
                if (settings.AutoDetectGames)
                    ChkTriggerHaptic.IsChecked = !ChkTriggerHaptic.IsChecked;
            }
            else if (item == NavNotifications)
            {
                ChkNotifications.IsChecked = !ChkNotifications.IsChecked;
            }
            else if (item == NavSyncLightbar)
            {
                ChkSyncLightbar.IsChecked = !ChkSyncLightbar.IsChecked;
            }
            else if (item == NavManualBridge)
            {
                ChkManualBridge.IsChecked = !ChkManualBridge.IsChecked;
            }
            else if (item == NavLanguage)
            {
                // Toggle between the two languages
                if (RadLangFr.IsChecked == true)
                    RadLangEn.IsChecked = true;
                else
                    RadLangFr.IsChecked = true;
            }
            else if (item == NavDatabase)
            {
                OnOpenGameListClick(null, null);
            }
            else if (item == NavUpdate)
            {
                OnCheckUpdatesClick(null, null);
            }
        }

        private void OnGamepadScroll(double deltaY)
        {
            if (ScrollMain != null)
            {
                ScrollMain.ScrollToVerticalOffset(ScrollMain.VerticalOffset + deltaY);
            }
        }

        protected override void OnClosed(EventArgs e)
        {
            if (gamepadNav != null)
            {
                gamepadNav.Dispose();
            }
            base.OnClosed(e);
        }

        #endregion
    }
}
