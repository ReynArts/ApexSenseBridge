using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace ApexSenseBridgeTray
{
    public enum ReportChoice
    {
        None,
        Bug,
        Feature,
        Game,
        Logs
    }

    public sealed class ReportChoiceStyle
    {
        public Geometry Icon { get; set; }
        public Brush Tint { get; set; }
        public Brush Stroke { get; set; }
    }

    public partial class ReportChooserWindow : Window
    {
        private readonly Button[] choices;

        public ReportChooserWindow()
        {
            InitializeComponent();
            choices = new[] { BtnChoiceBug, BtnChoiceFeature, BtnChoiceGame, BtnChoiceLogs };

            BtnChoiceBug.Tag = MakeStyle("GeoReportBug", "#33F59E0B", "#FBBF24");
            BtnChoiceFeature.Tag = MakeStyle("GeoReportFeature", "#3334D399", "#34D399");
            BtnChoiceGame.Tag = MakeStyle("GeoReportGame", "#331E6FE8", "#7FB0FF");
            BtnChoiceLogs.Tag = MakeStyle("GeoReportLogs", "#1FFFFFFF", "#FFFFFF");

            Loaded += (sender, args) => BtnChoiceBug.Focus();
        }

        public ReportChoice SelectedChoice { get; private set; }

        private ReportChoiceStyle MakeStyle(string geometryKey, string tint, string stroke)
        {
            var converter = new BrushConverter();
            return new ReportChoiceStyle
            {
                Icon = (Geometry)FindResource(geometryKey),
                Tint = (Brush)converter.ConvertFromString(tint),
                Stroke = (Brush)converter.ConvertFromString(stroke)
            };
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed) DragMove();
        }

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            SelectedChoice = ReportChoice.None;
            DialogResult = false;
        }

        private void OnChoiceClick(object sender, RoutedEventArgs e)
        {
            if (sender == BtnChoiceBug) SelectedChoice = ReportChoice.Bug;
            else if (sender == BtnChoiceFeature) SelectedChoice = ReportChoice.Feature;
            else if (sender == BtnChoiceGame) SelectedChoice = ReportChoice.Game;
            else if (sender == BtnChoiceLogs) SelectedChoice = ReportChoice.Logs;
            else return;
            DialogResult = true;
        }

        private void OnPreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                e.Handled = true;
                OnCloseClick(this, null);
                return;
            }

            if (e.Key != Key.Down && e.Key != Key.Up) return;
            int current = System.Array.IndexOf(choices, Keyboard.FocusedElement as Button);
            int step = e.Key == Key.Down ? 1 : -1;
            int next = current < 0 ? 0 : (current + step + choices.Length) % choices.Length;
            choices[next].Focus();
            e.Handled = true;
        }
    }
}
