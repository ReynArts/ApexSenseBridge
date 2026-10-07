using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using System;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Threading;

namespace ApexSenseBridgeTray
{
    public partial class ControllerTestWindow : Window
    {
        private readonly TraySettings settings;
        private readonly ControllerTestService testService;
        private readonly DispatcherTimer pollTimer;
        private byte currentR = 0;
        private byte currentG = 55;
        private byte currentB = 145;

        private double pitchCalibrationOffset = 0.0;
        private double rollCalibrationOffset = 0.0;
        private double lastRawPitch = 0.0;
        private double lastRawRoll = 0.0;
        private CancellationTokenSource latencyTestCancellation;

        private readonly SolidColorBrush activeBtnBrush = new SolidColorBrush(Color.FromRgb(41, 121, 255));
        private readonly SolidColorBrush defaultBtnBrush = new SolidColorBrush(Color.FromRgb(37, 41, 52));
        private readonly SolidColorBrush defaultBdrBrush = new SolidColorBrush(Color.FromRgb(58, 64, 80));

        public ControllerTestWindow(TraySettings settings)
        {
            this.settings = settings ?? TraySettings.Load();
            InitializeComponent();

            testService = new ControllerTestService();

            pollTimer = new DispatcherTimer(DispatcherPriority.Render)
            {
                Interval = TimeSpan.FromMilliseconds(16) // ~60 FPS
            };
            pollTimer.Tick += OnPollTick;

            Loaded += OnWindowLoaded;
            Closed += OnWindowClosed;
            IsVisibleChanged += OnWindowVisibleChanged;
            Activated += OnWindowActivated;
            Deactivated += OnWindowDeactivated;

            UpdatePersistenceView();
        }

        private void OnWindowLoaded(object sender, RoutedEventArgs e)
        {
            GamepadNavigationService.IsNavigationSuspended = true;
            pollTimer.Start();
        }

        private void OnWindowClosed(object sender, EventArgs e)
        {
            GamepadNavigationService.IsNavigationSuspended = false;
            pollTimer.Stop();
            CancelLatencyTest();
            testService.StopGyroStream();
            testService.Dispose();
        }

        private void OnWindowActivated(object sender, EventArgs e)
        {
            GamepadNavigationService.IsNavigationSuspended = true;
        }

        private void OnWindowVisibleChanged(object sender, DependencyPropertyChangedEventArgs e)
        {
            if (!IsVisible)
            {
                GamepadNavigationService.IsNavigationSuspended = false;
                pollTimer.Stop();
                CancelLatencyTest();
                testService.KillActiveTestProcess();
                testService.StopGyroStream();
            }
            else
            {
                GamepadNavigationService.IsNavigationSuspended = true;
                if (!pollTimer.IsEnabled) pollTimer.Start();
                if (TabGyro != null && TabGyro.IsChecked == true) StartGyroStream();
            }
        }

        private void OnWindowDeactivated(object sender, EventArgs e)
        {
            GamepadNavigationService.IsNavigationSuspended = false;
            CancelLatencyTest();
            testService.KillActiveTestProcess();
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed)
            {
                DragMove();
            }
        }

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void OnPollTick(object sender, EventArgs e)
        {
            if (!IsVisible) return;

            var state = testService.PollInputState();

            // Status badge
            if (state.Connected)
            {
                BadgeControllerStatus.Background = new SolidColorBrush(Color.FromArgb(50, 46, 187, 79));
                TxtControllerStatus.Foreground = new SolidColorBrush(Color.FromRgb(46, 187, 79));
                TxtControllerStatus.Text = "● " + LocalizationManager.Get("Loc_ControllerConnectedBadge");
            }
            else
            {
                BadgeControllerStatus.Background = (Brush)FindResource("BadgeStandbyBg");
                TxtControllerStatus.Foreground = (Brush)FindResource("BadgeStandbyFg");
                TxtControllerStatus.Text = "○ " + LocalizationManager.Get("Loc_ControllerDisconnectedBadge");
            }

            // Triggers L2 / R2: travel gauges under the controller + overlay glow on the drawing
            FillL2.Height = TriggerGaugeHeight * state.LeftTrigger / 255.0;
            TxtL2Val.Text = string.Format("L2 {0}%", (int)Math.Round(state.LeftTrigger / 2.55));
            OverlayL2.Opacity = state.LeftTrigger / 255.0;
            OverlayL1.Visibility = state.L1 ? Visibility.Visible : Visibility.Collapsed;

            FillR2.Height = TriggerGaugeHeight * state.RightTrigger / 255.0;
            TxtR2Val.Text = string.Format("R2 {0}%", (int)Math.Round(state.RightTrigger / 2.55));
            OverlayR2.Opacity = state.RightTrigger / 255.0;
            OverlayR1.Visibility = state.R1 ? Visibility.Visible : Visibility.Collapsed;
            // D-Pad
            OverlayUp.Visibility = state.DpadUp ? Visibility.Visible : Visibility.Collapsed;
            OverlayDown.Visibility = state.DpadDown ? Visibility.Visible : Visibility.Collapsed;
            OverlayLeft.Visibility = state.DpadLeft ? Visibility.Visible : Visibility.Collapsed;
            OverlayRight.Visibility = state.DpadRight ? Visibility.Visible : Visibility.Collapsed;

            // Action Buttons
            OverlayCross.Visibility = state.Cross ? Visibility.Visible : Visibility.Collapsed;
            OverlayCircle.Visibility = state.Circle ? Visibility.Visible : Visibility.Collapsed;
            OverlaySquare.Visibility = state.Square ? Visibility.Visible : Visibility.Collapsed;
            OverlayTriangle.Visibility = state.Triangle ? Visibility.Visible : Visibility.Collapsed;

            // System buttons
            OverlayCreate.Visibility = state.Create ? Visibility.Visible : Visibility.Collapsed;
            OverlayOptions.Visibility = state.Options ? Visibility.Visible : Visibility.Collapsed;
            OverlayPS.Visibility = state.PsButton ? Visibility.Visible : Visibility.Collapsed;
            OverlayTouchpad.Opacity = state.TouchpadClick ? 1.0 : 0.85;

            // Thumbsticks (in 896x512 drawing space) and precise pads
            UpdateStick(state.ThumbLX / 32768.0, state.ThumbLY / 32768.0, state.L3,
                TransStickL, TrailStickL, BorderStickL, GlowStickL, TxtStickL, TransDotL, PadTrailL, TxtTelemetryStickL);
            UpdateStick(state.ThumbRX / 32768.0, state.ThumbRY / 32768.0, state.R3,
                TransStickR, TrailStickR, BorderStickR, GlowStickR, TxtStickR, TransDotR, PadTrailR, TxtTelemetryStickR);
            HandleGamepadShortcuts(state);
        }

        #region Stick rendering

        private const double TriggerGaugeHeight = 88.0;
        private const double StickTravel = 24.0;   // cap travel inside its well (drawing pixels)
        private const double PadRadius = 37.0;     // dot travel inside the precise pad

        private static readonly Brush CapIdleFill = Frozen(Color.FromRgb(0x16, 0x20, 0x3A));
        private static readonly Brush CapPressedFill = Frozen(Color.FromRgb(0x1E, 0x6F, 0xE8));
        private static readonly Brush CapIdleStroke = Frozen(Color.FromRgb(0xAE, 0xB9, 0xD3));
        private static readonly Brush CapActiveStroke = Frozen(Color.FromRgb(0xE6, 0xEC, 0xFA));
        private static readonly Brush LabelIdle = Frozen(Color.FromRgb(0x7F, 0x8D, 0xAD));
        private static readonly Brush LabelActive = Frozen(Color.FromRgb(0xC9, 0xD5, 0xEE));

        private static Brush Frozen(Color color)
        {
            var brush = new SolidColorBrush(color);
            brush.Freeze();
            return brush;
        }

        private static void UpdateStick(double x, double y, bool pressed,
            TranslateTransform cap, System.Windows.Shapes.Line trail, Border capBorder, DropShadowEffect glow, TextBlock label,
            TranslateTransform dot, System.Windows.Shapes.Line padTrail, TextBlock readout)
        {
            // Square-ish raw ranges are clamped to the unit circle so the cap never leaves its well.
            double magnitude = Math.Sqrt(x * x + y * y);
            double scale = magnitude > 1.0 ? 1.0 / magnitude : 1.0;
            double cx = x * scale, cy = y * scale;
            double strength = Math.Min(1.0, magnitude);
            bool deflected = strength > 0.08;

            cap.X = cx * StickTravel;
            cap.Y = -cy * StickTravel;
            trail.X2 = trail.X1 + cap.X;
            trail.Y2 = trail.Y1 + cap.Y;
            trail.Opacity = deflected ? 0.35 + 0.55 * strength : 0.0;
            glow.Opacity = pressed ? 1.0 : (deflected ? 0.2 + 0.45 * strength : 0.0);
            capBorder.Background = pressed ? CapPressedFill : CapIdleFill;
            capBorder.BorderBrush = pressed ? Brushes.White : (deflected ? CapActiveStroke : CapIdleStroke);
            label.Foreground = pressed ? Brushes.White : (deflected ? LabelActive : LabelIdle);

            dot.X = cx * PadRadius;
            dot.Y = -cy * PadRadius;
            padTrail.X2 = padTrail.X1 + dot.X;
            padTrail.Y2 = padTrail.Y1 + dot.Y;
            readout.Text = string.Format(CultureInfo.InvariantCulture, "X {0,4}   Y {1,4}",
                (int)Math.Round(x * 100), (int)Math.Round(y * 100));
        }

        #endregion

        #region Gamepad Shortcuts

        // Every button is under test in this window, so navigation stays suspended and only
        // non-destructive shortcuts are used: LB/RB switch tests, Menu runs the current test,
        // and B must be held to close (a short press is still just a tested button).
        private const double HoldToCloseSeconds = 1.0;
        private bool lastShoulderLeft;
        private bool lastShoulderRight;
        private bool lastMenu;
        private DateTime? backHoldStart;

        private void HandleGamepadShortcuts(DualSenseVisualState state)
        {
            bool active = IsActive && state.Connected;
            // A running latency measurement needs free stick/button input: never interrupt it.
            bool busy = latencyTestCancellation != null;

            if (active && !busy)
            {
                if (state.L1 && !lastShoulderLeft) CycleTestTab(-1);
                if (state.R1 && !lastShoulderRight) CycleTestTab(1);
                if (state.Options && !lastMenu) RunCurrentTest();
            }
            lastShoulderLeft = state.L1;
            lastShoulderRight = state.R1;
            lastMenu = state.Options;

            double progress = 0;
            if (active && !busy && state.Circle)
            {
                if (!backHoldStart.HasValue) backHoldStart = DateTime.UtcNow;
                progress = Math.Min(1.0, (DateTime.UtcNow - backHoldStart.Value).TotalSeconds / HoldToCloseSeconds);
            }
            else
            {
                backHoldStart = null;
            }
            UpdateHoldCloseArc(progress);
            if (progress >= 1.0)
            {
                backHoldStart = null;
                Close();
            }
        }

        private RadioButton[] GetTestTabs()
        {
            return new[] { TabTriggers, TabVibration, TabRgb, TabGyro, TabLatency, TabMapping };
        }

        private void CycleTestTab(int delta)
        {
            var tabs = GetTestTabs();
            int current = Array.FindIndex(tabs, t => t.IsChecked == true);
            int next = ((current < 0 ? 0 : current) + delta + tabs.Length) % tabs.Length;
            tabs[next].IsChecked = true;
        }

        private void RunCurrentTest()
        {
            Button primary = null;
            if (TabTriggers.IsChecked == true) primary = BtnApplyTrigger;
            else if (TabVibration.IsChecked == true) primary = BtnTestVibePulse;
            else if (TabRgb.IsChecked == true) primary = BtnApplyRgb;
            else if (TabGyro.IsChecked == true) primary = BtnTestGyro;
            else if (TabLatency.IsChecked == true) primary = BtnTestNativeLatency;
            else if (TabMapping.IsChecked == true) primary = BtnVerifyPersistence;
            if (primary != null && primary.IsEnabled) primary.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        }

        // Circular progress around the B glyph while the button is held.
        private void UpdateHoldCloseArc(double progress)
        {
            if (ArcHoldClose == null) return;
            if (progress <= 0) { ArcHoldClose.Data = null; return; }
            const double radius = 10, center = 11;
            double angle = Math.Min(progress, 0.999) * 2 * Math.PI;
            var end = new Point(center + radius * Math.Sin(angle), center - radius * Math.Cos(angle));
            var figure = new PathFigure { StartPoint = new Point(center, center - radius) };
            figure.Segments.Add(new ArcSegment(end, new Size(radius, radius), 0, angle > Math.PI, SweepDirection.Clockwise, true));
            ArcHoldClose.Data = new PathGeometry(new[] { figure });
        }

        #endregion

        #region Sub-Tabs Navigation

        private void OnTestSubTabChanged(object sender, RoutedEventArgs e)
        {
            if (PanelTriggers == null || PanelVibration == null || PanelRgb == null ||
                PanelMapping == null || PanelGyro == null || PanelLatency == null) return;

            PanelTriggers.Visibility = TabTriggers.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
            PanelVibration.Visibility = TabVibration.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
            PanelRgb.Visibility = TabRgb.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
            PanelMapping.Visibility = TabMapping.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
            PanelGyro.Visibility = TabGyro.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
            PanelLatency.Visibility = TabLatency.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;

            if (TabGyro.IsChecked == true)
            {
                StartGyroStream();
            }
            else
            {
                testService.StopGyroStream();
            }

            if (TabLatency.IsChecked != true)
            {
                CancelLatencyTest();
            }
        }

        #endregion

        #region Adaptive Triggers Test

        private async void OnApplyTriggerClick(object sender, RoutedEventArgs e)
        {
            string side = "both";
            if (RadSideLt.IsChecked == true) side = "lt";
            else if (RadSideRt.IsChecked == true) side = "rt";

            string mode = "resistance";
            if (RadModeWeapon.IsChecked == true) mode = "weapon";
            else if (RadModeVibration.IsChecked == true) mode = "vibration";
            else if (RadModeBow.IsChecked == true) mode = "bow";

            int level = 2;
            if (RadLevel1.IsChecked == true) level = 1;
            else if (RadLevel3.IsChecked == true) level = 3;
            else if (RadLevel4.IsChecked == true) level = 4;

            TxtTestFeedback.Text = string.Format(LocalizationManager.Get("Loc_TestingTriggerStatus"), side.ToUpperInvariant(), mode, level);
            BtnApplyTrigger.IsEnabled = false;

            try
            {
                bool ok = await testService.TestTriggerAsync(side, mode, level, 3);
                TxtTestFeedback.Text = ok ? LocalizationManager.Get("Loc_TriggerTestSuccess") : LocalizationManager.Get("Loc_TriggerTestFailed");
            }
            catch (Exception ex)
            {
                TxtTestFeedback.Text = "Erreur : " + ex.Message;
            }
            finally
            {
                BtnApplyTrigger.IsEnabled = true;
            }
        }

        private async void OnResetTriggerClick(object sender, RoutedEventArgs e)
        {
            TxtTestFeedback.Text = LocalizationManager.Get("Loc_ResettingTriggers");
            await testService.ResetAllEffectsAsync();
            TxtTestFeedback.Text = LocalizationManager.Get("Loc_TriggersNormal");
        }

        #endregion

        #region Vibration (Rumble) Test

        private void OnMotorSliderChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (TxtValMotorL == null || TxtValMotorR == null) return;
            TxtValMotorL.Text = string.Format("{0}%", (int)(SliderMotorL.Value / 2.55));
            TxtValMotorR.Text = string.Format("{0}%", (int)(SliderMotorR.Value / 2.55));
        }

        private void OnPresetVibeLightClick(object sender, RoutedEventArgs e)
        {
            SliderMotorL.Value = 75;
            SliderMotorR.Value = 60;
        }

        private void OnPresetVibeMedClick(object sender, RoutedEventArgs e)
        {
            SliderMotorL.Value = 140;
            SliderMotorR.Value = 120;
        }

        private void OnPresetVibeFullClick(object sender, RoutedEventArgs e)
        {
            SliderMotorL.Value = 255;
            SliderMotorR.Value = 255;
        }

        private async void OnTestVibePulseClick(object sender, RoutedEventArgs e)
        {
            int left = (int)SliderMotorL.Value;
            int right = (int)SliderMotorR.Value;

            TxtTestFeedback.Text = string.Format(LocalizationManager.Get("Loc_TestingRumbleStatus"), left, right);
            BtnTestVibePulse.IsEnabled = false;

            try
            {
                bool ok = await testService.TestRumbleAsync(left, right, 1);
                TxtTestFeedback.Text = ok ? LocalizationManager.Get("Loc_RumbleTestSuccess") : LocalizationManager.Get("Loc_RumbleTestFailed");
            }
            catch (Exception ex)
            {
                TxtTestFeedback.Text = "Erreur : " + ex.Message;
            }
            finally
            {
                BtnTestVibePulse.IsEnabled = true;
            }
        }

        private async void OnStopVibeClick(object sender, RoutedEventArgs e)
        {
            TxtTestFeedback.Text = LocalizationManager.Get("Loc_StoppingVibration");
            await testService.ResetAllEffectsAsync();
            TxtTestFeedback.Text = LocalizationManager.Get("Loc_VibrationStopped");
        }

        #endregion

        #region LightSync RGB Test

        private void OnColorPresetClick(object sender, RoutedEventArgs e)
        {
            var btn = sender as Button;
            if (btn == null || btn.Tag == null) return;

            string hex = btn.Tag.ToString();
            try
            {
                var color = (Color)ColorConverter.ConvertFromString(hex);
                SliderR.Value = color.R;
                SliderG.Value = color.G;
                SliderB.Value = color.B;
                UpdateColorPreview(color);
            }
            catch { }
        }

        private void OnRgbSliderChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (TxtRgbR == null || TxtRgbG == null || TxtRgbB == null || BoxColorPreview == null) return;

            currentR = (byte)SliderR.Value;
            currentG = (byte)SliderG.Value;
            currentB = (byte)SliderB.Value;

            TxtRgbR.Text = currentR.ToString();
            TxtRgbG.Text = currentG.ToString();
            TxtRgbB.Text = currentB.ToString();

            UpdateColorPreview(Color.FromRgb(currentR, currentG, currentB));
        }

        private void UpdateColorPreview(Color c)
        {
            var brush = new SolidColorBrush(c);
            BoxColorPreview.Background = brush;
            GlowLightbar.Color = c;
        }

        private async void OnApplyRgbClick(object sender, RoutedEventArgs e)
        {
            TxtTestFeedback.Text = string.Format(LocalizationManager.Get("Loc_TestingRgbStatus"), currentR, currentG, currentB);
            BtnApplyRgb.IsEnabled = false;

            try
            {
                bool ok = await testService.TestRgbAsync(currentR, currentG, currentB, 2);
                TxtTestFeedback.Text = ok ? LocalizationManager.Get("Loc_RgbTestSuccess") : LocalizationManager.Get("Loc_RgbTestFailed");
            }
            catch (Exception ex)
            {
                TxtTestFeedback.Text = "Erreur : " + ex.Message;
            }
            finally
            {
                BtnApplyRgb.IsEnabled = true;
            }
        }

        private async void OnRestoreRgbClick(object sender, RoutedEventArgs e)
        {
            TxtTestFeedback.Text = LocalizationManager.Get("Loc_RestoringProfileLighting");
            await testService.ResetAllEffectsAsync();
            UpdateColorPreview(Color.FromRgb(0, 55, 145)); // Reset to standard PlayStation Blue
            TxtTestFeedback.Text = LocalizationManager.Get("Loc_ProfileLightingRestored");
        }

        #endregion

        #region Mapping & Persistence Verification

        private void UpdatePersistenceView()
        {
            string profile = !string.IsNullOrWhiteSpace(settings.ForcedProfile) && settings.ForcedProfile != "none"
                ? settings.ForcedProfile
                : "Standard";
            TxtCurrentProfileBadge.Text = profile;

            int defaultSlot = 0;
            if (settings.ApexProfileSlots != null && settings.ApexProfileSlots.Count > 0)
            {
                foreach (var kvp in settings.ApexProfileSlots)
                {
                    defaultSlot = kvp.Value;
                    break;
                }
            }

            TxtCurrentApexSlotBadge.Text = defaultSlot > 0
                ? LocalizationManager.Format("Loc_ApexProfileNumber", defaultSlot)
                : LocalizationManager.Get("Loc_ApexProfileKeep");

            string settingsFile = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "ApexSenseBridge", "tray_settings.json");

            if (File.Exists(settingsFile))
            {
                var fi = new FileInfo(settingsFile);
                TxtPersistenceTimestamp.Text = string.Format(
                    LocalizationManager.Get("Loc_PersistenceLastSaved"),
                    fi.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"));
            }
            else
            {
                TxtPersistenceTimestamp.Text = LocalizationManager.Get("Loc_PersistenceDefaultMemory");
            }
        }

        private void OnVerifyPersistenceClick(object sender, RoutedEventArgs e)
        {
            try
            {
                // Verify non-destructive roundtrip save and load
                settings.Save();
                var reloaded = TraySettings.Load();

                if (reloaded != null)
                {
                    UpdatePersistenceView();
                    TxtTestFeedback.Text = LocalizationManager.Get("Loc_PersistenceVerifiedSuccess");
                }
                else
                {
                    TxtTestFeedback.Text = LocalizationManager.Get("Loc_PersistenceVerifiedWarning");
                }
            }
            catch (Exception ex)
            {
                TxtTestFeedback.Text = "Erreur vérification persistance : " + ex.Message;
            }
        }

        #endregion

        #region Latency Tests

        private async void OnNativeLatencyClick(object sender, RoutedEventArgs e)
        {
            await RunLatencyTestAsync(false);
        }

        private async void OnBridgeLatencyClick(object sender, RoutedEventArgs e)
        {
            await RunLatencyTestAsync(true);
        }

        private async Task RunLatencyTestAsync(bool withBridge)
        {
            if (latencyTestCancellation != null) return;

            var cancellation = new CancellationTokenSource();
            latencyTestCancellation = cancellation;
            bool resumeInputPolling = pollTimer.IsEnabled;
            pollTimer.Stop();
            SetLatencyButtonsEnabled(false);
            TxtTestFeedback.Text = LocalizationManager.Get(
                withBridge ? "Loc_LatencyRunningBridge" : "Loc_LatencyRunningNative");

            if (withBridge)
            {
                TxtBridgeLatencyMain.Text = "…";
                TxtBridgeLatencyPercentiles.Text = LocalizationManager.Get("Loc_LatencyMeasuring");
                TxtBridgeLatencySamples.Text = LocalizationManager.Get("Loc_LatencyMoveController");
            }
            else
            {
                TxtNativeLatencyMain.Text = "…";
                TxtNativeLatencyPercentiles.Text = LocalizationManager.Get("Loc_LatencyMeasuring");
                TxtNativeLatencySamples.Text = LocalizationManager.Get("Loc_LatencyMoveController");
            }

            try
            {
                LatencyTestResult result;
                if (withBridge)
                {
                    int timeout = settings != null ? settings.InitializationTimeoutSeconds : 20;
                    result = await testService.TestBridgeLatencyAsync(5, timeout, cancellation.Token);
                }
                else
                {
                    result = await testService.TestNativeLatencyAsync(5, cancellation.Token);
                }

                if (!cancellation.IsCancellationRequested)
                {
                    DisplayLatencyResult(result);
                }
            }
            catch (OperationCanceledException)
            {
                if (IsLoaded)
                {
                    TxtTestFeedback.Text = LocalizationManager.Get("Loc_LatencyCanceled");
                }
            }
            catch (Exception ex)
            {
                if (IsLoaded)
                {
                    TxtTestFeedback.Text = string.Format(
                        LocalizationManager.Get("Loc_LatencyFailed"), ex.Message);
                }
            }
            finally
            {
                if (ReferenceEquals(latencyTestCancellation, cancellation))
                {
                    latencyTestCancellation = null;
                }
                cancellation.Dispose();
                if (IsLoaded)
                {
                    SetLatencyButtonsEnabled(true);
                    if (resumeInputPolling && IsVisible && !pollTimer.IsEnabled)
                    {
                        pollTimer.Start();
                    }
                }
            }
        }

        private void DisplayLatencyResult(LatencyTestResult result)
        {
            var main = result.IsBridge ? TxtBridgeLatencyMain : TxtNativeLatencyMain;
            var percentiles = result.IsBridge ? TxtBridgeLatencyPercentiles : TxtNativeLatencyPercentiles;
            var samples = result.IsBridge ? TxtBridgeLatencySamples : TxtNativeLatencySamples;

            if (!result.Success)
            {
                main.Text = "…";
                percentiles.Text = LocalizationManager.Get("Loc_LatencyUnavailable");
                samples.Text = LocalizationManager.Get("Loc_LatencyNoResult");
                TxtTestFeedback.Text = string.Format(
                    LocalizationManager.Get("Loc_LatencyFailed"),
                    string.IsNullOrWhiteSpace(result.Error)
                        ? LocalizationManager.Get("Loc_LatencyNoResult")
                        : result.Error);
                return;
            }

            main.Text = FormatLatency(result.P50Milliseconds);
            percentiles.Text = string.Format(
                LocalizationManager.Get("Loc_LatencyPercentiles"),
                FormatLatency(result.P95Milliseconds),
                FormatLatency(result.P99Milliseconds));
            samples.Text = string.Format(
                LocalizationManager.Get(result.IsBridge
                    ? "Loc_LatencyBridgeSamples"
                    : "Loc_LatencyNativeSamples"),
                result.Samples,
                result.UpdateRateHz);
            TxtTestFeedback.Text = string.Format(
                LocalizationManager.Get("Loc_LatencyCompleted"),
                FormatLatency(result.P99Milliseconds));
        }

        private static string FormatLatency(double milliseconds)
        {
            return milliseconds < 10.0
                ? milliseconds.ToString("F3", CultureInfo.CurrentCulture) + " ms"
                : milliseconds.ToString("F1", CultureInfo.CurrentCulture) + " ms";
        }

        private void SetLatencyButtonsEnabled(bool enabled)
        {
            if (BtnTestNativeLatency != null) BtnTestNativeLatency.IsEnabled = enabled;
            if (BtnTestBridgeLatency != null) BtnTestBridgeLatency.IsEnabled = enabled;
        }

        private void CancelLatencyTest()
        {
            var cancellation = latencyTestCancellation;
            if (cancellation != null && !cancellation.IsCancellationRequested)
            {
                cancellation.Cancel();
            }
        }

        #endregion

        #region Gyroscope & Accelerometer 6-Axis Test

        private void StartGyroStream()
        {
            testService.StartGyroStream(sample =>
            {
                Dispatcher.BeginInvoke(new Action(() =>
                {
                    UpdateGyroUi(sample);
                }), DispatcherPriority.Render);
            });
        }

        private void UpdateGyroUi(GyroMotionState sample)
        {
            if (PanelGyro == null || !IsVisible || TabGyro.IsChecked != true) return;
            if (!sample.Connected)
            {
                TxtGyroMotion.Text = LocalizationManager.Get("Loc_StatusUnavailable");
                TxtTestFeedback.Text = sample.Error ?? LocalizationManager.Get("Loc_ControllerUnavailable");
                return;
            }

            lastRawPitch = sample.Pitch;
            lastRawRoll = sample.Roll;

            double calibratedPitch = sample.Pitch - pitchCalibrationOffset;
            double calibratedRoll = sample.Roll - rollCalibrationOffset;

            // Update artificial horizon
            // Pitch moves horizon vertically: clamp to [-35, 35] within 110px circle
            TransHorizonPitch.Y = Math.Max(-35, Math.Min(35, calibratedPitch * 1.2));
            // Roll rotates horizon: banking right tilts horizon counter-clockwise
            RotHorizonRoll.Angle = -calibratedRoll;

            // Text readouts
            TxtGyroPitch.Text = string.Format(CultureInfo.InvariantCulture, "{0:+0.0;-0.0;0.0}°", calibratedPitch);
            TxtGyroRoll.Text = string.Format(CultureInfo.InvariantCulture, "{0:+0.0;-0.0;0.0}°", calibratedRoll);

            // Motion state badge
            if (sample.MotionDetected)
            {
                BadgeGyroMotion.Background = new SolidColorBrush(Color.FromArgb(50, 41, 121, 255));
                TxtGyroMotionIcon.Foreground = new SolidColorBrush(Color.FromRgb(41, 121, 255));
                TxtGyroMotion.Foreground = new SolidColorBrush(Color.FromRgb(147, 197, 253));
                TxtGyroMotion.Text = LocalizationManager.Get("Loc_GyroStatusActive");
            }
            else
            {
                BadgeGyroMotion.Background = new SolidColorBrush(Color.FromArgb(40, 72, 187, 120));
                TxtGyroMotionIcon.Foreground = new SolidColorBrush(Color.FromRgb(72, 187, 120));
                TxtGyroMotion.Foreground = new SolidColorBrush(Color.FromRgb(154, 230, 180));
                TxtGyroMotion.Text = LocalizationManager.Get("Loc_GyroStatusStill");
            }

            // Raw Gyroscope (DPS)
            TxtGyroX.Text = string.Format("{0} °/s", sample.GyroX);
            TxtGyroY.Text = string.Format("{0} °/s", sample.GyroY);
            TxtGyroZ.Text = string.Format("{0} °/s", sample.GyroZ);
            ProgGyroX.Value = Math.Max(-500, Math.Min(500, sample.GyroX));
            ProgGyroY.Value = Math.Max(-500, Math.Min(500, sample.GyroY));
            ProgGyroZ.Value = Math.Max(-500, Math.Min(500, sample.GyroZ));

            // Raw Accelerometer (G)
            double ax = sample.AccelX / 10000.0;
            double ay = sample.AccelY / 10000.0;
            double az = sample.AccelZ / 10000.0;
            TxtAccelX.Text = string.Format(CultureInfo.InvariantCulture, "{0:+0.00;-0.00;0.00} G", ax);
            TxtAccelY.Text = string.Format(CultureInfo.InvariantCulture, "{0:+0.00;-0.00;0.00} G", ay);
            TxtAccelZ.Text = string.Format(CultureInfo.InvariantCulture, "{0:+0.00;-0.00;0.00} G", az);
            ProgAccelX.Value = Math.Max(-200, Math.Min(200, (int)(ax * 100)));
            ProgAccelY.Value = Math.Max(-200, Math.Min(200, (int)(ay * 100)));
            ProgAccelZ.Value = Math.Max(-200, Math.Min(200, (int)(az * 100)));
        }

        private async void OnTestGyroClick(object sender, RoutedEventArgs e)
        {
            TxtTestFeedback.Text = LocalizationManager.Get("Loc_GyroTestingStatus");
            BtnTestGyro.IsEnabled = false;

            try
            {
                var res = await testService.TestGyroAsync(3);
                if (!string.IsNullOrWhiteSpace(res.Error))
                {
                    TxtTestFeedback.Text = res.Error;
                    return;
                }
                string stateStr = res.MotionDetected
                    ? LocalizationManager.Get("Loc_GyroSensorOk")
                    : LocalizationManager.Get("Loc_GyroSensorStill");
                TxtTestFeedback.Text = string.Format(LocalizationManager.Get("Loc_GyroTestResult"), res.SamplesCount, stateStr);
            }
            catch (Exception ex)
            {
                TxtTestFeedback.Text = "Erreur : " + ex.Message;
            }
            finally
            {
                BtnTestGyro.IsEnabled = true;
                if (IsVisible && TabGyro.IsChecked == true) StartGyroStream();
            }
        }

        private void OnCalibrateGyroClick(object sender, RoutedEventArgs e)
        {
            pitchCalibrationOffset = lastRawPitch;
            rollCalibrationOffset = lastRawRoll;
            TxtTestFeedback.Text = LocalizationManager.Get("Loc_GyroCalibratedSuccess");
        }

        #endregion
    }
}
