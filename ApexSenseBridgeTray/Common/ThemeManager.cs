using Microsoft.Win32;
using System;
using System.Windows;
using System.Windows.Media;

namespace ApexSenseBridgeTray.Common
{
    public static class ThemeManager
    {
        private static bool isDarkTheme = true;

        public static bool IsDarkTheme
        {
            get { return isDarkTheme; }
            private set { isDarkTheme = value; }
        }

        public static event Action ThemeChanged;

        public static void Initialize()
        {
            UpdateTheme();
            SystemEvents.UserPreferenceChanged += OnUserPreferenceChanged;
        }

        private static void OnUserPreferenceChanged(object sender, UserPreferenceChangedEventArgs e)
        {
            if (e.Category == UserPreferenceCategory.General || e.Category == UserPreferenceCategory.Color)
            {
                if (Application.Current != null && Application.Current.Dispatcher != null)
                {
                    Application.Current.Dispatcher.Invoke(new Action(() =>
                    {
                        UpdateTheme();
                        var handler = ThemeChanged;
                        if (handler != null)
                        {
                            handler();
                        }
                    }));
                }
            }
        }

        public static void UpdateTheme()
        {
            IsDarkTheme = QueryWindowsDarkTheme();
            ApplyThemeResources(IsDarkTheme);
        }

        private static bool QueryWindowsDarkTheme()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"))
                {
                    if (key != null)
                    {
                        var value = key.GetValue("AppsUseLightTheme");
                        if (value is int)
                        {
                            int lightTheme = (int)value;
                            return lightTheme == 0;
                        }
                    }
                }
            }
            catch
            {
            }
            return true;
        }

        private static void ApplyThemeResources(bool isDark)
        {
            if (Application.Current == null) return;
            var res = Application.Current.Resources;
            if (res == null) return;

            if (isDark)
            {
                // Ultra-noir AMOLED / Deep Obsidian palette (sans contours visibles)
                res["WindowBackground"] = new SolidColorBrush(Color.FromRgb(0x05, 0x05, 0x07));
                res["CardBackground"] = new SolidColorBrush(Color.FromRgb(0x0D, 0x0D, 0x12));
                res["CardBorder"] = Brushes.Transparent;
                res["FooterBackground"] = new SolidColorBrush(Color.FromRgb(0x07, 0x07, 0x09));

                res["ControlBackground"] = new SolidColorBrush(Color.FromRgb(0x14, 0x14, 0x1A));
                res["ControlBorder"] = Brushes.Transparent;

                res["TextPrimary"] = new SolidColorBrush(Color.FromRgb(0xFF, 0xFF, 0xFF));
                res["TextSecondary"] = new SolidColorBrush(Color.FromRgb(0xA0, 0xA0, 0xB0));
                res["TextMuted"] = new SolidColorBrush(Color.FromRgb(0x5C, 0x5C, 0x6E));

                res["BadgeActiveBg"] = new SolidColorBrush(Color.FromArgb(0x28, 0x10, 0xB9, 0x81));
                res["BadgeActiveFg"] = new SolidColorBrush(Color.FromRgb(0x34, 0xD3, 0x99));
                res["BadgeStandbyBg"] = new SolidColorBrush(Color.FromArgb(0x24, 0x00, 0x70, 0xD1));
                res["BadgeStandbyFg"] = new SolidColorBrush(Color.FromRgb(0x38, 0xBD, 0xF8));

                res["FeaturePillBg"] = new SolidColorBrush(Color.FromArgb(0x18, 0xFF, 0xFF, 0xFF));
                res["FeaturePillBorder"] = Brushes.Transparent;

                // Gamepad HUD & Focus
                res["GamepadFocusBorder"] = new SolidColorBrush(Color.FromRgb(0x00, 0x70, 0xD1));
                res["GamepadFocusGlow"] = new SolidColorBrush(Color.FromArgb(0x30, 0x00, 0x70, 0xD1));
                res["GamepadHudBg"] = new SolidColorBrush(Color.FromRgb(0x07, 0x07, 0x0A));
                res["GamepadBadgeBg"] = new SolidColorBrush(Color.FromRgb(0x18, 0x18, 0x22));
                res["GamepadBadgeBorder"] = Brushes.Transparent;
                res["GamepadBadgeFg"] = new SolidColorBrush(Color.FromRgb(0xEE, 0xEE, 0xF2));
            }
            else
            {
                res["WindowBackground"] = new SolidColorBrush(Color.FromRgb(0xF2, 0xF3, 0xF5));
                res["CardBackground"] = new SolidColorBrush(Color.FromRgb(0xFF, 0xFF, 0xFF));
                res["CardBorder"] = new SolidColorBrush(Color.FromArgb(0x14, 0x00, 0x00, 0x00));
                res["FooterBackground"] = new SolidColorBrush(Color.FromRgb(0xEB, 0xEC, 0xF0));

                res["ControlBackground"] = new SolidColorBrush(Color.FromRgb(0xF5, 0xF5, 0xF8));
                res["ControlBorder"] = new SolidColorBrush(Color.FromRgb(0xD1, 0xD5, 0xDB));

                res["TextPrimary"] = new SolidColorBrush(Color.FromRgb(0x11, 0x11, 0x14));
                res["TextSecondary"] = new SolidColorBrush(Color.FromArgb(0x99, 0x00, 0x00, 0x00));
                res["TextMuted"] = new SolidColorBrush(Color.FromArgb(0x55, 0x00, 0x00, 0x00));

                res["BadgeActiveBg"] = new SolidColorBrush(Color.FromArgb(0x1A, 0x16, 0xA3, 0x4A));
                res["BadgeActiveFg"] = new SolidColorBrush(Color.FromRgb(0x15, 0x80, 0x3D));
                res["BadgeStandbyBg"] = new SolidColorBrush(Color.FromArgb(0x14, 0x37, 0x68, 0xD1));
                res["BadgeStandbyFg"] = new SolidColorBrush(Color.FromRgb(0x1D, 0x4E, 0xD8));

                res["FeaturePillBg"] = new SolidColorBrush(Color.FromArgb(0x0A, 0x00, 0x00, 0x00));
                res["FeaturePillBorder"] = new SolidColorBrush(Color.FromArgb(0x0C, 0x00, 0x00, 0x00));

                res["GamepadFocusBorder"] = new SolidColorBrush(Color.FromRgb(0x00, 0x70, 0xD1));
                res["GamepadFocusGlow"] = new SolidColorBrush(Color.FromArgb(0x20, 0x00, 0x70, 0xD1));
                res["GamepadHudBg"] = new SolidColorBrush(Color.FromRgb(0xEA, 0xEC, 0xF0));
                res["GamepadBadgeBg"] = new SolidColorBrush(Color.FromRgb(0xDE, 0xE2, 0xE8));
                res["GamepadBadgeBorder"] = new SolidColorBrush(Color.FromRgb(0xCD, 0xD2, 0xDC));
                res["GamepadBadgeFg"] = new SolidColorBrush(Color.FromRgb(0x20, 0x22, 0x28));
            }

            res["PlayStationBlue"] = new SolidColorBrush(Color.FromRgb(0x00, 0x70, 0xD1));
            res["PlayStationBluePressed"] = new SolidColorBrush(Color.FromRgb(0x00, 0x64, 0xB7));
            res["PlayStationBlueActive"] = new SolidColorBrush(Color.FromRgb(0x00, 0x4D, 0x8D));
            res["TextOnPrimary"] = new SolidColorBrush(Colors.White);
        }
    }
}
