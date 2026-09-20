using ApexSenseBridgeTray.Common;
using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace ApexSenseBridgeTray
{
    public partial class LanguagePickerWindow : Window
    {
        private readonly List<Button> options = new List<Button>();
        private readonly GamepadNavigationService gamepadNav;
        private int selectedIndex;

        public string SelectedLanguage { get; private set; }

        public LanguagePickerWindow(string currentLanguage)
        {
            InitializeComponent();
            options.Add(BtnEnglish);
            options.Add(BtnFrench);
            options.Add(BtnSpanish);
            options.Add(BtnChinese);
            selectedIndex = Math.Max(0, options.FindIndex(button => string.Equals(
                button.Tag as string, currentLanguage, StringComparison.OrdinalIgnoreCase)));
            UpdateSelection();

            gamepadNav = new GamepadNavigationService(this);
            gamepadNav.UpPressed += () => MoveVertical(-1);
            gamepadNav.DownPressed += () => MoveVertical(1);
            gamepadNav.LeftPressed += () => MoveHorizontal(-1);
            gamepadNav.RightPressed += () => MoveHorizontal(1);
            gamepadNav.ActionPressed += OnGamepadAction;
            gamepadNav.DirectionNavigated += direction => { };
        }

        private void MoveVertical(int delta)
        {
            int target = selectedIndex + (delta * 2);
            if (target >= 0 && target < options.Count)
            {
                selectedIndex = target;
                UpdateSelection();
            }
        }

        private void MoveHorizontal(int delta)
        {
            int column = selectedIndex % 2;
            if ((delta < 0 && column == 1) || (delta > 0 && column == 0))
            {
                selectedIndex += delta;
                UpdateSelection();
            }
        }

        private void UpdateSelection()
        {
            Brush active = new SolidColorBrush(Color.FromRgb(0x00, 0x70, 0xD1));
            Brush inactive = new SolidColorBrush(Color.FromRgb(0x2A, 0x2F, 0x3B));
            for (int i = 0; i < options.Count; i++)
            {
                options[i].BorderBrush = i == selectedIndex ? active : inactive;
                options[i].Background = i == selectedIndex
                    ? new SolidColorBrush(Color.FromRgb(0x12, 0x2B, 0x45))
                    : new SolidColorBrush(Color.FromRgb(0x17, 0x1A, 0x22));
            }
        }

        private void OnGamepadAction(GamepadButtonAction action)
        {
            if (action == GamepadButtonAction.Select) CommitSelection();
            else if (action == GamepadButtonAction.Back) Close();
        }

        private void OnLanguageClick(object sender, RoutedEventArgs e)
        {
            Button button = sender as Button;
            int index = button == null ? -1 : options.IndexOf(button);
            if (index < 0) return;
            selectedIndex = index;
            CommitSelection();
        }

        private void CommitSelection()
        {
            SelectedLanguage = options[selectedIndex].Tag as string;
            DialogResult = true;
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
