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
                res["WindowBackground"] = new SolidColorBrush(Color.FromRgb(0x0E, 0x0E, 0x10));
                res["CardBackground"] = new SolidColorBrush(Color.FromRgb(0x1A, 0x1A, 0x1E));
                res["CardBorder"] = new SolidColorBrush(Color.FromArgb(0x1A, 0xFF, 0xFF, 0xFF));
                res["FooterBackground"] = new SolidColorBrush(Color.FromRgb(0x14, 0x14, 0x18));

                res["ControlBackground"] = new SolidColorBrush(Color.FromRgb(0x2A, 0x2A, 0x30));
                res["ControlBorder"] = new SolidColorBrush(Color.FromRgb(0x3A, 0x3A, 0x42));

                res["TextPrimary"] = new SolidColorBrush(Color.FromRgb(0xF5, 0xF5, 0xF7));
                res["TextSecondary"] = new SolidColorBrush(Color.FromArgb(0xB3, 0xFF, 0xFF, 0xFF));
                res["TextMuted"] = new SolidColorBrush(Color.FromArgb(0x66, 0xFF, 0xFF, 0xFF));

                res["BadgeActiveBg"] = new SolidColorBrush(Color.FromArgb(0x26, 0x34, 0xD3, 0x99));
                res["BadgeActiveFg"] = new SolidColorBrush(Color.FromRgb(0x34, 0xD3, 0x99));
                res["BadgeStandbyBg"] = new SolidColorBrush(Color.FromArgb(0x1A, 0x60, 0xA5, 0xFA));
                res["BadgeStandbyFg"] = new SolidColorBrush(Color.FromRgb(0x60, 0xA5, 0xFA));

                res["FeaturePillBg"] = new SolidColorBrush(Color.FromArgb(0x14, 0xFF, 0xFF, 0xFF));
                res["FeaturePillBorder"] = new SolidColorBrush(Color.FromArgb(0x12, 0xFF, 0xFF, 0xFF));
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
            }

            res["PlayStationBlue"] = new SolidColorBrush(Color.FromRgb(0x00, 0x70, 0xD1));
            res["PlayStationBluePressed"] = new SolidColorBrush(Color.FromRgb(0x00, 0x64, 0xB7));
            res["PlayStationBlueActive"] = new SolidColorBrush(Color.FromRgb(0x00, 0x4D, 0x8D));
            res["TextOnPrimary"] = new SolidColorBrush(Colors.White);
        }
    }
}
