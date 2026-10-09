using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace ApexSenseBridgeTray.Common
{
    internal static class WindowCorners
    {
        private const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
        private const int DWMWCP_ROUND = 2;

        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

        public static void ApplyRounded(Window window)
        {
            window.SourceInitialized += (s, e) =>
            {
                try
                {
                    int preference = DWMWCP_ROUND;
                    DwmSetWindowAttribute(new WindowInteropHelper(window).Handle, DWMWA_WINDOW_CORNER_PREFERENCE, ref preference, sizeof(int));
                }
                catch (Exception) { }
            };
        }
    }
}
