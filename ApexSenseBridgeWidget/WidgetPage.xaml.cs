using System;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Gaming.XboxGameBar;
using Windows.UI;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;

namespace ApexSenseBridgeWidget
{
    public sealed partial class WidgetPage : Page
    {
        private static readonly TimeSpan LocalChangeHold = TimeSpan.FromMilliseconds(1500);

        private readonly DispatcherTimer pollTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(700) };
        private readonly DispatcherTimer sendTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(150) };
        private XboxGameBarWidget widget;
        private BridgeStatus last = new BridgeStatus();
        private string renderedRemapKey = string.Empty;
        private bool updating;
        private bool busy;
        private DateTime lastLocalChange = DateTime.MinValue;
        private bool pendingTrigger;
        private bool pendingVibration;
        private bool pendingMute;
        private bool pendingEffects;
        private int? pendingProfile;

        public WidgetPage()
        {
            InitializeComponent();
            ApplyTexts();
            pollTimer.Tick += async (s, e) => await PollAsync();
            sendTimer.Tick += async (s, e) => await SendPendingAsync();
            Render(last);
        }

        protected override async void OnNavigatedTo(NavigationEventArgs e)
        {
            widget = e.Parameter as XboxGameBarWidget;
            pollTimer.Start();
            await PollAsync();
        }

        protected override void OnNavigatedFrom(NavigationEventArgs e)
        {
            pollTimer.Stop();
            sendTimer.Stop();
        }

        private async Task PollAsync()
        {
            if (busy) return;
            busy = true;
            try
            {
                Render(await BridgeClient.StatusAsync());
            }
            finally
            {
                busy = false;
            }
        }

        private async Task SendPendingAsync()
        {
            sendTimer.Stop();
            if (busy)
            {
                sendTimer.Start();
                return;
            }
            busy = true;
            try
            {
                var status = await BridgeClient.SetAsync(
                    pendingTrigger ? (int?)(int)TriggerSlider.Value : null,
                    pendingVibration ? (int?)(int)VibrationSlider.Value : null,
                    pendingMute ? (bool?)MuteToggle.IsOn : null,
                    pendingEffects ? (bool?)EffectsOffToggle.IsOn : null,
                    pendingProfile,
                    SaveToggle.IsOn && (pendingTrigger || pendingVibration));
                pendingTrigger = pendingVibration = pendingMute = pendingEffects = false;
                pendingProfile = null;
                lastLocalChange = DateTime.UtcNow;
                Render(status, true);
                if (status.TrayReachable && string.IsNullOrEmpty(status.Error))
                {
                    FooterText.Text = status.Acknowledged
                        ? Texts.Get(status.Saved ? "Saved" : "Applied")
                        : Texts.Get("NotApplied");
                }
            }
            finally
            {
                busy = false;
            }
        }

        private void QueueSend()
        {
            lastLocalChange = DateTime.UtcNow;
            sendTimer.Stop();
            sendTimer.Start();
        }

        private void OnSliderChanged(object sender, Windows.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            UpdateSubTexts();
            if (updating) return;
            if (sender == TriggerSlider) pendingTrigger = true;
            else if (sender == VibrationSlider) pendingVibration = true;
            QueueSend();
        }

        private void OnToggleChanged(object sender, RoutedEventArgs e)
        {
            UpdateSubTexts();
            if (updating) return;
            if (sender == MuteToggle) pendingMute = true;
            else if (sender == EffectsOffToggle) pendingEffects = true;
            QueueSend();
        }

        private void OnSaveToggled(object sender, RoutedEventArgs e)
        {
            UpdateSubTexts();
        }

        private void OnProfileClick(object sender, RoutedEventArgs e)
        {
            if (last.Tuning == null || last.Profiles.Length == 0) return;
            pendingProfile = (last.Tuning.TouchpadProfile + 1) % last.Profiles.Length;
            QueueSend();
        }

        private void ApplyTexts()
        {
            TriggerLabel.Text = Texts.Get("Triggers");
            VibrationLabel.Text = Texts.Get("Grips");
            MuteTitle.Text = Texts.Get("MuteGrips");
            EffectsTitle.Text = Texts.Get("EffectsOff");
            ProfileTitle.Text = Texts.Get("Profile");
            RemapHeader.Text = Texts.Get("Remapping");
            SaveTitle.Text = Texts.Get("Remember");
            SaveSub.Text = Texts.Get("RememberHint");
            UpdateSubTexts();
        }

        private void OnPageSizeChanged(object sender, SizeChangedEventArgs e)
        {
            var compact = e.NewSize.Height < 560;
            var narrow = e.NewSize.Width < 340;
            var side = narrow ? 10 : compact ? 14 : 18;
            Root.Padding = new Thickness(side, compact ? 4 : 8, side, compact ? 8 : 12);
            var gap = compact ? 5 : 10;
            foreach (var divider in new[] { Divider1, Divider2, Divider3 }) divider.Margin = new Thickness(0, gap, 0, gap);
            Divider0.Margin = new Thickness(0, compact ? 2 : 4, 0, gap);
        }

        private void UpdateSubTexts()
        {
            TriggerValue.Text = string.Format(CultureInfo.CurrentCulture, Texts.Get("Percent"), (int)TriggerSlider.Value);
            VibrationValue.Text = string.Format(CultureInfo.CurrentCulture, Texts.Get("Percent"), (int)VibrationSlider.Value);
            MuteSub.Text = Texts.Get(MuteToggle.IsOn ? "On" : "Off");
            EffectsSub.Text = Texts.Get(EffectsOffToggle.IsOn ? "On" : "Off");
        }

        private void Render(BridgeStatus status, bool force = false)
        {
            last = status;
            if (Texts.SetLanguage(status.Language))
            {
                ApplyTexts();
                renderedRemapKey = string.Empty;
            }
            string label;
            Color dot;
            if (!status.TrayReachable || !status.Active)
            {
                label = Texts.Get("Stopped");
                dot = Colors.Gray;
            }
            else
            {
                switch (status.Phase)
                {
                    case "Ready":
                        label = status.ControllerLost ? Texts.Get("Reconnecting") : Texts.Get("Active");
                        dot = status.ControllerLost ? Colors.Orange : Colors.LimeGreen;
                        break;
                    case "Starting":
                        label = Texts.Get("Starting");
                        dot = Colors.Gold;
                        break;
                    case "Stopping":
                        label = Texts.Get("Stopping");
                        dot = Colors.Orange;
                        break;
                    case "Failed":
                        label = Texts.Get("Error");
                        dot = Colors.OrangeRed;
                        break;
                    default:
                        label = Texts.Get("Stopped");
                        dot = Colors.Gray;
                        break;
                }
            }
            StatusDot.Fill = new SolidColorBrush(dot);
            StatusText.Text = label;

            var interactive = status.TrayReachable && status.Active && status.Tuning != null;
            Controls.Visibility = interactive ? Visibility.Visible : Visibility.Collapsed;
            if (!status.TrayReachable)
            {
                DetailText.Text = Texts.Get("TrayMissing");
                FooterText.Text = string.Empty;
                return;
            }
            if (!status.Active)
            {
                DetailText.Text = Texts.Get("NoSession");
                FooterText.Text = string.Empty;
                return;
            }
            DetailText.Text = string.Join(" · ", new[] { status.Game, status.Controller }.Where(part => !string.IsNullOrEmpty(part)));
            if (!interactive) return;

            if (!force && DateTime.UtcNow - lastLocalChange < LocalChangeHold) return;
            updating = true;
            try
            {
                var tuning = status.Tuning;
                VibrationSlider.Maximum = tuning.MaxVibration;
                TriggerSlider.Value = tuning.Trigger;
                VibrationSlider.Value = tuning.Vibration;
                MuteToggle.IsOn = tuning.GripMute;
                EffectsOffToggle.IsOn = tuning.TriggerEffectsOff;
                ProfileSub.Text = ProfileLabel(tuning.TouchpadProfileName);
                SaveToggle.IsEnabled = status.CanSave;
                if (!status.CanSave) SaveToggle.IsOn = false;
                RenderRemaps(status.Remaps, tuning.TouchpadProfileName);
                UpdateSubTexts();
            }
            finally
            {
                updating = false;
            }
        }

        private void RenderRemaps(RemapEntry[] remaps, string profileName)
        {
            var key = profileName + "|" + string.Join(";", remaps.Select(r => r.Output + ":" + string.Join(",", r.Buttons) + ":" + r.Hold + ":" + r.Use));
            if (key == renderedRemapKey) return;
            renderedRemapKey = key;
            RemapPanel.Children.Clear();
            foreach (var remap in remaps)
            {
                RemapPanel.Children.Add(new Viewbox
                {
                    Stretch = Stretch.Uniform,
                    StretchDirection = StretchDirection.DownOnly,
                    HorizontalAlignment = HorizontalAlignment.Left,
                    Child = GlyphFactory.BuildEntry(remap)
                });
                var text = string.IsNullOrEmpty(remap.Use) ? remap.OutputLabel : remap.Use;
                if (!string.IsNullOrEmpty(text))
                {
                    RemapPanel.Children.Add(new TextBlock
                    {
                        Text = text,
                        FontSize = 12,
                        Foreground = new SolidColorBrush(Color.FromArgb(0xFF, 0xA9, 0xAE, 0xB9)),
                        TextWrapping = TextWrapping.Wrap,
                        Margin = new Thickness(0, 0, 0, 4)
                    });
                }
            }
            RemapSection.Visibility = remaps.Length == 0 ? Visibility.Collapsed : Visibility.Visible;
        }

        private static string ProfileLabel(string name)
        {
            if (string.IsNullOrEmpty(name) || name == "none") return Texts.Get("ProfileNone");
            var words = name.Split('-');
            return string.Join(" ", words.Select(word => word.Length == 0
                ? word
                : char.ToUpper(word[0], CultureInfo.InvariantCulture) + word.Substring(1)));
        }
    }
}
