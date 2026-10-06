using ApexSenseBridgeTray.Common;
using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;

namespace ApexSenseBridgeTray
{
    public partial class LanguagePickerWindow : Window
    {
        private readonly List<Border> options = new List<Border>();
        private readonly GamepadNavigationService gamepadNav;
        private readonly string currentLanguage;
        private int selectedIndex;

        public string SelectedLanguage { get; private set; }

        public LanguagePickerWindow(string currentLanguage)
        {
            InitializeComponent();
            this.currentLanguage = currentLanguage;
            // One row per embedded dictionary, so new languages appear without touching this window.
            foreach (var language in LanguageCatalog.Available)
            {
                if (string.Equals(language.Code, currentLanguage, StringComparison.OrdinalIgnoreCase))
                    selectedIndex = options.Count;
                var row = CreateRow(language);
                options.Add(row);
                PnlLanguages.Children.Add(row);
            }
            UpdateSelection();

            gamepadNav = new GamepadNavigationService(this);
            gamepadNav.UpPressed += () => Move(-1);
            gamepadNav.DownPressed += () => Move(1);
            gamepadNav.ActionPressed += OnGamepadAction;
            gamepadNav.ScrollRequested += delta => ScrollLanguages.ScrollToVerticalOffset(ScrollLanguages.VerticalOffset + delta);
            gamepadNav.DirectionNavigated += direction => { };
            PreviewKeyDown += OnPreviewKeyDown;
            Loaded += (s, e) => UpdateSelection();
        }

        // Row: code chip, native name, check mark on the current language.
        private Border CreateRow(LanguageCatalog.Entry language)
        {
            var dock = new DockPanel();
            var chip = new Border { Style = (Style)FindResource("LanguageCode") };
            chip.Child = new TextBlock
            {
                Text = language.Badge,
                Style = (Style)FindResource("LanguageCodeText"),
                FontSize = language.Badge.Length == 1 ? 14 : 12
            };
            DockPanel.SetDock(chip, Dock.Left);
            dock.Children.Add(chip);

            if (string.Equals(language.Code, currentLanguage, StringComparison.OrdinalIgnoreCase))
            {
                var check = new Border { Style = (Style)FindResource("CurrentCheck"), Visibility = Visibility.Visible };
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

            dock.Children.Add(new TextBlock { Text = language.NativeName, Style = (Style)FindResource("LanguageName") });
            var row = new Border { Style = (Style)FindResource("LanguageRow"), Tag = language.Code, Child = dock };
            row.MouseLeftButtonDown += OnLanguageRowClick;
            return row;
        }

        private void Move(int delta)
        {
            int target = selectedIndex + delta;
            if (target < 0 || target >= options.Count) return;
            selectedIndex = target;
            UpdateSelection();
        }

        // Same focus language as the main window: lifted fill and a crisp detached white ring.
        private void UpdateSelection()
        {
            for (int i = 0; i < options.Count; i++)
            {
                if (i == selectedIndex)
                {
                    options[i].Background = (Brush)FindResource("GamepadFocusFill");
                    options[i].BorderBrush = Brushes.Transparent;
                    FocusRingAdorner.Show(options[i]);
                    // Keep the row and its ring fully inside the scrolled area.
                    if (options[i].IsLoaded)
                    {
                        double gap = FocusRingAdorner.Gap + 4;
                        options[i].BringIntoView(new Rect(-gap, -gap,
                            options[i].ActualWidth + 2 * gap, options[i].ActualHeight + 2 * gap));
                    }
                }
                else
                {
                    options[i].ClearValue(Border.BackgroundProperty);
                    options[i].BorderBrush = Brushes.Transparent;
                    FocusRingAdorner.Hide(options[i]);
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

        private void OnLanguageRowClick(object sender, MouseButtonEventArgs e)
        {
            int index = options.IndexOf(sender as Border);
            if (index < 0) return;
            e.Handled = true; // the frame below would otherwise start a window drag
            selectedIndex = index;
            CommitSelection();
        }

        private void CommitSelection()
        {
            if (selectedIndex < 0 || selectedIndex >= options.Count) return;
            SelectedLanguage = options[selectedIndex].Tag as string;
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
            foreach (var row in options) FocusRingAdorner.Hide(row);
            gamepadNav.Dispose();
            base.OnClosed(e);
        }
    }
}
