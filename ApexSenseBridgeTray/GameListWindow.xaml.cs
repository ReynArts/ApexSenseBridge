using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using ApexSenseBridge.Common;
using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;

namespace ApexSenseBridgeTray
{
    public partial class GameListWindow : Window
    {
        private readonly CloudGameListService gameListService;
        private readonly TraySettings settings;
        private readonly ExecutableLearningService learningService;
        private readonly EngineSessionManager sessionManager;
        private readonly ProcessMonitorService monitorService;
        private readonly UpdateCheckerService updateChecker;
        private readonly ControllerDetectionService controllerDetection;

        private readonly List<GameItemViewModel> allGameViewModels = new List<GameItemViewModel>();
        private readonly ObservableCollection<GameItemViewModel> filteredGames = new ObservableCollection<GameItemViewModel>();
        private readonly List<LearnedItemViewModel> allLearnedViewModels = new List<LearnedItemViewModel>();
        private readonly ObservableCollection<LearnedItemViewModel> filteredLearned = new ObservableCollection<LearnedItemViewModel>();
        private readonly ObservableCollection<GameItemViewModel> dashboardFeaturedGames = new ObservableCollection<GameItemViewModel>();

        private readonly GamepadNavigationService gamepadNav;
        private int currentTabIndex = 0;
        private int selectedGameIndex = -1;
        private bool updatingEffectSettings;
        private bool updatingExecutables;
        private bool preparedLaunchPending;
        private bool viewInitialized;
        private readonly System.Windows.Threading.DispatcherTimer statusTimer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };

        private sealed class ExecutableChoice
        {
            public string Path { get; set; }
            public string Label { get; set; }
        }

        public GameListWindow(
            CloudGameListService gameListService,
            TraySettings settings,
            ExecutableLearningService learningService = null,
            EngineSessionManager sessionManager = null,
            ProcessMonitorService monitorService = null,
            UpdateCheckerService updateChecker = null,
            string initialTab = "dashboard")
        {
            this.gameListService = gameListService;
            this.settings = settings;
            this.learningService = learningService;
            this.sessionManager = sessionManager;
            this.monitorService = monitorService;
            this.updateChecker = updateChecker;

            InitializeComponent();
            viewInitialized = true;

            LstGames.ItemsSource = filteredGames;
            LstLearned.ItemsSource = filteredLearned;
            if (LstDashboardFeatured != null) LstDashboardFeatured.ItemsSource = dashboardFeaturedGames;

            gamepadNav = new GamepadNavigationService(this);
            gamepadNav.TabCycleRequested += OnGamepadTabCycle;
            gamepadNav.UpPressed += OnGamepadUp;
            gamepadNav.DownPressed += OnGamepadDown;
            gamepadNav.LeftPressed += OnGamepadLeft;
            gamepadNav.RightPressed += OnGamepadRight;
            gamepadNav.ActionPressed += OnGamepadAction;
            gamepadNav.ScrollRequested += OnGamepadScroll;
            gamepadNav.InputModeChanged += OnGamepadModeChanged;
            gamepadNav.ConnectionChanged += OnGamepadConnectionChanged;
            gamepadNav.DirectionNavigated += dir => { };
            PreviewKeyDown += OnWindowPreviewKeyDown;
            Loaded += (sender, args) => ScheduleSupportHint();

            UpdateGamepadHudVisibility(gamepadNav.IsGamepadActive);
            UpdateGamepadConnectionVisibility(gamepadNav.IsControllerConnected);
            Loaded += (sender, args) => UpdateGamepadConnectionVisibility(gamepadNav.IsControllerConnected);

            if (learningService != null)
            {
                learningService.StateChanged += () => Dispatcher.BeginInvoke(new Action(() => { LoadLearnedItems(); UpdateExecutableChoices(); }));
            }

            if (sessionManager != null)
            {
                sessionManager.SessionStarted += (game, profile) => Dispatcher.BeginInvoke(new Action(UpdateDashboardStatus));
                sessionManager.SessionStopped += reason => Dispatcher.BeginInvoke(new Action(UpdateDashboardStatus));
                sessionManager.SessionError += err => Dispatcher.BeginInvoke(new Action(UpdateDashboardStatus));
            }

            if (gameListService != null)
            {
                gameListService.GamesUpdated += () => Dispatcher.BeginInvoke(new Action(LoadGames));
            }

            LoadGames();
            LoadLearnedItems();
            UpdateDashboardStatus();
            UpdateSettingsView();

            controllerDetection = new ControllerDetectionService();
            controllerDetection.StatusChanged += status => Dispatcher.BeginInvoke(
                new Action(() => UpdateControllerStatus(status)));
            UpdateControllerStatus("disconnected");
            statusTimer.Tick += (s, e) => { UpdateDashboardStatus(); UpdateClock(); };
            UpdateClock();
            statusTimer.Start();

            if (string.Equals(initialTab, "games", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(initialTab, "certified", StringComparison.OrdinalIgnoreCase))
            {
                SetCurrentTab(1);
            }
            else if (string.Equals(initialTab, "learned", StringComparison.OrdinalIgnoreCase))
            {
                SetCurrentTab(2);
            }
            else if (string.Equals(initialTab, "settings", StringComparison.OrdinalIgnoreCase))
            {
                SetCurrentTab(3);
            }
            else
            {
                SetCurrentTab(0);
            }

            LocalizationManager.LanguageChanged += () =>
            {
                Dispatcher.BeginInvoke(new Action(() =>
                {
                    try
                    {
                        ApplyFilter();
                        ApplyLearnedFilter();
                        foreach (var item in allGameViewModels)
                        {
                            item.RefreshLocalization();
                        }
                        UpdateTabTitles();
                        UpdateDashboardStatus();
                        UpdateSettingsView();
                        UpdateDetailPane();
                        UpdateControllerStatus(lastControllerStatus);
                        UpdateContextualGamepadHints();
                    }
                    catch { }
                }));
            };
        }

        #region Gamepad Handling

        private bool isGamepadMode;

        // Dashboard navigation: row 0 = action tiles (or recovery buttons), row 1 = game shelf (col 0..N)
        private int dashboardNavRow = 0;
        private int dashboardJumpCol = 0;
        private int dashboardShelfIndex = 0;

        // Settings navigation: 2 columns
        private int settingsCol = 0; // 0 = Left (general), 1 = Right (controller feel)
        private int settingsRow0 = 0; // row in Left column
        private int settingsRow1 = 0; // row in Right column

        // Learned navigation
        // section 0 = Toolbar (col 0: SelectAll, col 1: Export, col 2: Delete)
        // section 1 = Cards list (index 0..N)
        private int learnedSection = 1;
        private int learnedToolbarIndex = 0;
        private int learnedNavIndex = -1;

        private void OnGamepadModeChanged(bool isGamepad)
        {
            isGamepadMode = isGamepad;
            UpdateGamepadHudVisibility(isGamepad);
            if (isGamepad) InitTabNavigation();
            else ClearAllNavHighlights();
        }

        private void UpdateGamepadHudVisibility(bool isGamepad)
        {
            if (PnlGamepadHud != null) PnlGamepadHud.Opacity = isGamepad ? 1.0 : 0.6;
        }

        private void OnGamepadConnectionChanged(bool isConnected)
        {
            if (Dispatcher.CheckAccess()) UpdateGamepadConnectionVisibility(isConnected);
            else Dispatcher.BeginInvoke(new Action(() => UpdateGamepadConnectionVisibility(isConnected)));
        }

        private void UpdateGamepadConnectionVisibility(bool isConnected)
        {
            Visibility visibility = isConnected ? Visibility.Visible : Visibility.Collapsed;
            if (HintBumperLeft != null) HintBumperLeft.Visibility = visibility;
            if (HintBumperRight != null) HintBumperRight.Visibility = visibility;
            if (HintSupportMenu != null) HintSupportMenu.Visibility = visibility;
            if (PnlGamepadHud != null) PnlGamepadHud.Visibility = visibility;
            SetXboxGlyphVisibility(this, visibility);
            UpdateContextualGamepadHints();
        }

        private void UpdateContextualGamepadHints()
        {
            bool connected = gamepadNav != null && gamepadNav.IsControllerConnected;
            Visibility common = connected ? Visibility.Visible : Visibility.Collapsed;
            Visibility listOnly = connected && (currentTabIndex == 1 || currentTabIndex == 2)
                ? Visibility.Visible : Visibility.Collapsed;
            Visibility scrollable = connected ? Visibility.Visible : Visibility.Collapsed;

            if (HudTabs != null) HudTabs.Visibility = common;
            if (HudSelect != null) HudSelect.Visibility = common;
            if (HudBack != null) HudBack.Visibility = common;
            if (HudActionX != null) HudActionX.Visibility = listOnly;
            if (HudActionY != null) HudActionY.Visibility = listOnly;
            if (HudScroll != null) HudScroll.Visibility = scrollable;
            if (HudSupport != null) HudSupport.Visibility = common;
            if (HudFilter != null) HudFilter.Visibility = connected && currentTabIndex == 1 ? Visibility.Visible : Visibility.Collapsed;
            if (HudAdjust != null)
            {
                HudAdjust.Visibility = connected && currentTabIndex == 3 && GetFocusedSettingsSlider() != null
                    ? Visibility.Visible : Visibility.Collapsed;
            }
            if (TxtHudActionX != null)
            {
                TxtHudActionX.Text = LocalizationManager.Get(
                    currentTabIndex == 2 ? "Loc_BtnDeleteLearned" : "Loc_Gamepad_ToggleExclude");
            }
        }

        private void SetXboxGlyphVisibility(DependencyObject parent, Visibility visibility)
        {
            if (parent == null) return;
            Style styleA = TryFindResource("XboxBtnA") as Style;
            Style styleB = TryFindResource("XboxBtnB") as Style;
            Style styleX = TryFindResource("XboxBtnX") as Style;
            Style styleY = TryFindResource("XboxBtnY") as Style;

            int count = VisualTreeHelper.GetChildrenCount(parent);
            for (int i = 0; i < count; i++)
            {
                DependencyObject child = VisualTreeHelper.GetChild(parent, i);
                Border border = child as Border;
                if (border != null &&
                    (ReferenceEquals(border.Style, styleA) || ReferenceEquals(border.Style, styleB) ||
                     ReferenceEquals(border.Style, styleX) || ReferenceEquals(border.Style, styleY)))
                {
                    border.Visibility = visibility;
                }
                SetXboxGlyphVisibility(child, visibility);
            }
        }

        private void InitTabNavigation()
        {
            if (currentTabIndex == 0) SetDashboardNav(dashboardNavRow, dashboardNavRow == 0 ? dashboardJumpCol : dashboardShelfIndex);
            else if (currentTabIndex == 1) { if (selectedGameIndex < 0 && filteredGames.Count > 0) SelectGame(0); }
            else if (currentTabIndex == 2)
            {
                if (filteredLearned.Count > 0) SetLearnedNav(learnedNavIndex >= 0 ? learnedNavIndex : 0);
                else SetLearnedToolbarNav(0);
            }
            else if (currentTabIndex == 3) SetSettingsNav(settingsCol, settingsCol == 0 ? settingsRow0 : settingsRow1);
        }

        private void OnGamepadTabCycle(int delta)
        {
            ClearAllNavHighlights();
            int next = currentTabIndex + delta;
            if (next < 0) next = 3;
            else if (next > 3) next = 0;
            SetCurrentTab(next);
            if (isGamepadMode) InitTabNavigation();
        }

        // --- Dashboard Navigation ---
        private void SetDashboardNav(int row, int col = -1)
        {
            ClearDashboardNavHighlights();
            dashboardNavRow = Math.Max(0, Math.Min(1, row));

            if (dashboardNavRow == 0)
            {
                var items = GetDashboardActionItems();
                if (items.Length == 0) return;
                if (col >= 0) dashboardJumpCol = col;
                dashboardJumpCol = Math.Max(0, Math.Min(items.Length - 1, dashboardJumpCol));
                ApplyHighlight(items[dashboardJumpCol]);
            }
            else
            {
                if (dashboardFeaturedGames.Count == 0)
                {
                    SetDashboardNav(0, dashboardJumpCol);
                    return;
                }
                if (col >= 0) dashboardShelfIndex = Math.Max(0, Math.Min(dashboardFeaturedGames.Count - 1, col));
                HighlightShelfItem(dashboardShelfIndex);
            }
        }

        // Row 0 of the home screen: the quick-action tiles, replaced by the
        // recovery buttons while an interrupted session awaits a decision.
        private FrameworkElement[] GetDashboardActionItems(bool navigableOnly = true)
        {
            var items = new List<FrameworkElement>();
            if (!navigableOnly || (PnlSessionRecovery != null && PnlSessionRecovery.Visibility == Visibility.Visible))
            {
                if (BtnResumeSession != null) items.Add(BtnResumeSession);
                if (BtnDismissRecovery != null) items.Add(BtnDismissRecovery);
            }
            if (!navigableOnly || PnlSessionRecovery == null || PnlSessionRecovery.Visibility != Visibility.Visible)
            {
                if (TileManualBridge != null) items.Add(TileManualBridge);
                if (TileJumpGames != null) items.Add(TileJumpGames);
                if (TileJumpLearned != null) items.Add(TileJumpLearned);
            }
            return navigableOnly ? items.Where(IsNavigable).ToArray() : items.ToArray();
        }

        private static bool IsNavigable(FrameworkElement element)
        {
            // Walk the logical parents so hidden sections are skipped even before layout runs.
            for (FrameworkElement current = element; current != null && !(current is Window); current = current.Parent as FrameworkElement)
            {
                if (current.Visibility != Visibility.Visible || !current.IsEnabled) return false;
                if (current is ScrollViewer) break;
            }
            return true;
        }

        private void HighlightShelfItem(int index)
        {
            if (index < 0 || index >= dashboardFeaturedGames.Count) return;
            for (int i = 0; i < dashboardFeaturedGames.Count; i++)
            {
                dashboardFeaturedGames[i].IsSelected = (i == index);
            }

            if (ScrollDashboardShelf != null && LstDashboardFeatured != null)
            {
                double cardWidth = 148.0; // 132 + 16 margin
                double targetOffset = index * cardWidth;
                double viewWidth = ScrollDashboardShelf.ViewportWidth > 0 ? ScrollDashboardShelf.ViewportWidth : ScrollDashboardShelf.ActualWidth;
                if (viewWidth <= 0) viewWidth = 800.0;
                double currentOffset = shelfScrollTarget ?? ScrollDashboardShelf.HorizontalOffset;

                if (targetOffset < currentOffset)
                    SmoothScrollShelfTo(Math.Max(0, targetOffset - 16));
                else if (targetOffset + cardWidth > currentOffset + viewWidth)
                    SmoothScrollShelfTo(targetOffset + cardWidth - viewWidth + 24);
            }
        }

        // --- Shelf scrolling (mouse wheel, arrows, focus) with a short ease-out ---
        private double? shelfScrollTarget;
        private double shelfScrollFrom;
        private DateTime shelfScrollStart;
        private const double ShelfScrollDurationMs = 220.0;

        private void SmoothScrollShelfTo(double offset)
        {
            if (ScrollDashboardShelf == null) return;
            double max = Math.Max(0, ScrollDashboardShelf.ExtentWidth - ScrollDashboardShelf.ViewportWidth);
            offset = Math.Max(0, Math.Min(max > 0 ? max : offset, offset));
            if (!IsLoaded)
            {
                ScrollDashboardShelf.ScrollToHorizontalOffset(offset);
                return;
            }
            shelfScrollFrom = ScrollDashboardShelf.HorizontalOffset;
            shelfScrollStart = DateTime.UtcNow;
            bool running = shelfScrollTarget.HasValue;
            shelfScrollTarget = offset;
            if (!running) CompositionTarget.Rendering += OnShelfScrollFrame;
        }

        private void OnShelfScrollFrame(object sender, EventArgs e)
        {
            if (!shelfScrollTarget.HasValue || ScrollDashboardShelf == null)
            {
                CompositionTarget.Rendering -= OnShelfScrollFrame;
                shelfScrollTarget = null;
                return;
            }
            double t = Math.Min(1.0, (DateTime.UtcNow - shelfScrollStart).TotalMilliseconds / ShelfScrollDurationMs);
            double eased = 1 - Math.Pow(1 - t, 3);
            ScrollDashboardShelf.ScrollToHorizontalOffset(shelfScrollFrom + (shelfScrollTarget.Value - shelfScrollFrom) * eased);
            if (t >= 1.0)
            {
                CompositionTarget.Rendering -= OnShelfScrollFrame;
                shelfScrollTarget = null;
            }
        }

        // The shelf only scrolls horizontally: map the vertical wheel (and tilt wheels) to it.
        private void OnShelfMouseWheel(object sender, MouseWheelEventArgs e)
        {
            double current = shelfScrollTarget ?? ScrollDashboardShelf.HorizontalOffset;
            SmoothScrollShelfTo(current - e.Delta * 1.4);
            e.Handled = true;
        }

        private void OnShelfHoverChanged(object sender, MouseEventArgs e) => UpdateShelfArrows();
        private void OnShelfScrollChanged(object sender, ScrollChangedEventArgs e) => UpdateShelfArrows();

        private void UpdateShelfArrows()
        {
            if (ScrollDashboardShelf == null || BtnShelfLeft == null || BtnShelfRight == null) return;
            bool hover = ShelfHost != null && ShelfHost.IsMouseOver;
            double max = ScrollDashboardShelf.ExtentWidth - ScrollDashboardShelf.ViewportWidth;
            BtnShelfLeft.Visibility = hover && ScrollDashboardShelf.HorizontalOffset > 1 ? Visibility.Visible : Visibility.Collapsed;
            BtnShelfRight.Visibility = hover && ScrollDashboardShelf.HorizontalOffset < max - 1 ? Visibility.Visible : Visibility.Collapsed;
        }

        private void OnShelfLeftClick(object sender, RoutedEventArgs e) => PageShelf(-1);
        private void OnShelfRightClick(object sender, RoutedEventArgs e) => PageShelf(1);

        private void PageShelf(int direction)
        {
            if (ScrollDashboardShelf == null) return;
            double page = Math.Max(148.0, Math.Floor(ScrollDashboardShelf.ViewportWidth * 0.8 / 148.0) * 148.0);
            double current = shelfScrollTarget ?? ScrollDashboardShelf.HorizontalOffset;
            SmoothScrollShelfTo(current + direction * page);
        }

        private void ClearDashboardNavHighlights()
        {
            foreach (var item in GetDashboardActionItems(false)) ClearElementHighlight(item);
            for (int i = 0; i < dashboardFeaturedGames.Count; i++)
            {
                dashboardFeaturedGames[i].IsSelected = false;
            }
        }

        // --- Settings Navigation (2 Columns) ---
        private FrameworkElement[] GetSettingsCol0Items(bool navigableOnly = true)
        {
            var items = new FrameworkElement[]
            {
                TileSettingAutoDetect, TileSettingAdaptive, TileSettingHaptic,
                TileSettingNotifications, TileSettingLanguage,
                BtnOpenControllerTest, BtnPreparedLaunch, BtnReportBug, BtnCheckUpdates
            }.Where(item => item != null);
            return (navigableOnly ? items.Where(IsNavigable) : items).ToArray();
        }

        private FrameworkElement[] GetSettingsCol1Items(bool navigableOnly = true)
        {
            // Only the capabilities of the identified controller are shown (and therefore navigable).
            var items = new FrameworkElement[]
            {
                TileSettingSyncLightbar, RowTriggerStrength, RowVibrationStrength,
                RowGyroStrength, RowGyroYawStrength, RowVibrationThreshold, RowGripVibrations
            }.Where(item => item != null);
            return (navigableOnly ? items.Where(IsNavigable) : items).ToArray();
        }

        private FrameworkElement GetFocusedSettingsItem()
        {
            var items = settingsCol == 0 ? GetSettingsCol0Items() : GetSettingsCol1Items();
            int row = settingsCol == 0 ? settingsRow0 : settingsRow1;
            return row >= 0 && row < items.Length ? items[row] : null;
        }

        private Slider GetFocusedSettingsSlider()
        {
            var row = GetFocusedSettingsItem();
            if (row == null) return null;
            if (row == RowTriggerStrength) return SliderTriggerStrength;
            if (row == RowVibrationStrength) return SliderVibrationStrength;
            if (row == RowGyroStrength) return SliderApex4GyroStrength;
            if (row == RowGyroYawStrength) return SliderApex4GyroYawStrength;
            if (row == RowVibrationThreshold) return SliderVibrationThreshold;
            return null;
        }

        // D-pad left/right on a slider row changes the value by one tick (auto-repeat while held).
        private bool TryAdjustFocusedSlider(int direction)
        {
            var slider = GetFocusedSettingsSlider();
            if (slider == null) return false;
            if (!slider.IsEnabled) return true;
            double step = slider.TickFrequency > 0 ? slider.TickFrequency : 1;
            slider.Value = Math.Max(slider.Minimum, Math.Min(slider.Maximum, slider.Value + direction * step));
            return true;
        }

        private void SetSettingsNav(int col, int row)
        {
            ClearSettingsNavHighlights();
            settingsCol = Math.Max(0, Math.Min(1, col));
            var items = settingsCol == 0 ? GetSettingsCol0Items() : GetSettingsCol1Items();
            if (items.Length == 0 && settingsCol == 1)
            {
                // Nothing to tune without an identified controller: stay on the general column.
                settingsCol = 0;
                items = GetSettingsCol0Items();
            }
            if (items.Length == 0) return;

            int clampedRow = Math.Max(0, Math.Min(items.Length - 1, row));
            if (settingsCol == 0) settingsRow0 = clampedRow;
            else settingsRow1 = clampedRow;

            ApplyHighlight(items[clampedRow]);
            ScrollElementIntoView(ScrollSettings, items[clampedRow]);
            UpdateContextualGamepadHints();
        }

        private void ClearSettingsNavHighlights()
        {
            foreach (var item in GetSettingsCol0Items(false)) ClearElementHighlight(item);
            foreach (var item in GetSettingsCol1Items(false)) ClearElementHighlight(item);
        }

        // --- Learned Navigation ---
        private void SetLearnedNav(int index)
        {
            ClearLearnedToolbarHighlights();
            if (filteredLearned.Count == 0)
            {
                learnedNavIndex = -1;
                learnedSection = 0;
                SetLearnedToolbarNav(0);
                return;
            }
            learnedSection = 1;
            int clamped = Math.Max(0, Math.Min(filteredLearned.Count - 1, index));
            learnedNavIndex = clamped;
            for (int i = 0; i < filteredLearned.Count; i++)
                filteredLearned[i].IsGamepadFocused = (i == learnedNavIndex);

            if (ScrollLearned != null)
            {
                double itemHeight = 90.0;
                double targetOffset = learnedNavIndex * itemHeight;
                double viewHeight = ScrollLearned.ViewportHeight > 0 ? ScrollLearned.ViewportHeight : ScrollLearned.ActualHeight;
                if (viewHeight <= 0) viewHeight = 400.0;
                double currentOffset = ScrollLearned.VerticalOffset;

                if (targetOffset < currentOffset)
                    ScrollLearned.ScrollToVerticalOffset(Math.Max(0, targetOffset - 8));
                else if (targetOffset + itemHeight > currentOffset + viewHeight)
                    ScrollLearned.ScrollToVerticalOffset(targetOffset - viewHeight + itemHeight + 16);
            }
        }

        private void SetLearnedToolbarNav(int index)
        {
            ClearLearnedCardHighlights();
            ClearLearnedToolbarHighlights();
            learnedSection = 0;
            learnedToolbarIndex = Math.Max(0, Math.Min(2, index));
            Button[] btns = new Button[] { BtnSelectAllLearned, BtnExportLearned, BtnDeleteLearned };
            if (learnedToolbarIndex < btns.Length && btns[learnedToolbarIndex] != null)
            {
                ApplyHighlight(btns[learnedToolbarIndex]);
            }
            if (ScrollLearned != null)
            {
                ScrollLearned.ScrollToVerticalOffset(0);
            }
        }

        private void ClearLearnedToolbarHighlights()
        {
            Button[] btns = new Button[] { BtnSelectAllLearned, BtnExportLearned, BtnDeleteLearned };
            foreach (var b in btns)
            {
                if (b != null) ClearElementHighlight(b);
            }
        }

        private void ClearLearnedCardHighlights()
        {
            learnedNavIndex = -1;
            foreach (var item in filteredLearned)
            {
                item.IsGamepadFocused = false;
            }
        }

        // --- Highlight Helpers ---
        // Local Background values replaced while a tile is focused, restored exactly afterwards.
        private readonly Dictionary<Border, object> focusedBackgrounds = new Dictionary<Border, object>();

        private void ApplyHighlight(FrameworkElement element)
        {
            // Console focus: lifted fill plus a crisp white ring drawn just outside the element
            // (no blurred glow: it smeared and was clipped by scroll viewers).
            if (element is Border border)
            {
                if (!focusedBackgrounds.ContainsKey(border))
                    focusedBackgrounds[border] = border.ReadLocalValue(Border.BackgroundProperty);
                border.Background = (Brush)FindResource("GamepadFocusFill");
                border.BorderBrush = Brushes.Transparent;
                border.BorderThickness = new Thickness(2);
                border.Effect = null;
            }
            else if (element is Button btn)
            {
                btn.BorderBrush = Brushes.Transparent;
                btn.BorderThickness = new Thickness(2);
                btn.Effect = null;
            }
            FocusRingAdorner.Show(element);
        }

        private void ClearElementHighlight(FrameworkElement element)
        {
            FocusRingAdorner.Hide(element);
            if (element is Border border)
            {
                object original;
                if (focusedBackgrounds.TryGetValue(border, out original))
                {
                    if (original == DependencyProperty.UnsetValue) border.ClearValue(Border.BackgroundProperty);
                    else border.SetValue(Border.BackgroundProperty, original); // also restores resource references
                    focusedBackgrounds.Remove(border);
                }
                border.BorderBrush = Brushes.Transparent;
                border.BorderThickness = new Thickness(2);
                border.Effect = null;
            }
            else if (element is Button btn)
            {
                btn.BorderBrush = Brushes.Transparent;
                btn.BorderThickness = new Thickness(2);
                btn.Effect = null;
            }
        }

        private void ClearAllNavHighlights()
        {
            ClearDashboardNavHighlights();
            ClearSettingsNavHighlights();
            ClearLearnedToolbarHighlights();
            ClearLearnedCardHighlights();
        }

        private void ScrollElementIntoView(ScrollViewer scroll, FrameworkElement element)
        {
            if (scroll == null || element == null) return;
            try
            {
                var transform = element.TransformToAncestor(scroll);
                var position = transform.Transform(new Point(0, 0));
                double itemTop = position.Y + scroll.VerticalOffset;
                double itemBottom = itemTop + element.ActualHeight;
                double viewTop = scroll.VerticalOffset;
                double viewBottom = viewTop + scroll.ActualHeight;

                if (itemTop < viewTop)
                    scroll.ScrollToVerticalOffset(itemTop - 8);
                else if (itemBottom > viewBottom)
                    scroll.ScrollToVerticalOffset(itemBottom - scroll.ActualHeight + 8);
            }
            catch { }
        }

        // --- Directional Input ---
        private void OnGamepadUp()
        {
                if (currentTabIndex == 0)
                {
                    if (dashboardNavRow == 1) SetDashboardNav(0, dashboardJumpCol);
                }
                else if (currentTabIndex == 1)
                {
                    SelectGame(selectedGameIndex - 1);
                }
                else if (currentTabIndex == 2)
                {
                    if (learnedSection == 1)
                    {
                        if (learnedNavIndex > 0)
                        {
                            SetLearnedNav(learnedNavIndex - 1);
                        }
                        else
                        {
                            SetLearnedToolbarNav(learnedToolbarIndex);
                        }
                    }
                }
                else if (currentTabIndex == 3)
                {
                    int currentRow = settingsCol == 0 ? settingsRow0 : settingsRow1;
                    if (currentRow > 0) SetSettingsNav(settingsCol, currentRow - 1);
                }
        }

        private void OnGamepadDown()
        {
                if (currentTabIndex == 0)
                {
                    if (dashboardNavRow == 0) SetDashboardNav(1, dashboardShelfIndex);
                }
                else if (currentTabIndex == 1)
                {
                    SelectGame(selectedGameIndex + 1);
                }
                else if (currentTabIndex == 2)
                {
                    if (learnedSection == 0)
                    {
                        if (filteredLearned.Count > 0)
                        {
                            SetLearnedNav(0);
                        }
                    }
                    else if (learnedSection == 1)
                    {
                        if (learnedNavIndex < filteredLearned.Count - 1)
                        {
                            SetLearnedNav(learnedNavIndex + 1);
                        }
                    }
                }
                else if (currentTabIndex == 3)
                {
                    var items = settingsCol == 0 ? GetSettingsCol0Items() : GetSettingsCol1Items();
                    int currentRow = settingsCol == 0 ? settingsRow0 : settingsRow1;
                    if (currentRow < items.Length - 1) SetSettingsNav(settingsCol, currentRow + 1);
                }
        }

        private void OnGamepadLeft()
        {
                if (currentTabIndex == 0)
                {
                    if (dashboardNavRow == 0)
                    {
                        if (dashboardJumpCol > 0) SetDashboardNav(0, dashboardJumpCol - 1);
                    }
                    else if (dashboardShelfIndex > 0)
                    {
                        SetDashboardNav(1, dashboardShelfIndex - 1);
                    }
                }
                else if (currentTabIndex == 1)
                {
                    var game = SelectedGame;
                    if (game != null)
                    {
                        game.CycleApexProfile(-1);
                        settings.SetApexProfileSlot(game.Normalized, game.SelectedApexProfileSlot);
                        settings.Save();
                        UpdateDetailPane();
                    }
                }
                else if (currentTabIndex == 2)
                {
                    if (learnedSection == 0)
                    {
                        if (learnedToolbarIndex > 0)
                        {
                            SetLearnedToolbarNav(learnedToolbarIndex - 1);
                        }
                    }
                    else if (learnedSection == 1)
                    {
                        if (learnedNavIndex > 4) SetLearnedNav(learnedNavIndex - 5);
                        else SetLearnedNav(0);
                    }
                }
                else if (currentTabIndex == 3)
                {
                    if (TryAdjustFocusedSlider(-1)) return;
                    if (settingsCol == 1) SetSettingsNav(0, settingsRow0);
                }
        }

        private void OnGamepadRight()
        {
                if (currentTabIndex == 0)
                {
                    if (dashboardNavRow == 0)
                    {
                        SetDashboardNav(0, dashboardJumpCol + 1);
                    }
                    else if (dashboardShelfIndex < dashboardFeaturedGames.Count - 1)
                    {
                        SetDashboardNav(1, dashboardShelfIndex + 1);
                    }
                }
                else if (currentTabIndex == 1)
                {
                    var game = SelectedGame;
                    if (game != null)
                    {
                        game.CycleApexProfile(1);
                        settings.SetApexProfileSlot(game.Normalized, game.SelectedApexProfileSlot);
                        settings.Save();
                        UpdateDetailPane();
                    }
                }
                else if (currentTabIndex == 2)
                {
                    if (learnedSection == 0)
                    {
                        if (learnedToolbarIndex < 2)
                        {
                            SetLearnedToolbarNav(learnedToolbarIndex + 1);
                        }
                    }
                    else if (learnedSection == 1)
                    {
                        if (learnedNavIndex + 5 < filteredLearned.Count) SetLearnedNav(learnedNavIndex + 5);
                        else SetLearnedNav(filteredLearned.Count - 1);
                    }
                }
                else if (currentTabIndex == 3)
                {
                    if (TryAdjustFocusedSlider(1)) return;
                    if (settingsCol == 0) SetSettingsNav(1, settingsRow1);
                }
        }

        private void OnGamepadAction(GamepadButtonAction action)
        {
                switch (action)
                {
                    case GamepadButtonAction.Back:
                        if (HideSupportHint()) break;
                        if (TxtSearch != null && (TxtSearch.IsFocused || !string.IsNullOrEmpty(TxtSearch.Text)))
                        {
                            TxtSearch.Text = string.Empty;
                            Keyboard.ClearFocus();
                        }
                        else if (TxtSearchLearned != null && (TxtSearchLearned.IsFocused || !string.IsNullOrEmpty(TxtSearchLearned.Text)))
                        {
                            TxtSearchLearned.Text = string.Empty;
                            Keyboard.ClearFocus();
                        }
                        else
                        {
                            Close();
                        }
                        break;

                    case GamepadButtonAction.ActionY:
                        if (currentTabIndex == 1 && TxtSearch != null)
                        {
                            TxtSearch.Focus();
                            TxtSearch.SelectAll();
                            VirtualKeyboardService.Show();
                        }
                        else if (currentTabIndex == 2 && TxtSearchLearned != null)
                        {
                            TxtSearchLearned.Focus();
                            TxtSearchLearned.SelectAll();
                            VirtualKeyboardService.Show();
                        }
                        break;

                    case GamepadButtonAction.ActionX:
                        if (currentTabIndex == 1)
                        {
                            ToggleCurrentGameExclusion();
                        }
                        else if (currentTabIndex == 2)
                        {
                            if (learnedSection == 1 && learnedNavIndex >= 0 && learnedNavIndex < filteredLearned.Count)
                            {
                                var item = filteredLearned[learnedNavIndex];
                                DeleteLearnedItemPrompt(item);
                            }
                            else if (learnedSection == 0 && learnedToolbarIndex == 2)
                            {
                                OnDeleteLearnedClick(null, null);
                            }
                        }
                        break;

                    case GamepadButtonAction.Select:
                        ActivateCurrentItem();
                        break;

                    case GamepadButtonAction.Menu:
                        // Options/Menu opens the support page from any tab.
                        OnSupportKofiClick(null, null);
                        break;

                    case GamepadButtonAction.View:
                        if (currentTabIndex == 1) CycleGameFilter();
                        break;
                }
        }

        // Keyboard mirrors the gamepad: arrows move the focus ring, Enter/Space validate,
        // Escape goes back, Ctrl+Tab switches tabs, Ctrl+F searches. The first arrow press
        // only reveals the focus ring so the user sees where they are before anything moves.
        private void OnWindowPreviewKeyDown(object sender, KeyEventArgs e)
        {
            Key key = e.Key == Key.System ? e.SystemKey : e.Key;
            bool ctrl = (Keyboard.Modifiers & ModifierKeys.Control) != 0;
            bool shift = (Keyboard.Modifiers & ModifierKeys.Shift) != 0;
            bool inTextBox = Keyboard.FocusedElement is TextBox;

            if (key == Key.Tab && ctrl)
            {
                RevealKeyboardFocus();
                OnGamepadTabCycle(shift ? -1 : 1);
                e.Handled = true;
                return;
            }
            if (key == Key.F && ctrl)
            {
                RevealKeyboardFocus();
                OnGamepadAction(GamepadButtonAction.ActionY);
                e.Handled = true;
                return;
            }
            if (key == Key.Escape)
            {
                OnGamepadAction(GamepadButtonAction.Back);
                e.Handled = true;
                return;
            }
            if (inTextBox)
            {
                // Typing stays in the search box; Down or Enter hand control back to the list.
                if (key != Key.Down && key != Key.Enter) return;
                Focus();
                bool revealed = RevealKeyboardFocus();
                if (!revealed && key == Key.Down) OnGamepadDown();
                e.Handled = true;
                return;
            }

            switch (key)
            {
                case Key.Left:
                case Key.Right:
                case Key.Up:
                case Key.Down:
                    if (!RevealKeyboardFocus())
                    {
                        if (key == Key.Left) OnGamepadLeft();
                        else if (key == Key.Right) OnGamepadRight();
                        else if (key == Key.Up) OnGamepadUp();
                        else OnGamepadDown();
                    }
                    e.Handled = true;
                    break;
                case Key.Enter:
                case Key.Space:
                    if (!RevealKeyboardFocus()) ActivateCurrentItem();
                    e.Handled = true;
                    break;
            }
        }

        /// <summary>Switches to focus-ring mode. Returns true when the ring was just revealed.</summary>
        private bool RevealKeyboardFocus()
        {
            bool wasHidden = !isGamepadMode;
            if (gamepadNav != null) gamepadNav.NotifyKeyboardNavigation();
            if (!isGamepadMode) OnGamepadModeChanged(true);
            return wasHidden;
        }

        private void CycleGameFilter()
        {
            var filters = new[] { RadAll, RadAdaptive, RadHaptic, RadExcluded };
            int current = Array.FindIndex(filters, f => f != null && f.IsChecked == true);
            var next = filters[(current + 1) % filters.Length];
            if (next != null) next.IsChecked = true;
        }

        private void ActivateCurrentItem()
        {
            if (currentTabIndex == 0)
            {
                if (dashboardNavRow == 0)
                {
                    var items = GetDashboardActionItems();
                    var item = dashboardJumpCol >= 0 && dashboardJumpCol < items.Length ? items[dashboardJumpCol] : null;
                    if (item == TileManualBridge) OnToggleManualBridgeClick(null, null);
                    else if (item == TileJumpGames) SetCurrentTab(1);
                    else if (item == TileJumpLearned) SetCurrentTab(2);
                    else if (item is Button button) button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                }
                else
                {
                    if (dashboardShelfIndex >= 0 && dashboardShelfIndex < dashboardFeaturedGames.Count)
                    {
                        var featured = dashboardFeaturedGames[dashboardShelfIndex];
                        OpenGameInCertifiedList(featured);
                    }
                }
            }
            else if (currentTabIndex == 1)
            {
                var game = SelectedGame;
                if (game != null)
                {
                    game.CycleApexProfile(1);
                    settings.SetApexProfileSlot(game.Normalized, game.SelectedApexProfileSlot);
                    settings.Save();
                    UpdateDetailPane();
                }
            }
            else if (currentTabIndex == 2)
            {
                if (learnedSection == 0)
                {
                    if (learnedToolbarIndex == 0) OnSelectAllLearnedClick(null, null);
                    else if (learnedToolbarIndex == 1) OnExportLearnedClick(null, null);
                    else if (learnedToolbarIndex == 2) OnDeleteLearnedClick(null, null);
                }
                else if (learnedSection == 1)
                {
                    if (learnedNavIndex >= 0 && learnedNavIndex < filteredLearned.Count)
                    {
                        var item = filteredLearned[learnedNavIndex];
                        item.IsSelected = !item.IsSelected;
                        UpdateLearnedButtons();
                    }
                }
            }
            else if (currentTabIndex == 3)
            {
                // Dispatch on the focused element, not on its index: rows appear and
                // disappear with the detected controller and the detection toggle.
                var item = GetFocusedSettingsItem();
                if (item == TileSettingAutoDetect) OnSettingAutoDetectToggled(null, null);
                else if (item == TileSettingAdaptive) OnSettingAdaptiveToggled(null, null);
                else if (item == TileSettingHaptic) OnSettingHapticToggled(null, null);
                else if (item == TileSettingNotifications) OnSettingNotificationsToggled(null, null);
                else if (item == TileSettingSyncLightbar) OnSettingSyncLightbarToggled(null, null);
                else if (item == TileSettingLanguage) OpenLanguagePicker();
                else if (item == RowGripVibrations) ToggleGripVibrations();
                else if (item is Button button) button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                // Keep the focus ring on the same element after a toggle changed which rows are visible.
                if (item != null)
                {
                    var items = settingsCol == 0 ? GetSettingsCol0Items() : GetSettingsCol1Items();
                    int index = Array.IndexOf(items, item);
                    SetSettingsNav(settingsCol, index >= 0 ? index : (settingsCol == 0 ? settingsRow0 : settingsRow1));
                }
            }
        }

        private void DeleteLearnedItemPrompt(LearnedItemViewModel item)
        {
            if (item == null || learningService == null) return;
            var result = MessageBox.Show(
                this,
                LocalizationManager.Format("Loc_LearnedDeleteConfirm", 1),
                LocalizationManager.Get("Loc_LearnedWindowTitle"),
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning);
            if (result != MessageBoxResult.Yes) return;
            learningService.DeleteBindings(new[] { item.Path });
            LoadLearnedItems();
            if (filteredLearned.Count > 0)
            {
                SetLearnedNav(Math.Min(learnedNavIndex, filteredLearned.Count - 1));
            }
            else
            {
                SetLearnedToolbarNav(0);
            }
        }

        private void OnGamepadScroll(double deltaY)
        {
            if (currentTabIndex == 0)
            {
                if (ScrollDashboardShelf != null)
                {
                    ScrollDashboardShelf.ScrollToHorizontalOffset(
                        Math.Max(0, ScrollDashboardShelf.HorizontalOffset + deltaY));
                }
            }
            else
            {
                ScrollViewer targetScroll = null;
                if (currentTabIndex == 1) targetScroll = ScrollCertified;
                else if (currentTabIndex == 2) targetScroll = ScrollLearned;
                else if (currentTabIndex == 3) targetScroll = ScrollSettings;

                if (targetScroll != null)
                {
                    targetScroll.ScrollToVerticalOffset(targetScroll.VerticalOffset + deltaY);
                }
            }
        }

        #endregion

        #region Navigation Tabs

        private void SetCurrentTab(int index)
        {
            // XAML raises Checked while named controls are still being created.
            // The constructor selects the initial tab once the whole view exists.
            if (!viewInitialized) return;
            bool changed = currentTabIndex != index;
            currentTabIndex = index;
            if (NavTabDashboard != null) NavTabDashboard.IsChecked = (index == 0);
            if (NavTabCertified != null) NavTabCertified.IsChecked = (index == 1);
            if (NavTabLearned != null) NavTabLearned.IsChecked = (index == 2);
            if (NavTabSettings != null) NavTabSettings.IsChecked = (index == 3);

            if (PanelDashboard != null) PanelDashboard.Visibility = (index == 0) ? Visibility.Visible : Visibility.Collapsed;
            if (PanelCertifiedGames != null) PanelCertifiedGames.Visibility = (index == 1) ? Visibility.Visible : Visibility.Collapsed;
            if (PanelLearnedExecutables != null) PanelLearnedExecutables.Visibility = (index == 2) ? Visibility.Visible : Visibility.Collapsed;
            if (ScrollSettings != null) ScrollSettings.Visibility = (index == 3) ? Visibility.Visible : Visibility.Collapsed;
            UpdateContextualGamepadHints();
            if (changed)
            {
                FrameworkElement[] panels = { PanelDashboard, PanelCertifiedGames, PanelLearnedExecutables, ScrollSettings };
                AnimatePanelIn(panels[index]);
            }

            if (index == 0)
            {
                UpdateDashboardStatus();
            }
            else if (index == 1)
            {
                if (selectedGameIndex < 0 && filteredGames.Count > 0)
                {
                    SelectGame(0);
                }
            }
            if (isGamepadMode) InitTabNavigation();
        }

        // Console-style page transition: short fade with a slight upward slide.
        private static void AnimatePanelIn(FrameworkElement panel)
        {
            if (panel == null) return;
            var ease = new System.Windows.Media.Animation.CubicEase { EasingMode = System.Windows.Media.Animation.EasingMode.EaseOut };
            var duration = TimeSpan.FromMilliseconds(220);
            var slide = new TranslateTransform(0, 14);
            panel.RenderTransform = slide;
            panel.BeginAnimation(OpacityProperty, new System.Windows.Media.Animation.DoubleAnimation(0, 1, duration) { EasingFunction = ease });
            var slideIn = new System.Windows.Media.Animation.DoubleAnimation(14, 0, duration) { EasingFunction = ease };
            EventHandler followSlide = (s, e) => FocusRingAdorner.RefreshAll();
            CompositionTarget.Rendering += followSlide;
            slideIn.Completed += (s, e) =>
            {
                CompositionTarget.Rendering -= followSlide;
                FocusRingAdorner.RefreshAll();
            };
            slide.BeginAnimation(TranslateTransform.YProperty, slideIn);
        }

        private void UpdateClock()
        {
            if (TxtClock != null) TxtClock.Text = DateTime.Now.ToString("t", CultureInfo.CurrentCulture);
        }

        private void OnNavTabChanged(object sender, RoutedEventArgs e)
        {
            if (NavTabDashboard != null && NavTabDashboard.IsChecked == true) SetCurrentTab(0);
            else if (NavTabCertified != null && NavTabCertified.IsChecked == true) SetCurrentTab(1);
            else if (NavTabLearned != null && NavTabLearned.IsChecked == true) SetCurrentTab(2);
            else if (NavTabSettings != null && NavTabSettings.IsChecked == true) SetCurrentTab(3);
        }

        private void OnJumpToGamesClick(object sender, MouseButtonEventArgs e)
        {
            SetCurrentTab(1);
        }

        private void OnJumpToLearnedClick(object sender, MouseButtonEventArgs e)
        {
            SetCurrentTab(2);
        }

        #endregion

        #region Dashboard View

        private string lastControllerStatus = "disconnected";

        private void UpdateControllerStatus(string status)
        {
            if (string.IsNullOrWhiteSpace(status)) status = "disconnected";
            lastControllerStatus = status;

            bool isApex4 = string.Equals(status, "apex4", StringComparison.OrdinalIgnoreCase);
            bool isApex5 = string.Equals(status, "apex5", StringComparison.OrdinalIgnoreCase);
            bool isApex6 = string.Equals(status, "apex6", StringComparison.OrdinalIgnoreCase);
            bool isConnected = isApex4 || isApex5 || isApex6;
            sessionManager?.UpdateRecoveryController(isConnected);
            UpdateSettingsView();

            string fullLabel;
            string shortLabel;

            if (isApex4)
            {
                fullLabel = LocalizationManager.Get("Loc_ControllerApex4");
                shortLabel = "Apex 4";
            }
            else if (isApex5)
            {
                fullLabel = LocalizationManager.Get("Loc_ControllerApex5");
                shortLabel = "Apex 5";
            }
            else if (isApex6)
            {
                fullLabel = "Flydigi Apex 6 Pro";
                shortLabel = "Apex 6 Pro";
            }
            else if (string.Equals(status, "unsupported", StringComparison.OrdinalIgnoreCase))
            {
                fullLabel = LocalizationManager.Get("Loc_ControllerUnsupported");
                shortLabel = LocalizationManager.Get("Loc_StatusUnknown");
            }
            else if (string.Equals(status, "unavailable", StringComparison.OrdinalIgnoreCase))
            {
                fullLabel = LocalizationManager.Get("Loc_ControllerUnavailable");
                shortLabel = LocalizationManager.Get("Loc_StatusUnavailable");
            }
            else
            {
                fullLabel = LocalizationManager.Get("Loc_ControllerDisconnected");
                shortLabel = "APEX";
            }

            Brush activeStroke = (Brush)FindResource("PlayStationBlue");
            Brush mutedStroke = (Brush)FindResource("TextMuted");
            Brush activeText = (Brush)FindResource("TextPrimary");

            // 1. Hero banner on Dashboard (exact model image; never substitute another model)
            if (ImgDashboardController != null)
            {
                bool hasExactControllerImage = isApex4 || isApex5 || isApex6;
                ImgDashboardController.Visibility = hasExactControllerImage ? Visibility.Visible : Visibility.Collapsed;
                if (hasExactControllerImage)
                {
                    string asset = isApex4
                        ? "Resources/controller-apex4.png"
                        : isApex5
                            ? "Resources/controller-apex5.png"
                            : "Resources/controller-apex6-pro.png";
                    ImgDashboardController.Source = new BitmapImage(new Uri(asset, UriKind.Relative));
                }
                else
                {
                    ImgDashboardController.Source = null;
                }
            }

            // 2. Persistent Header Indicator (visible on all tabs, flat with no outline)
            if (TxtHeaderController != null)
            {
                TxtHeaderController.Text = shortLabel;
                TxtHeaderController.Foreground = isConnected ? activeText : mutedStroke;
            }
            if (IconHeaderController != null)
            {
                IconHeaderController.Stroke = isConnected ? activeStroke : mutedStroke;
            }
            if (BadgeHeaderController != null)
            {
                BadgeHeaderController.Visibility = isConnected ? Visibility.Visible : Visibility.Collapsed;
                BadgeHeaderController.BorderThickness = new Thickness(0);
                BadgeHeaderController.BorderBrush = Brushes.Transparent;
                if (isConnected)
                {
                    BadgeHeaderController.Background = new SolidColorBrush(Color.FromArgb(0x28, 0x00, 0x70, 0xD1));
                }
                else
                {
                    BadgeHeaderController.Background = new SolidColorBrush(Color.FromArgb(0x14, 0xFF, 0xFF, 0xFF));
                }
            }
        }

        private void UpdateDashboardStatus()
        {
            if (!viewInitialized) return;
            sessionManager?.UpdateRecoveryController(lastControllerStatus == "apex4" || lastControllerStatus == "apex5" || lastControllerStatus == "apex6");
            var recovery = sessionManager != null ? sessionManager.Recovery : null;
            UpdateRecoveryPanel(recovery);
            bool isActive = sessionManager != null && sessionManager.IsSessionActive;
            var external = !isActive ? BridgeSession.ReadActiveSession() : null;
            string phase = external != null ? external.Phase.ToString() : sessionManager != null ? sessionManager.StateName : "Stopped";
            bool unknownExternal = !isActive && external == null && phase != "Starting" && phase != "Stopping" && sessionManager != null && sessionManager.HasExternalSession;
            if (unknownExternal) phase = "External";
            bool anyActive = isActive || external != null || unknownExternal;
            if (BadgeDashboardStatus != null)
            {
                BadgeDashboardStatus.Background = (Brush)FindResource(anyActive ? "BadgeActiveBg" : "BadgeStandbyBg");
            }
            if (TxtDashboardStatus != null)
            {
                TxtDashboardStatus.Text = LocalizationManager.Get(recovery != null && recovery.Pending && !recovery.Resuming && external == null
                    ? "Loc_SessionInterrupted" : "Loc_Session" + phase);
                TxtDashboardStatus.Foreground = (Brush)FindResource(anyActive ? "BadgeActiveFg" : "BadgeStandbyFg");
            }

            if (external != null && TxtDashboardGameTitle != null)
                TxtDashboardGameTitle.Text = string.IsNullOrWhiteSpace(external.Game) ? LocalizationManager.Get("Loc_NotificationGame") : external.Game;
            else if (TxtDashboardGameTitle != null && sessionManager != null &&
                (phase == "Starting" || phase == "Failed"))
                TxtDashboardGameTitle.Text = sessionManager.ActiveGameTitle;
            if (TxtDashboardHint != null)
            {
                string profile = external != null ? external.Profile : unknownExternal ? null : sessionManager != null ? sessionManager.ActiveProfile : null;
                string selectedController = external != null ? external.Controller : unknownExternal ? null : sessionManager != null ? sessionManager.ActiveController : null;
                string owner = external != null ? external.Owner : unknownExternal ? LocalizationManager.Get("Loc_ExternalOwner") : recovery != null && recovery.Pending ? recovery.Owner : (isActive || phase == "Starting") ? "Tray" : null;
                string reason = external != null ? (external.Phase == SessionPhase.Ready ? null : external.Message) : unknownExternal ? null : sessionManager != null ? sessionManager.LastReason : null;
                string profileText = string.IsNullOrWhiteSpace(profile) || profile == "none" ? null : profile;
                string controllerText = string.IsNullOrWhiteSpace(selectedController) ? null : selectedController;
                string ownerText = string.IsNullOrWhiteSpace(owner) ? null : owner;
                // Full labelled details stay available in the tooltip; the home shows only compact chips.
                string details = LocalizationManager.Format("Loc_SessionDetails", ownerText ?? "·", profileText ?? "·", controllerText ?? "·");
                string readable = null;
                if (!string.IsNullOrWhiteSpace(reason))
                {
                    readable = reason.StartsWith("Loc_") ? LocalizationManager.Get(reason) : reason;
                    if (reason.StartsWith("Temporary APEX isolation failed:", StringComparison.Ordinal))
                    {
                        details += "\n" + reason;
                        readable = LocalizationManager.Get("Loc_RefusedIsolation");
                    }
                }
                // Idle home stays clean; chips appear for a live, starting, failed or interrupted session.
                bool showDetails = anyActive || phase == "Starting" || phase == "Failed" || readable != null ||
                    (recovery != null && (recovery.Pending || recovery.Recovered));
                SetSessionChip(ChipSessionOwner, TxtSessionOwner, ownerText);
                SetSessionChip(ChipSessionProfile, TxtSessionProfile, profileText);
                SetSessionChip(ChipSessionController, TxtSessionController, controllerText);
                if (PnlSessionChips != null)
                {
                    PnlSessionChips.Visibility = showDetails && (ownerText != null || profileText != null || controllerText != null)
                        ? Visibility.Visible : Visibility.Collapsed;
                    PnlSessionChips.ToolTip = details;
                }
                TxtDashboardHint.Text = readable ?? string.Empty;
                TxtDashboardHint.Visibility = readable != null ? Visibility.Visible : Visibility.Collapsed;
                TxtDashboardHint.ToolTip = readable != null ? details + "\n" + LocalizationManager.Get("Loc_SessionReason") + " " + readable : null;
            }

            if (isActive && sessionManager != null)
            {
                if (TxtDashboardGameTitle != null) TxtDashboardGameTitle.Text = sessionManager.ActiveGameTitle ?? LocalizationManager.Get("Loc_NotificationGame");

                var activeGame = allGameViewModels.FirstOrDefault(g => string.Equals(g.Title, sessionManager.ActiveGameTitle, StringComparison.OrdinalIgnoreCase));
                if (activeGame != null && activeGame.HasCoverImage && ImgDashboardActiveCover != null)
                {
                    ImgDashboardActiveCover.Source = activeGame.CoverImage;
                    PnlDashboardActiveCover.Visibility = Visibility.Visible;
                    PnlDashboardStandbyCover.Visibility = Visibility.Collapsed;
                }
                else
                {
                    if (PnlDashboardActiveCover != null) PnlDashboardActiveCover.Visibility = Visibility.Collapsed;
                    if (PnlDashboardStandbyCover != null) PnlDashboardStandbyCover.Visibility = Visibility.Visible;
                }

                if (BadgeDashAdaptive != null) BadgeDashAdaptive.Visibility = (activeGame != null && activeGame.AdaptiveTriggers) ? Visibility.Visible : Visibility.Collapsed;
                if (BadgeDashHaptic != null) BadgeDashHaptic.Visibility = (activeGame != null && activeGame.HapticFeedback) ? Visibility.Visible : Visibility.Collapsed;
                if (BadgeDashTouchpad != null) BadgeDashTouchpad.Visibility = (activeGame != null && activeGame.HasCustomRemapping) ? Visibility.Visible : Visibility.Collapsed;
            }
            else
            {
                if (TxtDashboardGameTitle != null && external == null && phase != "Starting" && phase != "Failed") TxtDashboardGameTitle.Text = LocalizationManager.Get("Loc_NoActiveGame");
                if (PnlDashboardActiveCover != null) PnlDashboardActiveCover.Visibility = Visibility.Collapsed;
                if (PnlDashboardStandbyCover != null) PnlDashboardStandbyCover.Visibility = Visibility.Visible;
                if (BadgeDashAdaptive != null) BadgeDashAdaptive.Visibility = Visibility.Collapsed;
                if (BadgeDashHaptic != null) BadgeDashHaptic.Visibility = Visibility.Collapsed;
                if (BadgeDashTouchpad != null) BadgeDashTouchpad.Visibility = Visibility.Collapsed;
            }

            UpdateManualBridgeTile();
            UpdateTabTitles();
        }

        private static void SetSessionChip(Border chip, TextBlock text, string value)
        {
            if (chip == null || text == null) return;
            text.Text = value ?? string.Empty;
            chip.Visibility = value != null ? Visibility.Visible : Visibility.Collapsed;
        }

        private void UpdateRecoveryPanel(SessionRecoveryState recovery)
        {
            if (PnlSessionRecovery == null || PnlDashboardCounts == null) return;
            bool visible = recovery != null && (recovery.Pending || recovery.Recovered);
            PnlSessionRecovery.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
            PnlDashboardCounts.Visibility = visible ? Visibility.Collapsed : Visibility.Visible;
            if (!visible) return;
            TxtRecoveryTitle.Text = LocalizationManager.Get(recovery.Recovered ? "Loc_RecoveryReady" : recovery.Resuming ? "Loc_RecoveryStarting" : "Loc_RecoveryInterrupted");
            var steps = new[] { "Loc_RecoveryController", "Loc_RecoveryVirtual", "Loc_RecoveryIsolation", "Loc_RecoveryRuntime" };
            TxtRecoverySteps.Text = string.Join("   ·   ", steps.Select((key, index) =>
                ((recovery.Stages & (1u << index)) != 0 ? "✓ " : "○ ") + LocalizationManager.Get(key)));
            TxtRecoveryHint.Text = LocalizationManager.Get(recovery.Owner != "Tray" ? "Loc_RecoveryPlaynite" : recovery.Recovered ? "Loc_RecoveryGameHint" : recovery.Resuming ? "Loc_RecoveryChecking" :
                recovery.ControllerAvailable ? "Loc_RecoveryControllerFound" : "Loc_RecoveryWaiting");
            BtnResumeSession.Visibility = recovery.Pending && recovery.Owner == "Tray" ? Visibility.Visible : Visibility.Collapsed;
            BtnResumeSession.IsEnabled = !recovery.Resuming;
            BtnDismissRecovery.IsEnabled = !recovery.Resuming;
        }

        private async void OnResumeSessionClick(object sender, RoutedEventArgs e)
        {
            if (sessionManager == null || settings == null) return;
            if (MessageBox.Show(this, LocalizationManager.Get("Loc_RecoveryConfirm"), "ApexSenseBridge",
                MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
            string error = null;
            BtnResumeSession.IsEnabled = false;
            bool success = await Task.Run(() => sessionManager.ResumeSession(settings, out error));
            if (!success) MessageBox.Show(this, error != null && error.StartsWith("Loc_") ? LocalizationManager.Get(error) : error,
                "ApexSenseBridge", MessageBoxButton.OK, MessageBoxImage.Warning);
            UpdateDashboardStatus();
        }

        private void OnDismissRecoveryClick(object sender, RoutedEventArgs e)
        {
            sessionManager?.DismissRecovery();
            UpdateDashboardStatus();
        }

        private void UpdateManualBridgeTile()
        {
            bool isManual = settings != null && settings.ForcedProfile == "standard";
            if (BadgeManualBridgeToggle != null)
            {
                BadgeManualBridgeToggle.Background = isManual ? (Brush)FindResource("PlayStationBlue") : (Brush)FindResource("ControlBackground");
                BadgeManualBridgeToggle.BorderBrush = isManual ? (Brush)FindResource("PlayStationBlue") : (Brush)FindResource("ControlBorder");
            }
            if (DotManualBridgeToggle != null)
            {
                DotManualBridgeToggle.HorizontalAlignment = isManual ? HorizontalAlignment.Right : HorizontalAlignment.Left;
                DotManualBridgeToggle.Margin = isManual ? new Thickness(0, 0, 4, 0) : new Thickness(4, 0, 0, 0);
                DotManualBridgeToggle.Fill = isManual ? Brushes.White : (Brush)FindResource("TextMuted");
            }
        }

        private void OnToggleManualBridgeClick(object sender, MouseButtonEventArgs e)
        {
            ToggleManualBridge();
        }

        private async void OnPreparedLaunchClick(object sender, RoutedEventArgs e)
        {
            if (settings == null || sessionManager == null) return;
            if (preparedLaunchPending || sessionManager.IsSessionActive)
            {
                MessageBox.Show(this, LocalizationManager.Get("Loc_PreparedLaunchBusy"),
                    "ApexSenseBridge", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            var picker = new OpenFileDialog
            {
                Filter = "Programs (*.exe)|*.exe",
                CheckFileExists = true,
                Multiselect = false
            };
            if (picker.ShowDialog(this) != true) return;

            SupportedGame launchGame = null;
            string gameKey;
            if (settings.TryGetGameForExecutable(picker.FileName, out gameKey))
                gameListService.TryFindExactGame(gameKey, out launchGame);
            if (launchGame == null && learningService != null)
                learningService.TryResolve(picker.FileName, gameListService, out launchGame);
            var title = launchGame != null ? launchGame.Title : System.IO.Path.GetFileNameWithoutExtension(picker.FileName);
            var profile = launchGame != null ? launchGame.Profile : "standard";
            var slot = launchGame != null ? settings.GetApexProfileSlot(launchGame.Normalized) : 0;
            settings.ForcedProfile = "standard";
            preparedLaunchPending = true;
            string error = null;
            bool started = false;
            try
            {
                started = await Task.Run(() => sessionManager.StartSession(title, profile, settings, slot, out error));
            }
            finally { preparedLaunchPending = false; }
            if (!started)
            {
                settings.ForcedProfile = "none";
                MessageBox.Show(this, error, "ApexSenseBridge",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                UpdateManualBridgeTile();
                return;
            }

            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = picker.FileName,
                    WorkingDirectory = System.IO.Path.GetDirectoryName(picker.FileName),
                    UseShellExecute = true
                });
            }
            catch (Exception ex)
            {
                sessionManager.StopSession("Prepared game launch failed");
                settings.ForcedProfile = "none";
                MessageBox.Show(this,
                    LocalizationManager.Format("Loc_PreparedLaunchError", ex.Message),
                    "ApexSenseBridge", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            UpdateManualBridgeTile();
            UpdateDashboardStatus();
        }

        private void ToggleManualBridge()
        {
            if (settings == null) return;
            bool enable = settings.ForcedProfile != "standard";

            if (enable)
            {
                if (sessionManager != null && sessionManager.AutomaticActivationBlocked)
                    sessionManager.StopSession("Fresh manual activation requested");
                if (sessionManager != null && sessionManager.IsSessionActive)
                {
                    MessageBox.Show(this, LocalizationManager.Get("Loc_PreparedLaunchBusy"),
                        "ApexSenseBridge", MessageBoxButton.OK,
                        MessageBoxImage.Information);
                    return;
                }
                settings.ForcedProfile = "standard";
                settings.Save();
                if (sessionManager != null && !sessionManager.IsSessionActive)
                {
                    string error;
                    if (!sessionManager.StartSession(LocalizationManager.Get("Loc_ManualBridgeGameTitle"), "standard", settings, out error))
                    {
                        settings.ForcedProfile = "none";
                        settings.Save();
                        MessageBox.Show(error, "ApexSenseBridge", MessageBoxButton.OK, MessageBoxImage.Warning);
                    }
                }
            }
            else
            {
                settings.ForcedProfile = "none";
                settings.Save();
                if (sessionManager != null && sessionManager.IsSessionActive)
                    sessionManager.StopSession("Manual bridge disabled");
                monitorService?.ForceCheck();
            }

            UpdateManualBridgeTile();
            UpdateDashboardStatus();
            UpdateSettingsView();
        }

        private void OnDashboardFeaturedGameClicked(object sender, MouseButtonEventArgs e)
        {
            if (sender is FrameworkElement fe && fe.DataContext is GameItemViewModel game)
            {
                OpenGameInCertifiedList(game);
            }
        }

        private void OpenGameInCertifiedList(GameItemViewModel target)
        {
            if (target == null) return;
            SetCurrentTab(1);
            if (TxtSearch != null) TxtSearch.Text = "";
            if (RadAll != null) RadAll.IsChecked = true;
            ApplyFilter();

            int targetIndex = filteredGames.IndexOf(target);
            if (targetIndex >= 0)
            {
                SelectGame(targetIndex);
            }
        }

        #endregion

        #region Certified Games (Master-Detail)

        public GameItemViewModel SelectedGame =>
            (selectedGameIndex >= 0 && selectedGameIndex < filteredGames.Count) ? filteredGames[selectedGameIndex] : null;

        private void LoadGames()
        {
            allGameViewModels.Clear();
            var list = gameListService?.GetAllGames() ?? new List<SupportedGame>();

            foreach (var g in list)
            {
                var vm = new GameItemViewModel
                {
                    Game = g,
                    IsExcluded = settings.IsGameExcluded(g.Normalized),
                    SelectedApexProfileSlot = settings.GetApexProfileSlot(g.Normalized)
                };
                allGameViewModels.Add(vm);
            }

            ApplyFilter();
            UpdateTabTitles();

            // Issue #29: the home shelf leads with the latest catalogue additions.
            // The earliest "addedAt" is the initial import, so only later dates are real additions.
            var dated = allGameViewModels.Where(g => g.AddedAt.HasValue).ToList();
            DateTime? baseline = dated.Count > 0 ? dated.Min(g => g.AddedAt.Value) : (DateTime?)null;
            DateTime today = DateTime.UtcNow.Date;
            foreach (var g in allGameViewModels)
            {
                g.IsRecentlyAdded = baseline.HasValue && g.AddedAt.HasValue && g.AddedAt.Value > baseline.Value &&
                    (today - g.AddedAt.Value).TotalDays <= NewGameBadgeDays;
            }
            var additions = allGameViewModels
                .Where(g => !g.IsExcluded && baseline.HasValue && g.AddedAt.HasValue && g.AddedAt.Value > baseline.Value)
                .OrderByDescending(g => g.AddedAt.Value)
                .ThenBy(g => g.Title, StringComparer.OrdinalIgnoreCase)
                .Take(DashboardShelfSize)
                .ToList();
            shelfShowsAdditions = additions.Count > 0;
            var featured = additions.Concat(allGameViewModels
                    .Where(g => !g.IsExcluded && g.HasReadyFeature && !additions.Contains(g))
                    .OrderByDescending(GetFeaturedScore)
                    .ThenBy(g => GetStableSelectionKey(g.Normalized)))
                .Take(DashboardShelfSize)
                .ToList();

            dashboardFeaturedGames.Clear();
            foreach (var f in featured)
            {
                dashboardFeaturedGames.Add(f);
            }
            dashboardShelfIndex = 0;
            shelfScrollTarget = null;
            if (ScrollDashboardShelf != null) ScrollDashboardShelf.ScrollToHorizontalOffset(0);
            UpdateShelfTitle();
        }

        private const int DashboardShelfSize = 14;
        private const int NewGameBadgeDays = 30;
        private bool shelfShowsAdditions;

        private void UpdateShelfTitle()
        {
            if (TxtShelfTitle != null)
                TxtShelfTitle.Text = LocalizationManager.Get(shelfShowsAdditions ? "Loc_ShelfRecentlyAdded" : "Loc_NavCertifiedGames");
        }

        private static int GetFeaturedScore(GameItemViewModel game)
        {
            int score = 0;
            if (game.AdaptiveTriggers) score += 4;
            if (game.HapticFeedback) score += 4;
            if (game.HasCustomRemapping) score += 2;
            if (!string.IsNullOrWhiteSpace(game.IconUrl)) score += 1;
            return score;
        }

        private static uint GetStableSelectionKey(string value)
        {
            unchecked
            {
                uint hash = 2166136261;
                foreach (char character in value ?? string.Empty)
                {
                    hash ^= char.ToLowerInvariant(character);
                    hash *= 16777619;
                }
                return hash;
            }
        }

        private void ApplyFilter()
        {
            string query = TxtSearch?.Text?.Trim()?.ToLowerInvariant() ?? "";
            bool filterAdaptive = RadAdaptive?.IsChecked == true;
            bool filterHaptic = RadHaptic?.IsChecked == true;
            bool filterExcluded = RadExcluded?.IsChecked == true;

            var matching = allGameViewModels.Where(g =>
            {
                if (filterExcluded && !g.IsExcluded) return false;
                if (!filterExcluded && g.IsExcluded && RadAll?.IsChecked != true) return false;
                if (filterAdaptive && !g.AdaptiveTriggers) return false;
                if (filterHaptic && !g.HapticFeedback) return false;

                if (!string.IsNullOrEmpty(query))
                {
                    return g.Title.ToLowerInvariant().Contains(query) ||
                           g.Normalized.ToLowerInvariant().Contains(query);
                }
                return true;
            }).OrderBy(g => g.Title).ToList();

            filteredGames.Clear();
            foreach (var item in matching)
            {
                item.IsSelected = false;
                filteredGames.Add(item);
            }

            if (TxtStats != null)
            {
                TxtStats.Text = string.Format("{0} / {1}", filteredGames.Count, allGameViewModels.Count);
            }

            if (filteredGames.Count > 0)
            {
                SelectGame(0);
            }
            else
            {
                selectedGameIndex = -1;
                UpdateDetailPane();
            }
        }

        private void SelectGame(int newIndex)
        {
            if (filteredGames.Count == 0)
            {
                selectedGameIndex = -1;
                UpdateDetailPane();
                return;
            }

            int clamped = Math.Max(0, Math.Min(filteredGames.Count - 1, newIndex));

            if (selectedGameIndex >= 0 && selectedGameIndex < filteredGames.Count)
            {
                filteredGames[selectedGameIndex].IsSelected = false;
            }

            selectedGameIndex = clamped;
            var selected = filteredGames[selectedGameIndex];
            selected.IsSelected = true;

            UpdateDetailPane();

            if (ScrollCertified != null)
            {
                double targetOffset = selectedGameIndex * 62.0;
                double currentOffset = ScrollCertified.VerticalOffset;
                double viewHeight = ScrollCertified.ActualHeight > 0 ? ScrollCertified.ActualHeight : 400.0;

                if (targetOffset < currentOffset)
                {
                    ScrollCertified.ScrollToVerticalOffset(targetOffset);
                }
                else if (targetOffset + 62.0 > currentOffset + viewHeight)
                {
                    ScrollCertified.ScrollToVerticalOffset(targetOffset - viewHeight + 80.0);
                }
            }
        }

        private void UpdateDetailPane()
        {
            var game = SelectedGame;
            if (game == null)
            {
                if (TxtDetailEmpty != null) TxtDetailEmpty.Visibility = Visibility.Visible;
                if (ScrollDetail != null) ScrollDetail.Visibility = Visibility.Collapsed;
                return;
            }

            if (TxtDetailEmpty != null) TxtDetailEmpty.Visibility = Visibility.Collapsed;
            if (ScrollDetail != null) ScrollDetail.Visibility = Visibility.Visible;

            if (TxtDetailTitle != null) TxtDetailTitle.Text = game.Title;
            if (TxtDetailInitials != null) TxtDetailInitials.Text = game.Initials;
            if (PnlDetailMonogram != null) PnlDetailMonogram.Background = game.MonogramBackground;

            if (ImgDetailCover != null)
            {
                ImgDetailCover.Source = game.CoverImage;
                ImgDetailCover.Visibility = game.HasCoverImage ? Visibility.Visible : Visibility.Collapsed;
                if (PnlDetailMonogram != null)
                {
                    PnlDetailMonogram.Visibility = game.HasCoverImage ? Visibility.Collapsed : Visibility.Visible;
                }
            }

            if (BadgeDetailAdaptive != null) BadgeDetailAdaptive.Visibility = game.AdaptiveVisibility;
            if (BadgeDetailHaptic != null) BadgeDetailHaptic.Visibility = game.HapticVisibility;
            if (BadgeDetailTouchpad != null) BadgeDetailTouchpad.Visibility = game.RemappingVisibility;
            if (BadgeDetailManualFix != null) BadgeDetailManualFix.Visibility = game.ManualFixVisibility;

            if (TxtDetailApexProfile != null) TxtDetailApexProfile.Text = game.SelectedApexProfileDisplay;
            UpdateExecutableChoices();

            if (TxtDetailExcludeAction != null)
            {
                TxtDetailExcludeAction.Text = game.IsExcluded
                    ? LocalizationManager.Get("Loc_StateIncluded")
                    : LocalizationManager.Get("Loc_BtnExcludeCurrent");
            }
        }

        private void UpdateExecutableChoices()
        {
            if (CmbGameExecutable == null || SelectedGame == null || settings == null) return;
            updatingExecutables = true;
            try
            {
                var game = SelectedGame;
                var configured = settings.GetGameExecutable(game.Normalized);
                var choices = new List<ExecutableChoice> { new ExecutableChoice { Path = string.Empty, Label = LocalizationManager.Get("Loc_ExecutableAutomatic") } };
                if (!string.IsNullOrWhiteSpace(configured))
                    choices.Add(new ExecutableChoice { Path = configured, Label = System.IO.Path.GetFileName(configured) + " · " + LocalizationManager.Get("Loc_ExecutableSelected") });
                if (learningService != null)
                    foreach (var binding in learningService.GetBindings())
                    {
                        if (!string.Equals(binding.GameNormalized, game.Normalized, StringComparison.OrdinalIgnoreCase) &&
                            !(game.Game.SteamAppIdVerified && binding.SteamAppId > 0 && binding.SteamAppId == game.Game.SteamAppId)) continue;
                        if (choices.Any(x => string.Equals(x.Path, binding.Path, StringComparison.OrdinalIgnoreCase))) continue;
                        choices.Add(new ExecutableChoice { Path = binding.Path, Label = System.IO.Path.GetFileName(binding.Path) + " · " + LocalizationManager.Get("Loc_ExecutableLearned") });
                    }
                CmbGameExecutable.ItemsSource = choices;
                CmbGameExecutable.SelectedItem = choices.FirstOrDefault(x => string.Equals(x.Path, configured, StringComparison.OrdinalIgnoreCase)) ?? choices[0];
                TxtExecutablePath.Text = configured + (!string.IsNullOrWhiteSpace(configured) && !File.Exists(configured) ? "\n" + LocalizationManager.Get("Loc_ExecutableMissing") : string.Empty);
            }
            finally { updatingExecutables = false; }
        }

        private void OnGameExecutableChanged(object sender, SelectionChangedEventArgs e)
        {
            if (updatingExecutables || SelectedGame == null || settings == null) return;
            var choice = CmbGameExecutable.SelectedItem as ExecutableChoice;
            if (choice == null) return;
            settings.SetGameExecutable(SelectedGame.Normalized, choice.Path);
            settings.Save();
            UpdateExecutableChoices();
            monitorService?.ForceCheck();
        }

        private void OnBrowseGameExecutable(object sender, RoutedEventArgs e)
        {
            if (SelectedGame == null || settings == null) return;
            var picker = new OpenFileDialog { Filter = "Programs (*.exe)|*.exe", CheckFileExists = true, Multiselect = false };
            if (picker.ShowDialog(this) != true) return;
            settings.SetGameExecutable(SelectedGame.Normalized, picker.FileName);
            settings.Save();
            UpdateExecutableChoices();
            monitorService?.ForceCheck();
        }

        private void OnGameCardClicked(object sender, MouseButtonEventArgs e)
        {
            if (sender is FrameworkElement el && el.DataContext is GameItemViewModel vm)
            {
                int idx = filteredGames.IndexOf(vm);
                if (idx >= 0)
                {
                    SelectGame(idx);
                }
            }
        }

        private void OnCycleProfileLeftClick(object sender, RoutedEventArgs e)
        {
            var game = SelectedGame;
            if (game != null)
            {
                game.CycleApexProfile(-1);
                settings.SetApexProfileSlot(game.Normalized, game.SelectedApexProfileSlot);
                settings.Save();
                UpdateDetailPane();
            }
        }

        private void OnCycleProfileRightClick(object sender, RoutedEventArgs e)
        {
            var game = SelectedGame;
            if (game != null)
            {
                game.CycleApexProfile(1);
                settings.SetApexProfileSlot(game.Normalized, game.SelectedApexProfileSlot);
                settings.Save();
                UpdateDetailPane();
            }
        }

        private void OnDetailToggleExcludeClick(object sender, RoutedEventArgs e)
        {
            ToggleCurrentGameExclusion();
        }

        private void ToggleCurrentGameExclusion()
        {
            var game = SelectedGame;
            if (game == null) return;

            game.IsExcluded = !game.IsExcluded;
            // Exclusion stores both aliases. Remove/add both together,
            // otherwise the title alias keeps a game excluded forever.
            settings.SetGameExcludedAliases(
                game.Normalized, game.Title, game.IsExcluded);
            settings.Save();

            UpdateDetailPane();

            if (RadExcluded?.IsChecked == true && !game.IsExcluded)
            {
                ApplyFilter();
            }
        }

        private void OnSearchTextChanged(object sender, TextChangedEventArgs e)
        {
            bool hasText = !string.IsNullOrEmpty(TxtSearch.Text);
            TxtSearchPlaceholder.Visibility = hasText ? Visibility.Collapsed : Visibility.Visible;
            BtnClearSearch.Visibility = hasText ? Visibility.Visible : Visibility.Collapsed;
            ApplyFilter();
        }

        private void OnClearSearchClick(object sender, RoutedEventArgs e)
        {
            TxtSearch.Text = string.Empty;
        }

        private void OnFilterTabChanged(object sender, RoutedEventArgs e)
        {
            ApplyFilter();
        }

        #endregion

        #region Learned Executables

        private void LoadLearnedItems()
        {
            allLearnedViewModels.Clear();
            var bindings = learningService?.GetBindings() ?? new List<LearnedExecutableBinding>();

            foreach (var b in bindings)
            {
                allLearnedViewModels.Add(new LearnedItemViewModel(b));
            }

            ApplyLearnedFilter();
            UpdateTabTitles();
        }

        private void ApplyLearnedFilter()
        {
            string query = TxtSearchLearned?.Text?.Trim()?.ToLowerInvariant() ?? "";

            var matching = allLearnedViewModels.Where(b =>
            {
                if (string.IsNullOrEmpty(query)) return true;
                return b.GameTitle.ToLowerInvariant().Contains(query) ||
                       b.Executable.ToLowerInvariant().Contains(query) ||
                       b.Path.ToLowerInvariant().Contains(query);
            }).OrderBy(b => b.GameTitle).ToList();

            filteredLearned.Clear();
            foreach (var item in matching)
            {
                filteredLearned.Add(item);
            }

            bool hasItems = filteredLearned.Count > 0;
            if (PnlLearnedEmpty != null) PnlLearnedEmpty.Visibility = hasItems ? Visibility.Collapsed : Visibility.Visible;
            if (LstLearned != null) LstLearned.Visibility = hasItems ? Visibility.Visible : Visibility.Collapsed;

            if (TxtLearnedStats != null)
            {
                TxtLearnedStats.Text = string.Format("{0} / {1}", filteredLearned.Count, allLearnedViewModels.Count);
            }
            UpdateLearnedButtons();
        }

        private void OnSearchLearnedTextChanged(object sender, TextChangedEventArgs e)
        {
            bool hasText = !string.IsNullOrEmpty(TxtSearchLearned.Text);
            TxtSearchLearnedPlaceholder.Visibility = hasText ? Visibility.Collapsed : Visibility.Visible;
            BtnClearSearchLearned.Visibility = hasText ? Visibility.Visible : Visibility.Collapsed;
            ApplyLearnedFilter();
        }

        private void OnClearSearchLearnedClick(object sender, RoutedEventArgs e)
        {
            TxtSearchLearned.Text = string.Empty;
        }

        private void UpdateLearnedButtons()
        {
            int selectedCount = allLearnedViewModels.Count(x => x.IsSelected);
            bool hasSelection = selectedCount > 0;
            if (BtnDeleteLearned != null) BtnDeleteLearned.IsEnabled = hasSelection;
            if (BtnExportLearned != null) BtnExportLearned.IsEnabled = hasSelection || allLearnedViewModels.Count > 0;
        }

        private void OnSelectAllLearnedClick(object sender, RoutedEventArgs e)
        {
            bool allSelected = allLearnedViewModels.Count > 0 && allLearnedViewModels.All(x => x.IsSelected);
            foreach (var item in allLearnedViewModels)
            {
                item.IsSelected = !allSelected;
            }
            UpdateLearnedButtons();
        }

        private void OnDeleteLearnedClick(object sender, RoutedEventArgs e)
        {
            var selected = allLearnedViewModels.Where(x => x.IsSelected).ToArray();
            if (selected.Length == 0 || learningService == null) return;

            var result = MessageBox.Show(
                this,
                LocalizationManager.Format("Loc_LearnedDeleteConfirm", selected.Length),
                LocalizationManager.Get("Loc_LearnedWindowTitle"),
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning);

            if (result != MessageBoxResult.Yes) return;

            learningService.DeleteBindings(selected.Select(x => x.Path));
            LoadLearnedItems();
        }

        private void OnDeleteSingleLearnedItemClick(object sender, RoutedEventArgs e)
        {
            var btn = sender as FrameworkElement;
            var item = btn != null ? btn.DataContext as LearnedItemViewModel : null;
            if (item == null || learningService == null) return;

            var result = MessageBox.Show(
                this,
                LocalizationManager.Format("Loc_LearnedDeleteConfirm", 1),
                LocalizationManager.Get("Loc_LearnedWindowTitle"),
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning);

            if (result != MessageBoxResult.Yes) return;

            learningService.DeleteBindings(new[] { item.Path });
            LoadLearnedItems();
        }

        private async void OnExportLearnedClick(object sender, RoutedEventArgs e)
        {
            if (learningService == null) return;

            var selected = allLearnedViewModels.Where(x => x.IsSelected).Select(x => x.Binding).ToArray();
            if (selected.Length == 0)
            {
                selected = allLearnedViewModels.Select(x => x.Binding).ToArray();
            }
            if (selected.Length == 0) return;

            var dialog = new SaveFileDialog
            {
                Title = LocalizationManager.Get("Loc_BtnExportLearned"),
                FileName = "apexsensebridge-learned-executables.json",
                DefaultExt = ".json",
                Filter = "JSON (*.json)|*.json"
            };

            if (dialog.ShowDialog(this) != true) return;

            BtnExportLearned.IsEnabled = false;
            string error = null;
            bool success = await Task.Run(() => learningService.ExportBindings(selected, dialog.FileName, out error));
            UpdateLearnedButtons();

            MessageBox.Show(
                this,
                success
                    ? LocalizationManager.Get("Loc_LearnedExportSuccess")
                    : LocalizationManager.Format("Loc_LearnedExportFailed", error ?? string.Empty),
                LocalizationManager.Get("Loc_LearnedWindowTitle"),
                MessageBoxButton.OK,
                success ? MessageBoxImage.Information : MessageBoxImage.Error);
        }

        #endregion

        #region Settings View

        private void UpdateSettingsView()
        {
            if (settings == null) return;
            var calibration = settings.GetControllerCalibration(lastControllerStatus);
            bool canEdit = calibration != null;
            var displayed = calibration ?? new ControllerCalibration();
            updatingEffectSettings = true;
            SliderTriggerStrength.Value = displayed.TriggerStrengthPercent;
            SliderVibrationStrength.Maximum = lastControllerStatus == "apex4" || lastControllerStatus == "apex5" ? 200 : 100;
            SliderVibrationStrength.Value = displayed.VibrationStrengthPercent;
            SliderApex4GyroStrength.Value = displayed.GyroStrengthPercent;
            SliderApex4GyroYawStrength.Value = displayed.GyroYawStrengthPercent;
            SliderVibrationThreshold.Value = displayed.HapticThresholdPercent;
            ChkGripVibrations.IsChecked = displayed.EnableRumble;
            SliderTriggerStrength.IsEnabled = canEdit;
            SliderVibrationStrength.IsEnabled = canEdit;
            ChkGripVibrations.IsEnabled = canEdit;
            SliderApex4GyroStrength.IsEnabled = lastControllerStatus == "apex4";
            SliderApex4GyroYawStrength.IsEnabled = lastControllerStatus == "apex4";
            SliderVibrationThreshold.IsEnabled = canEdit && lastControllerStatus != "apex6";
            TileSettingSyncLightbar.IsEnabled = lastControllerStatus == "apex5";
            // Show only what the identified model supports; keep the locked message otherwise.
            TileSettingSyncLightbar.Visibility = TileSettingSyncLightbar.IsEnabled ? Visibility.Visible : Visibility.Collapsed;
            PnlControllerEffects.Visibility = canEdit ? Visibility.Visible : Visibility.Collapsed;
            PnlApex4GyroSettings.Visibility = lastControllerStatus == "apex4" ? Visibility.Visible : Visibility.Collapsed;
            PnlHapticThresholdSettings.Visibility = SliderVibrationThreshold.IsEnabled ? Visibility.Visible : Visibility.Collapsed;
            TxtControllerCalibrationStatus.Text = canEdit
                ? string.Format(LocalizationManager.Get("Loc_ControllerCalibrationReady"),
                    lastControllerStatus == "apex4" ? "Apex 4" : lastControllerStatus == "apex5" ? "Apex 5" : "Apex 6 Pro")
                : LocalizationManager.Get("Loc_ControllerCalibrationLocked");
            UpdateEffectLabels();
            updatingEffectSettings = false;

            UpdateSettingToggle(BadgeSettingAutoDetect, DotSettingAutoDetect, settings.AutoDetectGames);
            UpdateSettingToggle(BadgeSettingAdaptive, DotSettingAdaptive, settings.TriggerOnAdaptiveTriggers);
            UpdateSettingToggle(BadgeSettingHaptic, DotSettingHaptic, settings.TriggerOnHapticFeedback);
            UpdateSettingToggle(BadgeSettingNotifications, DotSettingNotifications, settings.EnableNotifications);
            UpdateSettingToggle(BadgeSettingSyncLightbar, DotSettingSyncLightbar, displayed.SyncLightbar);

            if (PnlSettingCriteria != null)
            {
                PnlSettingCriteria.IsEnabled = settings.AutoDetectGames;
                PnlSettingCriteria.Opacity = settings.AutoDetectGames ? 1.0 : 0.45;
            }

            UpdateLanguageDisplay();

            if (updateChecker != null && TxtVersionInfo != null)
            {
                TxtVersionInfo.Text = string.Format("ApexSenseBridge v{0}", updateChecker.GetCurrentVersion());
            }
        }

        private void UpdateEffectLabels()
        {
            if (TxtTriggerStrength == null || SliderTriggerStrength == null ||
                SliderVibrationStrength == null || SliderApex4GyroStrength == null ||
                SliderApex4GyroYawStrength == null || SliderVibrationThreshold == null) return;
            TxtTriggerStrength.Text = ((int)SliderTriggerStrength.Value) + " %";
            TxtVibrationStrength.Text = ((int)SliderVibrationStrength.Value) + " %";
            TxtApex4GyroStrength.Text = ((int)SliderApex4GyroStrength.Value) + " %";
            TxtApex4GyroYawStrength.Text = ((int)SliderApex4GyroYawStrength.Value) + " %";
            TxtVibrationThreshold.Text = ((int)SliderVibrationThreshold.Value) + " %";
        }

        private void OnEffectSettingChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (updatingEffectSettings || settings == null || !IsLoaded) return;
            lock (settings)
            {
                var calibration = settings.GetControllerCalibration(lastControllerStatus);
                if (calibration == null) return;
                // Guard handlers too (including programmatic/gamepad invocation),
                // not just visual controls. Only write the changed capability.
                if (sender == SliderTriggerStrength) calibration.TriggerStrengthPercent = (int)SliderTriggerStrength.Value;
                else if (sender == SliderVibrationStrength) calibration.VibrationStrengthPercent = (int)SliderVibrationStrength.Value;
                else if (sender == SliderApex4GyroStrength && lastControllerStatus == "apex4") calibration.GyroStrengthPercent = (int)SliderApex4GyroStrength.Value;
                else if (sender == SliderApex4GyroYawStrength && lastControllerStatus == "apex4") calibration.GyroYawStrengthPercent = (int)SliderApex4GyroYawStrength.Value;
                else if (sender == SliderVibrationThreshold && lastControllerStatus != "apex6") calibration.HapticThresholdPercent = (int)SliderVibrationThreshold.Value;
                else return;
            }
            settings.Save();
            UpdateEffectLabels();
        }

        private void OnGripVibrationsChanged(object sender, RoutedEventArgs e)
        {
            if (settings == null || updatingEffectSettings) return;
            lock (settings)
            {
                var calibration = settings.GetControllerCalibration(lastControllerStatus);
                if (calibration == null) return;
                calibration.EnableRumble = ChkGripVibrations.IsChecked == true;
            }
            settings.Save();
        }

        private void OnGripVibrationsRowClick(object sender, MouseButtonEventArgs e)
        {
            ToggleGripVibrations();
        }

        private void ToggleGripVibrations()
        {
            if (ChkGripVibrations == null || !ChkGripVibrations.IsEnabled) return;
            ChkGripVibrations.IsChecked = ChkGripVibrations.IsChecked != true;
            OnGripVibrationsChanged(ChkGripVibrations, null);
        }

        private void UpdateLanguageDisplay()
        {
            if (TxtCurrentLanguage != null)
            {
                TxtCurrentLanguage.Text = LanguageCatalog.GetNativeName(LocalizationManager.CurrentLanguage);
            }
        }

        private void OnLanguagePickerClick(object sender, MouseButtonEventArgs e)
        {
            OpenLanguagePicker();
        }

        private void OpenLanguagePicker()
        {
            gamepadNav?.Stop();
            try
            {
                var picker = new LanguagePickerWindow(LocalizationManager.CurrentLanguage) { Owner = this };
                if (picker.ShowDialog() == true && !string.IsNullOrWhiteSpace(picker.SelectedLanguage))
                    SwitchLanguage(picker.SelectedLanguage);
            }
            finally { gamepadNav?.Start(); }
        }

        private void OnLearnedItemClicked(object sender, MouseButtonEventArgs e)
        {
            var element = sender as FrameworkElement;
            var item = element != null ? element.DataContext as LearnedItemViewModel : null;
            if (item != null)
            {
                int index = filteredLearned.IndexOf(item);
                if (index >= 0) SetLearnedNav(index);
                item.IsSelected = !item.IsSelected;
                UpdateLearnedButtons();
            }
        }

        private void UpdateSettingToggle(Border badge, Ellipse dot, bool isChecked)
        {
            if (badge == null || dot == null) return;
            badge.Background = isChecked ? (Brush)FindResource("PlayStationBlue") : (Brush)FindResource("ControlBackground");
            badge.BorderBrush = isChecked ? (Brush)FindResource("PlayStationBlue") : (Brush)FindResource("ControlBorder");
            dot.HorizontalAlignment = isChecked ? HorizontalAlignment.Right : HorizontalAlignment.Left;
            dot.Margin = isChecked ? new Thickness(0, 0, 3, 0) : new Thickness(3, 0, 0, 0);
            dot.Fill = isChecked ? Brushes.White : (Brush)FindResource("TextMuted");
        }

        private void OnSettingAutoDetectToggled(object sender, MouseButtonEventArgs e)
        {
            if (settings == null) return;
            settings.AutoDetectGames = !settings.AutoDetectGames;
            settings.Save();
            UpdateSettingsView();
            if (settings.AutoDetectGames)
            {
                monitorService?.ForceCheck();
            }
            else if (sessionManager != null && sessionManager.IsSessionActive && settings.ForcedProfile == "none")
            {
                sessionManager.StopSession("Auto-detect disabled");
            }
            UpdateDashboardStatus();
        }

        private void OnSettingAdaptiveToggled(object sender, MouseButtonEventArgs e)
        {
            if (settings == null || !settings.AutoDetectGames) return;
            settings.TriggerOnAdaptiveTriggers = !settings.TriggerOnAdaptiveTriggers;
            settings.Save();
            UpdateSettingsView();
            monitorService?.ForceCheck();
        }

        private void OnSettingHapticToggled(object sender, MouseButtonEventArgs e)
        {
            if (settings == null || !settings.AutoDetectGames) return;
            settings.TriggerOnHapticFeedback = !settings.TriggerOnHapticFeedback;
            settings.Save();
            UpdateSettingsView();
            monitorService?.ForceCheck();
        }

        private void OnSettingNotificationsToggled(object sender, MouseButtonEventArgs e)
        {
            if (settings == null) return;
            settings.EnableNotifications = !settings.EnableNotifications;
            settings.Save();
            UpdateSettingsView();
        }

        private void OnSettingSyncLightbarToggled(object sender, MouseButtonEventArgs e)
        {
            if (settings == null || lastControllerStatus != "apex5") return;
            lock (settings)
            {
                var calibration = settings.GetControllerCalibration(lastControllerStatus);
                calibration.SyncLightbar = !calibration.SyncLightbar;
            }
            settings.Save();
            UpdateSettingsView();
        }

        private void OnSettingLanguageEnChecked(object sender, RoutedEventArgs e)
        {
            SwitchLanguage(LocalizationManager.LangEnglish);
        }

        private void OnSettingLanguageFrChecked(object sender, RoutedEventArgs e)
        {
            SwitchLanguage(LocalizationManager.LangFrench);
        }

        private void OnSettingLanguageEsChecked(object sender, RoutedEventArgs e)
        {
            SwitchLanguage(LocalizationManager.LangSpanish);
        }

        private void OnSettingLanguageZhChecked(object sender, RoutedEventArgs e)
        {
            SwitchLanguage(LocalizationManager.LangChinese);
        }

        private void SwitchLanguage(string lang)
        {
            if (settings != null)
            {
                settings.Language = lang;
                settings.Save();
            }
            LocalizationManager.SetLanguage(lang);
            UpdateLanguageDisplay();
            UpdateControllerStatus(lastControllerStatus);
        }

        private void OnOpenControllerTestClick(object sender, RoutedEventArgs e)
        {
            var win = new ControllerTestWindow(settings);
            win.Owner = this;
            win.ShowDialog();
        }

        #region First-run support callout

        private System.Windows.Threading.DispatcherTimer supportHintTimer;

        private void ScheduleSupportHint()
        {
            if (settings == null || settings.SupportHintShown || SupportHintLayer == null) return;
            supportHintTimer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(1.2) };
            supportHintTimer.Tick += (s, e) =>
            {
                supportHintTimer.Stop();
                ShowSupportHint();
            };
            supportHintTimer.Start();
        }

        private void ShowSupportHint()
        {
            if (!IsLoaded || SupportHintLayer == null || BtnSupportKofi == null) return;
            var host = SupportHintLayer.Parent as FrameworkElement;
            if (host == null) return;

            // Anchor under the support pill: right edges aligned, arrow under the pill centre.
            Point origin = BtnSupportKofi.TranslatePoint(new Point(0, 0), host);
            double right = host.ActualWidth - (origin.X + BtnSupportKofi.ActualWidth);
            SupportHintLayer.Margin = new Thickness(0, origin.Y + BtnSupportKofi.ActualHeight + 4, Math.Max(0, right), 0);
            SupportHintArrow.Margin = new Thickness(0, 0, Math.Max(12, BtnSupportKofi.ActualWidth / 2 - 10), 0);

            SupportHintLayer.Visibility = Visibility.Visible;
            var ease = new System.Windows.Media.Animation.CubicEase { EasingMode = System.Windows.Media.Animation.EasingMode.EaseOut };
            SupportHintLayer.BeginAnimation(OpacityProperty, new System.Windows.Media.Animation.DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(260)) { EasingFunction = ease });
            SupportHintSlide.BeginAnimation(TranslateTransform.YProperty, new System.Windows.Media.Animation.DoubleAnimation(-10, 0, TimeSpan.FromMilliseconds(260)) { EasingFunction = ease });

            // Recorded as soon as it is shown: the callout never comes back, even after a crash.
            settings.SupportHintShown = true;
            settings.Save();

            supportHintTimer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(20) };
            supportHintTimer.Tick += (s, e) =>
            {
                supportHintTimer.Stop();
                HideSupportHint();
            };
            supportHintTimer.Start();
        }

        /// <summary>Hides the callout if it is open. Returns true when something was dismissed.</summary>
        private bool HideSupportHint()
        {
            if (SupportHintLayer == null || SupportHintLayer.Visibility != Visibility.Visible) return false;
            if (supportHintTimer != null) supportHintTimer.Stop();
            var fade = new System.Windows.Media.Animation.DoubleAnimation(1, 0, TimeSpan.FromMilliseconds(180));
            fade.Completed += (s, e) =>
            {
                SupportHintLayer.Visibility = Visibility.Collapsed;
                SupportHintLayer.BeginAnimation(OpacityProperty, null);
            };
            SupportHintLayer.BeginAnimation(OpacityProperty, fade);
            return true;
        }

        private void OnSupportHintLaterClick(object sender, RoutedEventArgs e)
        {
            HideSupportHint();
        }

        private void OnSupportHintSupportClick(object sender, RoutedEventArgs e)
        {
            OnSupportKofiClick(sender, e); // also dismisses the callout
        }

        #endregion

        private void OnSupportKofiClick(object sender, RoutedEventArgs e)
        {
            HideSupportHint();
            try
            {
                Process.Start(new ProcessStartInfo("https://ko-fi.com/reynarts97")
                {
                    UseShellExecute = true
                });
            }
            catch
            {
                MessageBox.Show(
                    this,
                    LocalizationManager.Get("Loc_ExternalLinkOpenFailed"),
                    LocalizationManager.Get("Loc_AppName"),
                    MessageBoxButton.OK,
                    MessageBoxImage.Warning);
            }
        }

        private void OnReportBugClick(object sender, RoutedEventArgs e)
        {
            gamepadNav?.Stop();
            try
            {
                var win = new BugReportWindow(settings, sessionManager, lastControllerStatus)
                {
                    Owner = this
                };
                win.ShowDialog();
            }
            finally
            {
                gamepadNav?.Start();
            }
        }

        private async void OnCheckUpdatesClick(object sender, RoutedEventArgs e)
        {
            if (updateChecker == null) return;
            if (BtnCheckUpdates != null) BtnCheckUpdates.IsEnabled = false;
            if (TxtUpdateStatus != null) TxtUpdateStatus.Text = LocalizationManager.Get("Loc_SoftwareUpdate") + "…";

            try
            {
                var info = await updateChecker.CheckForUpdatesAsync(false);
                if (info != null && info.HasUpdate)
                {
                    if (TxtUpdateStatus != null)
                    {
                        TxtUpdateStatus.Text = LocalizationManager.Format("Loc_UpdateAvailableBadge", info.LatestVersion);
                        TxtUpdateStatus.Foreground = (Brush)FindResource("BadgeActiveFg");
                    }
                }
                else
                {
                    if (TxtUpdateStatus != null)
                    {
                        TxtUpdateStatus.Text = LocalizationManager.Get("Loc_UpdateUpToDate");
                    }
                }
            }
            catch
            {
                if (TxtUpdateStatus != null)
                {
                    TxtUpdateStatus.Text = LocalizationManager.Get("Loc_UpdateCheckUnavailable");
                }
            }
            finally
            {
                if (BtnCheckUpdates != null) BtnCheckUpdates.IsEnabled = true;
            }
        }

        #endregion

        #region Window Controls

        private void UpdateTabTitles()
        {
            if (TxtNavCertifiedCount != null) TxtNavCertifiedCount.Text = allGameViewModels.Count.ToString();
            if (TxtNavLearnedCount != null) TxtNavLearnedCount.Text = allLearnedViewModels.Count.ToString();
            if (TxtDashCountGames != null) TxtDashCountGames.Text = allGameViewModels.Count.ToString();
            if (TxtDashCountLearned != null) TxtDashCountLearned.Text = allLearnedViewModels.Count.ToString();
            UpdateShelfTitle();
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed)
            {
                DragMove();
            }
        }

        private void OnMinimizeClick(object sender, RoutedEventArgs e)
        {
            WindowState = WindowState.Minimized;
        }

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            Close();
        }

        protected override void OnClosed(EventArgs e)
        {
            statusTimer.Stop();
            controllerDetection?.Dispose();
            gamepadNav?.Dispose();
            base.OnClosed(e);
        }

        #endregion
    }

    public sealed class LearnedItemViewModel : INotifyPropertyChanged
    {
        public LearnedExecutableBinding Binding { get; private set; }
        public string GameTitle => !string.IsNullOrWhiteSpace(Binding.GameTitle) ? Binding.GameTitle : Binding.GameNormalized;
        public string Executable => Binding.Executable;
        public string Path => Binding.Path;
        public string DetectionMethod => !string.IsNullOrWhiteSpace(Binding.DetectionMethod) ? Binding.DetectionMethod : LocalizationManager.Get("Loc_DetectionMethodAuto");
        public string SessionsDisplay => LocalizationManager.Format(Binding.SuccessfulSessions > 1 ? "Loc_SessionCountPlural" : "Loc_SessionCountSingular", Binding.SuccessfulSessions);
        public string LastSeenDisplay { get; private set; }

        private bool isSelected;
        public bool IsSelected
        {
            get => isSelected;
            set
            {
                if (isSelected != value)
                {
                    isSelected = value;
                    OnPropertyChanged("IsSelected");
                }
            }
        }

        private bool isGamepadFocused;
        public bool IsGamepadFocused
        {
            get => isGamepadFocused;
            set
            {
                if (isGamepadFocused != value)
                {
                    isGamepadFocused = value;
                    OnPropertyChanged("IsGamepadFocused");
                }
            }
        }

        public LearnedItemViewModel(LearnedExecutableBinding binding)
        {
            Binding = binding;

            DateTime lastSeen;
            LastSeenDisplay = DateTime.TryParse(
                binding.LastSeenUtc,
                CultureInfo.InvariantCulture,
                DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal,
                out lastSeen)
                ? lastSeen.ToLocalTime().ToString("g", CultureInfo.CurrentCulture)
                : binding.LastSeenUtc;
        }

        public event PropertyChangedEventHandler PropertyChanged;
        private void OnPropertyChanged(string prop) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(prop));
    }

    public class GameItemViewModel : INotifyPropertyChanged
    {
        private SupportedGame game;
        public SupportedGame Game
        {
            get => game;
            set
            {
                game = value;
                InitCover();
            }
        }

        public string Title => Game != null ? Game.Title : string.Empty;
        public string Normalized => Game != null ? Game.Normalized : string.Empty;
        public bool AdaptiveTriggers => Game != null && Game.AdaptiveTriggers;
        public bool HapticFeedback => Game != null && Game.HapticFeedback;
        public bool RequiresManualFix => Game != null && Game.RequiresManualFix;
        public bool HasReadyFeature => Game != null &&
            ((Game.AdaptiveTriggers && !Game.AdaptiveTriggersManualFix) ||
             (Game.HapticFeedback && !Game.HapticFeedbackManualFix));
        public string Profile => Game != null ? Game.Profile : "standard";
        public string IconUrl => Game != null ? Game.IconUrl : string.Empty;
        public DateTime? AddedAt => Game != null ? Game.AddedAt : null;

        private bool isRecentlyAdded;
        /// <summary>Added to the catalogue after its initial import and recently enough to be flagged.</summary>
        public bool IsRecentlyAdded
        {
            get => isRecentlyAdded;
            set
            {
                if (isRecentlyAdded == value) return;
                isRecentlyAdded = value;
                OnPropertyChanged("IsRecentlyAdded");
                OnPropertyChanged("NewBadgeVisibility");
            }
        }
        public Visibility NewBadgeVisibility => IsRecentlyAdded ? Visibility.Visible : Visibility.Collapsed;
        public string AddedTooltip => AddedAt.HasValue
            ? LocalizationManager.Format("Loc_AddedOn", AddedAt.Value.ToString("d", CultureInfo.CurrentCulture))
            : null;

        private ImageSource coverImage;
        public ImageSource CoverImage
        {
            get => coverImage;
            private set
            {
                if (coverImage != value)
                {
                    coverImage = value;
                    OnPropertyChanged("CoverImage");
                    OnPropertyChanged("HasCoverImage");
                    OnPropertyChanged("IconVisibility");
                    OnPropertyChanged("PlaceholderVisibility");
                }
            }
        }

        public bool HasCoverImage => CoverImage != null;
        public Visibility IconVisibility => HasCoverImage ? Visibility.Visible : Visibility.Collapsed;
        public Visibility PlaceholderVisibility => HasCoverImage ? Visibility.Collapsed : Visibility.Visible;

        public string Initials
        {
            get
            {
                if (string.IsNullOrWhiteSpace(Title)) return "?";
                var parts = Title.Split(new[] { ' ', ':', '-', '\'', '’' }, StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length >= 2)
                    return (parts[0].Substring(0, 1) + parts[1].Substring(0, 1)).ToUpperInvariant();
                return Title.Length >= 2 ? Title.Substring(0, 2).ToUpperInvariant() : Title.ToUpperInvariant();
            }
        }

        public Brush MonogramBackground
        {
            get
            {
                int hash = Math.Abs((Title ?? "game").GetHashCode());
                var palette = new[]
                {
                    new SolidColorBrush(Color.FromRgb(26, 47, 85)),
                    new SolidColorBrush(Color.FromRgb(20, 60, 50)),
                    new SolidColorBrush(Color.FromRgb(60, 25, 65)),
                    new SolidColorBrush(Color.FromRgb(70, 30, 30)),
                    new SolidColorBrush(Color.FromRgb(24, 52, 70)),
                    new SolidColorBrush(Color.FromRgb(40, 40, 55))
                };
                var brush = palette[hash % palette.Length];
                brush.Freeze();
                return brush;
            }
        }

        private void InitCover()
        {
            if (string.IsNullOrWhiteSpace(IconUrl)) return;
            CoverImage = CoverCacheService.GetImage(IconUrl, loadedImage =>
            {
                CoverImage = loadedImage;
            });
        }

        public bool HasCustomRemapping
        {
            get
            {
                if (Game == null || string.IsNullOrWhiteSpace(Game.Profile)) return false;
                string p = Game.Profile.ToLowerInvariant();
                return p != "standard" && p != "none" && p != "default";
            }
        }

        public Visibility AdaptiveVisibility => AdaptiveTriggers ? Visibility.Visible : Visibility.Collapsed;
        public Visibility HapticVisibility => HapticFeedback ? Visibility.Visible : Visibility.Collapsed;
        public Visibility ManualFixVisibility => RequiresManualFix ? Visibility.Visible : Visibility.Collapsed;
        public Visibility RemappingVisibility => HasCustomRemapping ? Visibility.Visible : Visibility.Collapsed;

        public string SelectedApexProfileDisplay
        {
            get
            {
                if (SelectedApexProfileSlot == 0) return LocalizationManager.Get("Loc_ApexProfileKeep");
                return LocalizationManager.Format("Loc_ApexProfileNumber", SelectedApexProfileSlot);
            }
        }

        private int selectedApexProfileSlot;
        public int SelectedApexProfileSlot
        {
            get => selectedApexProfileSlot;
            set
            {
                int normalized = value >= 1 && value <= 4 ? value : 0;
                if (selectedApexProfileSlot != normalized)
                {
                    selectedApexProfileSlot = normalized;
                    OnPropertyChanged("SelectedApexProfileSlot");
                    OnPropertyChanged("SelectedApexProfileDisplay");
                }
            }
        }

        public void CycleApexProfile(int delta)
        {
            int next = SelectedApexProfileSlot + delta;
            if (next < 0) next = 4;
            else if (next > 4) next = 0;
            SelectedApexProfileSlot = next;
        }

        public void RefreshLocalization()
        {
            OnPropertyChanged("SelectedApexProfileSlot");
            OnPropertyChanged("SelectedApexProfileDisplay");
        }

        private bool isSelected;
        public bool IsSelected
        {
            get => isSelected;
            set
            {
                if (isSelected != value)
                {
                    isSelected = value;
                    OnPropertyChanged("IsSelected");
                }
            }
        }

        private bool isExcluded;
        public bool IsExcluded
        {
            get => isExcluded;
            set
            {
                if (isExcluded != value)
                {
                    isExcluded = value;
                    OnPropertyChanged("IsExcluded");
                    OnPropertyChanged("CardOpacity");
                    OnPropertyChanged("ExcludedBadgeVisibility");
                }
            }
        }

        public Visibility ExcludedBadgeVisibility => IsExcluded ? Visibility.Visible : Visibility.Collapsed;
        public double CardOpacity => IsExcluded ? 0.55 : 1.0;

        public event PropertyChangedEventHandler PropertyChanged;
        protected void OnPropertyChanged(string name) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
