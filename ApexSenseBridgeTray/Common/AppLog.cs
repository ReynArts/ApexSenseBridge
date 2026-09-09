using System;
using System.Globalization;
using System.IO;
using System.Text;

namespace ApexSenseBridgeTray.Common
{
    internal static class AppLog
    {
        private static readonly object SyncRoot = new object();

        public static string DefaultDirectory
        {
            get
            {
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "ApexSenseBridge", "logs");
            }
        }

        public static void WriteLine(string fileName, string message)
        {
            WriteLine(DefaultDirectory, fileName, message);
        }

        public static void WriteLine(string directory, string fileName, string message)
        {
            try
            {
                if (string.IsNullOrWhiteSpace(directory) || string.IsNullOrWhiteSpace(fileName))
                    return;

                var safeFileName = Path.GetFileName(fileName);
                if (string.IsNullOrWhiteSpace(safeFileName)) return;

                lock (SyncRoot)
                {
                    if (!Directory.Exists(directory)) Directory.CreateDirectory(directory);
                    File.AppendAllText(
                        Path.Combine(directory, safeFileName),
                        DateTime.Now.ToString("s", CultureInfo.InvariantCulture) + " " +
                        (message ?? string.Empty) + "\r\n",
                        new UTF8Encoding(false));
                }
            }
            catch
            {
                // Logging must never take down process monitoring or the tray UI.
            }
        }
    }
}
