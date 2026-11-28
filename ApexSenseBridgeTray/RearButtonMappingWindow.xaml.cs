using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace ApexSenseBridgeTray
{
    public partial class RearButtonMappingWindow : Window
    {
        private static readonly string[] ActionNames =
        {
            "None",
            "Cross", "Circle", "Square", "Triangle",
            "L1", "R1", "L2", "R2", "L3", "R3",
            "Create", "Options", "PS", "Touchpad", "Mute"
        };

        private static readonly Dictionary<string, string> EngineErrorPrefixes = new Dictionary<string, string>
        {
            { "APEX identity verification failed:", "Loc_RearButtonsIdentityFailed" },
            { "Could not read APEX input transport:", "Loc_RearButtonsReadRoutingFailed" },
            { "Could not enable raw APEX input routing:", "Loc_RearButtonsEnableRoutingFailed" },
            { "APEX input stream error:", "Loc_RearButtonsInputFailed" },
            { "Could not restore APEX input routing:", "Loc_RearButtonsRestoreRoutingFailed" },
        };

        private sealed class RearRowView
        {
            public RearButton Button;
            public Border Container;
            public TextBlock Status;
            public ComboBox Action;
            public bool Detected;
        }

        private readonly TraySettings settings;
        private readonly RearButtonService service;
        private readonly Dictionary<RearButton, RearRowView> rows =
            new Dictionary<RearButton, RearRowView>();

        public RearButtonMappingWindow(TraySettings settings)
        {
            this.settings = settings ?? TraySettings.Load();
            InitializeComponent();

            service = new RearButtonService();
            BuildRows();

            Loaded += OnWindowLoaded;
            Closed += OnWindowClosed;
        }

        private void BuildRows()
        {
            foreach (RearButton button in Enum.GetValues(typeof(RearButton)))
            {
                var name = new TextBlock
                {
                    Text = button.ToString(),
                    Style = (Style)FindResource("RearButtonName")
                };
                var status = new TextBlock
                {
                    Text = LocalizationManager.Get("Loc_RearButtonsPressToDetect"),
                    Style = (Style)FindResource("RearButtonStatus")
                };
                var labels = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
                labels.Children.Add(name);
                labels.Children.Add(status);

                var combo = new ComboBox
                {
                    Style = (Style)FindResource("RearActionCombo"),
                    ItemsSource = ActionNames,
                    IsEnabled = false
                };
                combo.SelectedItem = ResolveSavedAction(button);

                var grid = new Grid();
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                Grid.SetColumn(labels, 0);
                Grid.SetColumn(combo, 1);
                grid.Children.Add(labels);
                grid.Children.Add(combo);

                var container = new Border
                {
                    Style = (Style)FindResource("RearRow"),
                    Child = grid
                };

                PnlRearButtons.Children.Add(container);
                rows[button] = new RearRowView
                {
                    Button = button,
                    Container = container,
                    Status = status,
                    Action = combo
                };
            }
        }

        private string ResolveSavedAction(RearButton button)
        {
            string value;
            if (settings.RearButtonBindings != null &&
                settings.RearButtonBindings.TryGetValue(button.ToString(), out value) &&
                Array.IndexOf(ActionNames, value) >= 0)
            {
                return value;
            }

            return "None";
        }

        private void OnWindowLoaded(object sender, RoutedEventArgs e)
        {
            GamepadNavigationService.IsNavigationSuspended = true;
            TxtRearStatus.Text = LocalizationManager.Get("Loc_RearButtonsListening");
            service.Start(OnRearButtonEvent, OnRearButtonError);
        }

        private void OnWindowClosed(object sender, EventArgs e)
        {
            GamepadNavigationService.IsNavigationSuspended = false;
            service.Stop();
        }

        private void OnRearButtonEvent(RearButtonEvent buttonEvent)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                if (!IsVisible || !rows.ContainsKey(buttonEvent.Button))
                    return;

                var row = rows[buttonEvent.Button];
                if (buttonEvent.Pressed)
                {
                    row.Detected = true;
                    row.Status.Text = "Detected";
                    row.Status.Foreground = Brushes.LightGreen;
                    row.Action.IsEnabled = true;
                    row.Container.BorderBrush = new SolidColorBrush(Color.FromArgb(0x55, 0x5B, 0x8C, 0xFF));
                    row.Container.BorderThickness = new Thickness(2);
                    TxtRearStatus.Text = buttonEvent.Button + " detected";
                }
            }));
        }

        private void OnRearButtonError(string error)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                if (IsVisible) TxtRearStatus.Text = FormatRearButtonError(error);
            }));
        }

        private static string FormatRearButtonError(string error)
        {
            if (string.IsNullOrWhiteSpace(error))
                return LocalizationManager.Get("Loc_RearButtonsUnavailable");

            error = error.Trim();
            if (error == "ApexSenseBridge.exe was not found.")
                return LocalizationManager.Get("Loc_RearButtonsEngineMissing");
            if (error == "Rear-button stream stopped; routing restored")
                return LocalizationManager.Get("Loc_RearButtonsStreamStopped");

            foreach (var entry in EngineErrorPrefixes)
            {
                if (error.StartsWith(entry.Key, StringComparison.Ordinal))
                    return LocalizationManager.Format(entry.Value, error.Substring(entry.Key.Length).TrimStart());
            }

            return LocalizationManager.Format("Loc_RearButtonsUnavailableDetail", error);
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed)
                DragMove();
        }

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void OnSaveRearBindingsClick(object sender, RoutedEventArgs e)
        {
            try
            {
                if (settings.RearButtonBindings == null)
                    settings.RearButtonBindings = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

                foreach (var row in rows.Values)
                    settings.RearButtonBindings[row.Button.ToString()] = row.Action.SelectedItem as string ?? "None";

                settings.Save();
                TxtRearStatus.Text = LocalizationManager.Get("Loc_RearButtonsSaved");
            }
            catch (Exception ex)
            {
                TxtRearStatus.Text = LocalizationManager.Format("Loc_RearButtonsSaveFailed", ex.Message);
            }
        }
    }
}
