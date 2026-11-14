using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Services;
using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;

namespace ApexSenseBridgeTray
{
    public partial class ControllerPickerWindow : Window
    {
        private readonly List<Border> rows = new List<Border>();
        private readonly List<string> keys = new List<string>();
        private readonly GamepadNavigationService gamepadNav;
        private readonly string currentKey;
        private readonly bool canSwitch;
        private int selectedIndex;

        public string SelectedKey { get; private set; }

        internal ControllerPickerWindow(IList<DetectedController> controllers, string currentKey, bool canSwitch)
        {
            InitializeComponent();
            this.currentKey = currentKey ?? string.Empty;
            this.canSwitch = canSwitch;
            TxtLocked.Visibility = canSwitch ? Visibility.Collapsed : Visibility.Visible;
            HintSelect.Visibility = canSwitch ? Visibility.Visible : Visibility.Collapsed;

            var modelTotals = new Dictionary<string, int>();
            foreach (var controller in controllers)
            {
                int total;
                modelTotals.TryGetValue(controller.Model, out total);
                modelTotals[controller.Model] = total + 1;
            }
            var modelSeen = new Dictionary<string, int>();
            foreach (var controller in controllers)
            {
                int seen;
                modelSeen.TryGetValue(controller.Model, out seen);
                modelSeen[controller.Model] = ++seen;
                bool current = string.Equals(controller.Key, this.currentKey, StringComparison.Ordinal);
                if (current) selectedIndex = rows.Count;
                var row = CreateRow(controller, modelTotals[controller.Model] > 1 ? seen : 0, current);
                rows.Add(row);
                keys.Add(controller.Key);
                PnlControllers.Children.Add(row);
            }
            UpdateSelection();

            gamepadNav = new GamepadNavigationService(this);
            gamepadNav.UpPressed += () => Move(-1);
            gamepadNav.DownPressed += () => Move(1);
            gamepadNav.ActionPressed += OnGamepadAction;
            gamepadNav.ScrollRequested += delta => ScrollControllers.ScrollToVerticalOffset(ScrollControllers.VerticalOffset + delta);
            gamepadNav.DirectionNavigated += direction => { };
            PreviewKeyDown += OnPreviewKeyDown;
            Loaded += (s, e) => UpdateSelection();
        }

        private Border CreateRow(DetectedController controller, int ordinal, bool current)
        {
            var dock = new DockPanel();
            var chip = new Border { Style = (Style)FindResource("PickerChip") };
            chip.Child = new Path
            {
                Style = (Style)FindResource("ControllerIconPath"),
                Stroke = Brushes.White
            };
            DockPanel.SetDock(chip, Dock.Left);
            dock.Children.Add(chip);

            if (current)
            {
                var check = new Border { Style = (Style)FindResource("CurrentCheck") };
                check.Child = new Path
                {
                    Data = Geometry.Parse("M 1,5 L 4.5,8.5 L 11,1.5"),
                    Stroke = Brushes.White,
                    StrokeThickness = 2.2,
                    StrokeStartLineCap = PenLineCap.Round,
                    StrokeEndLineCap = PenLineCap.Round,
                    StrokeLineJoin = PenLineJoin.Round,
                    Width = 11,
                    Height = 9,
                    Stretch = Stretch.Uniform,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                };
                DockPanel.SetDock(check, Dock.Right);
                dock.Children.Add(check);
            }

            string name = ControllerDetectionService.ModelName(controller.Model);
            string subtitle = null;
            if (name == null)
            {
                if (controller.Model == "unsupported") name = LocalizationManager.Get("Loc_ControllerUnsupported");
                else
                {
                    name = "APEX";
                    subtitle = LocalizationManager.Get("Loc_ControllerPickerNotResponding");
                }
            }
            if (ordinal > 0) name += " #" + ordinal;

            var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            text.Children.Add(new TextBlock { Text = name, Style = (Style)FindResource("PickerName") });
            if (subtitle != null) text.Children.Add(new TextBlock { Text = subtitle, Style = (Style)FindResource("PickerSub") });
            dock.Children.Add(text);

            var row = new Border { Style = (Style)FindResource("PickerRow"), Child = dock };
            if (!canSwitch && !current)
            {
                row.Opacity = 0.45;
                row.Cursor = Cursors.Arrow;
            }
            row.MouseLeftButtonDown += OnRowClick;
            return row;
        }

        private bool IsEnabledRow(int index)
        {
            return canSwitch || string.Equals(keys[index], currentKey, StringComparison.Ordinal);
        }

        private void Move(int delta)
        {
            int target = selectedIndex + delta;
            while (target >= 0 && target < rows.Count && !IsEnabledRow(target)) target += delta;
            if (target < 0 || target >= rows.Count) return;
            selectedIndex = target;
            UpdateSelection();
        }

        private void UpdateSelection()
        {
            for (int i = 0; i < rows.Count; i++)
            {
                if (i == selectedIndex)
                {
                    rows[i].Background = (Brush)FindResource("GamepadFocusFill");
                    rows[i].BorderBrush = Brushes.Transparent;
                    if (rows[i].IsLoaded)
                    {
                        const double gap = 8;
                        rows[i].BringIntoView(new Rect(-gap, -gap,
                            rows[i].ActualWidth + 2 * gap, rows[i].ActualHeight + 2 * gap));
                    }
                }
                else
                {
                    rows[i].ClearValue(Border.BackgroundProperty);
                    rows[i].BorderBrush = Brushes.Transparent;
                }
            }
        }

        private void OnGamepadAction(GamepadButtonAction action)
        {
            if (action == GamepadButtonAction.Select) CommitSelection();
            else if (action == GamepadButtonAction.Back) Close();
        }

        private void OnPreviewKeyDown(object sender, KeyEventArgs e)
        {
            switch (e.Key)
            {
                case Key.Up: Move(-1); e.Handled = true; break;
                case Key.Down: Move(1); e.Handled = true; break;
                case Key.Enter:
                case Key.Space: CommitSelection(); e.Handled = true; break;
                case Key.Escape: Close(); e.Handled = true; break;
            }
        }

        private void OnRowClick(object sender, MouseButtonEventArgs e)
        {
            int index = rows.IndexOf(sender as Border);
            if (index < 0) return;
            e.Handled = true;
            if (!IsEnabledRow(index)) return;
            selectedIndex = index;
            CommitSelection();
        }

        private void CommitSelection()
        {
            if (selectedIndex < 0 || selectedIndex >= rows.Count || !IsEnabledRow(selectedIndex)) return;
            SelectedKey = keys[selectedIndex];
            DialogResult = true;
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed)
            {
                try { DragMove(); } catch (InvalidOperationException) { }
            }
        }

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            Close();
        }

        protected override void OnClosed(EventArgs e)
        {
            gamepadNav.Dispose();
            base.OnClosed(e);
        }
    }
}
