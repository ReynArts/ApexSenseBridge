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

            // Triggers L2 / R2
            TxtL2Val.Text = string.Format("L2 {0}%", (int)(state.LeftTrigger / 2.55));
            OverlayL2.Opacity = state.LeftTrigger / 255.0;
            OverlayL1.Visibility = state.L1 ? Visibility.Visible : Visibility.Collapsed;

            TxtR2Val.Text = string.Format("R2 {0}%", (int)(state.RightTrigger / 2.55));
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

            // Thumbsticks deflection & visual state (in 896x512 space)
            double normLX = state.ThumbLX / 32768.0;
            double normLY = state.ThumbLY / 32768.0;
            TransStickL.X = normLX * 22.0;
            TransStickL.Y = -normLY * 22.0;
            bool stickLDeflected = Math.Abs(normLX) > 0.08 || Math.Abs(normLY) > 0.08;
            GlowStickL.Opacity = state.L3 ? 1.0 : (stickLDeflected ? 0.35 : 0.0);
            BorderStickL.BorderBrush = state.L3 ? new SolidColorBrush(Color.FromRgb(0, 112, 209)) : (stickLDeflected ? new SolidColorBrush(Color.FromRgb(72, 92, 122)) : new SolidColorBrush(Color.FromRgb(59, 70, 93)));
            BorderStickL.Background = state.L3 ? new SolidColorBrush(Color.FromRgb(22, 34, 52)) : new SolidColorBrush(Color.FromRgb(26, 31, 43));
            TxtStickL.Foreground = state.L3 ? Brushes.White : (stickLDeflected ? new SolidColorBrush(Color.FromRgb(190, 210, 235)) : new SolidColorBrush(Color.FromRgb(122, 139, 162)));
            TxtTelemetryStickL.Text = string.Format("Stick G : X={0,4} Y={1,4}", (int)(normLX * 100), (int)(normLY * 100));

            double normRX = state.ThumbRX / 32768.0;
            double normRY = state.ThumbRY / 32768.0;
            TransStickR.X = normRX * 22.0;
            TransStickR.Y = -normRY * 22.0;
            bool stickRDeflected = Math.Abs(normRX) > 0.08 || Math.Abs(normRY) > 0.08;
            GlowStickR.Opacity = state.R3 ? 1.0 : (stickRDeflected ? 0.35 : 0.0);
            BorderStickR.BorderBrush = state.R3 ? new SolidColorBrush(Color.FromRgb(0, 112, 209)) : (stickRDeflected ? new SolidColorBrush(Color.FromRgb(72, 92, 122)) : new SolidColorBrush(Color.FromRgb(59, 70, 93)));
            BorderStickR.Background = state.R3 ? new SolidColorBrush(Color.FromRgb(22, 34, 52)) : new SolidColorBrush(Color.FromRgb(26, 31, 43));
            TxtStickR.Foreground = state.R3 ? Brushes.White : (stickRDeflected ? new SolidColorBrush(Color.FromRgb(190, 210, 235)) : new SolidColorBrush(Color.FromRgb(122, 139, 162)));
            TxtTelemetryStickR.Text = string.Format("Stick D : X={0,4} Y={1,4}", (int)(normRX * 100), (int)(normRY * 100));
        }

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
                main.Text = "—";
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
