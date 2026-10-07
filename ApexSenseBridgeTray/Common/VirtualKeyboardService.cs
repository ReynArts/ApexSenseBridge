using System;
using System.Diagnostics;
using System.IO;

namespace ApexSenseBridgeTray.Common
{
    internal static class VirtualKeyboardService
    {
        public static bool Show()
        {
            string commonProgramFiles = Environment.GetFolderPath(Environment.SpecialFolder.CommonProgramFiles);
            string tabTip = Path.Combine(commonProgramFiles, "microsoft shared", "ink", "TabTip.exe");
            if (TryStart(tabTip)) return true;

            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            return TryStart(Path.Combine(windows, "System32", "osk.exe"));
        }

        private static bool TryStart(string path)
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path)) return false;
            try
            {
                Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
                return true;
            }
            catch
            {
                return false;
            }
        }
    }
}
