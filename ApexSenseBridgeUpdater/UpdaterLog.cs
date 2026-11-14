using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace ApexSenseBridgeUpdater
{
    internal static class UpdaterLog
    {
        private const long MaxBytes = 512 * 1024;
        private static readonly List<string> pending = new List<string>();
        private static string logPath;
        private static string[] redactions;

        public static void Initialize(string directory)
        {
            var names = new List<string>();
            AddRedaction(names, Environment.GetFolderPath(Environment.SpecialFolder.UserProfile));
            AddRedaction(names, Environment.UserName);
            AddRedaction(names, Environment.MachineName);
            names.Sort(delegate(string a, string b) { return b.Length.CompareTo(a.Length); });
            redactions = names.ToArray();
            try
            {
                if (SafePaths.IsReparsePoint(directory)) return;
                var candidate = Path.Combine(directory, "update.log");
                if (File.Exists(candidate))
                {
                    if (SafePaths.IsReparsePoint(candidate)) return;
                    if (new FileInfo(candidate).Length > MaxBytes) File.Delete(candidate);
                }
                logPath = candidate;
            }
            catch
            {
                logPath = null;
            }
            Flush();
        }

        public static void Info(string message)
        {
            pending.Add(DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ") + " " + Sanitize(message));
            Flush();
        }

        private static void Flush()
        {
            if (logPath == null) return;
            try
            {
                using (var stream = new FileStream(logPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
                using (var writer = new StreamWriter(stream, new UTF8Encoding(false)))
                {
                    foreach (var line in pending) writer.WriteLine(line);
                }
                pending.Clear();
            }
            catch
            {
            }
        }

        private static void AddRedaction(List<string> names, string value)
        {
            if (!string.IsNullOrWhiteSpace(value) && value.Length >= 3 && !names.Contains(value)) names.Add(value);
        }

        private static string Sanitize(string message)
        {
            if (message == null) return string.Empty;
            var text = message.Replace("\r", " ").Replace("\n", " ");
            if (redactions != null)
            {
                foreach (var name in redactions) text = Replace(text, name, "<redacted>");
            }
            return text;
        }

        private static string Replace(string text, string value, string replacement)
        {
            var index = text.IndexOf(value, StringComparison.OrdinalIgnoreCase);
            while (index >= 0)
            {
                text = text.Substring(0, index) + replacement + text.Substring(index + value.Length);
                index = text.IndexOf(value, index + replacement.Length, StringComparison.OrdinalIgnoreCase);
            }
            return text;
        }
    }
}
