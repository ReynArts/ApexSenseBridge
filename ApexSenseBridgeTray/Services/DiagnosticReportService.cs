using ApexSenseBridge.Common;
using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;

namespace ApexSenseBridgeTray.Services
{
    /// <summary>
    /// Builds a bounded, anonymized support snapshot. This service has no timer,
    /// background worker or network access and is called only from the report UI.
    /// </summary>
    internal static class DiagnosticReportService
    {
        private const string InstallerRegistryPath = @"SOFTWARE\ApexSenseBridge";
        private static readonly string[] KnownLogFiles =
        {
            "tray_crash.log",
            "tray_startup_error.log",
            "tray_bridge.log",
            "tray_detection.log"
        };

        public static string BuildMarkdown(
            TraySettings settings,
            EngineSessionManager sessionManager,
            string controllerStatus,
            string reportId)
        {
            var report = new StringBuilder(4096);
            report.AppendLine("<details>");
            report.AppendLine("<summary>Installation diagnostics (shared with user consent)</summary>");
            report.AppendLine();
            report.AppendLine("```text");
            report.AppendLine("Report ID: " + Safe(reportId));
            report.AppendLine("Generated (UTC): " + DateTime.UtcNow.ToString("yyyy-MM-dd HH:mm:ss 'UTC'", CultureInfo.InvariantCulture));
            report.AppendLine();

            AppendSystemInformation(report);
            AppendInstallationInformation(report);
            AppendRuntimeInformation(report, settings, sessionManager, controllerStatus);
            AppendRelevantLogExcerpt(report);

            report.AppendLine("```");
            report.AppendLine("</details>");
            return report.ToString().TrimEnd();
        }

        private static void AppendSystemInformation(StringBuilder report)
        {
            report.AppendLine("[System]");
            report.AppendLine("Windows: " + GetWindowsDescription());
            report.AppendLine("OS architecture: " + (Environment.Is64BitOperatingSystem ? "64-bit" : "32-bit"));
            report.AppendLine("Tray process: " + (Environment.Is64BitProcess ? "64-bit" : "32-bit"));
            report.AppendLine(".NET runtime: " + Safe(Environment.Version.ToString()));
            report.AppendLine();
        }

        private static void AppendInstallationInformation(StringBuilder report)
        {
            string enginePath = InstallLocator.ResolveEngine();
            string appDirectory = AppDomain.CurrentDomain.BaseDirectory;
            bool registeredInstall = HasRegistryKey(InstallerRegistryPath);

            report.AppendLine("[Installation]");
            report.AppendLine("Mode: " + (registeredInstall ? "Installer" : "Portable/development"));
            report.AppendLine("Registered version: " + ReadInstallerValue("Version", "not registered"));
            report.AppendLine("USBip registered version: " + ReadInstallerValue("UsbipVersion", "not registered"));
            report.AppendLine("HidHide registered version: " + ReadInstallerValue("HidHideVersion", "not registered"));
            report.AppendLine("HidHide service key: " + GetServiceRegistration("HidHide"));
            report.AppendLine("USBip UDE service key: " + GetServiceRegistration("usbip2_ude"));
            report.AppendLine("USBip filter service key: " + GetServiceRegistration("usbip2_filter"));
            AppendFileInformation(report, "Tray", Assembly.GetExecutingAssembly().Location);
            AppendFileInformation(report, "Engine", enginePath);
            AppendFileInformation(report, "viiper.exe", FindCompanionFile("viiper.exe", enginePath, appDirectory));
            AppendFileInformation(report, "libVIIPER.dll", FindCompanionFile("libVIIPER.dll", enginePath, appDirectory));
            report.AppendLine();
        }

        private static void AppendRuntimeInformation(
            StringBuilder report,
            TraySettings settings,
            EngineSessionManager sessionManager,
            string controllerStatus)
        {
            report.AppendLine("[Runtime]");
            report.AppendLine("Controller state: " + SafeControllerStatus(controllerStatus));
            report.AppendLine("Bridge session active: " + YesNo(sessionManager != null && sessionManager.IsSessionActive));
            report.AppendLine("Bridge session healthy: " + YesNo(sessionManager != null && sessionManager.IsSessionHealthy));
            report.AppendLine("Active profile: " + Safe(sessionManager != null ? sessionManager.ActiveProfile : "unavailable"));
            report.AppendLine("Active game: " + Safe(sessionManager != null ? sessionManager.ActiveGameTitle : "unavailable"));

            if (settings != null)
            {
                report.AppendLine("Auto detection: " + YesNo(settings.AutoDetectGames));
                report.AppendLine("Adaptive-trigger criterion: " + YesNo(settings.TriggerOnAdaptiveTriggers));
                report.AppendLine("Haptic criterion: " + YesNo(settings.TriggerOnHapticFeedback));
                report.AppendLine("Notifications: " + YesNo(settings.EnableNotifications));
                report.AppendLine("Controller calibration profiles (selected by verified hardware at launch):");
                report.AppendLine(settings.BuildControllerCalibrationArguments().Trim());
                report.AppendLine("Initialization timeout: " + settings.InitializationTimeoutSeconds.ToString(CultureInfo.InvariantCulture) + "s");
                report.AppendLine("Forced profile: " + Safe(settings.ForcedProfile));
                report.AppendLine("UI language: " + Safe(LocalizationManager.CurrentLanguage));
            }
            else
            {
                report.AppendLine("Settings: unavailable");
            }
            report.AppendLine();
        }

        private static void AppendRelevantLogExcerpt(StringBuilder report)
        {
            var collected = new List<string>();
            foreach (string fileName in KnownLogFiles)
            {
                string path = Path.Combine(AppLog.DefaultDirectory, fileName);
                foreach (string line in ReadTailLines(path, 6))
                {
                    if (!IsRelevantLogLine(line)) continue;
                    collected.Add(fileName + ": " + Sanitize(line, 180));
                }
            }

            report.AppendLine("[Recent relevant log lines]");
            if (collected.Count == 0)
            {
                report.AppendLine("No recent warning or error found in the bounded log sample.");
            }
            else
            {
                foreach (string line in collected.Take(8)) report.AppendLine(line);
            }
            report.AppendLine();
        }

        private static IEnumerable<string> ReadTailLines(string path, int maximumLines)
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path)) return Enumerable.Empty<string>();
            try
            {
                const int maximumBytes = 16 * 1024;
                using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                {
                    long start = Math.Max(0, stream.Length - maximumBytes);
                    stream.Seek(start, SeekOrigin.Begin);
                    using (var reader = new StreamReader(stream, Encoding.UTF8, true, 1024, false))
                    {
                        if (start > 0) reader.ReadLine();
                        string text = reader.ReadToEnd();
                        return text.Split(new[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries)
                            .TakeLastCompat(maximumLines)
                            .ToArray();
                    }
                }
            }
            catch
            {
                return Enumerable.Empty<string>();
            }
        }

        private static bool IsRelevantLogLine(string line)
        {
            if (string.IsNullOrWhiteSpace(line)) return false;
            return line.IndexOf("error", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   line.IndexOf("warn", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   line.IndexOf("fail", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   line.IndexOf("exception", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   line.IndexOf("timeout", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private static void AppendFileInformation(StringBuilder report, string label, string path)
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                report.AppendLine(label + ": missing");
                return;
            }

            try
            {
                var info = FileVersionInfo.GetVersionInfo(path);
                string version = !string.IsNullOrWhiteSpace(info.FileVersion)
                    ? info.FileVersion
                    : "version unavailable";
                report.AppendLine(label + ": present, " + Safe(version) + ", " + new FileInfo(path).Length.ToString(CultureInfo.InvariantCulture) + " bytes");
            }
            catch
            {
                report.AppendLine(label + ": present, metadata unavailable");
            }
        }

        private static string FindCompanionFile(string fileName, params string[] relatedPaths)
        {
            foreach (string relatedPath in relatedPaths)
            {
                if (string.IsNullOrWhiteSpace(relatedPath)) continue;
                try
                {
                    string directory = Directory.Exists(relatedPath) ? relatedPath : Path.GetDirectoryName(relatedPath);
                    if (string.IsNullOrWhiteSpace(directory)) continue;
                    string candidate = Path.Combine(directory, fileName);
                    if (File.Exists(candidate)) return candidate;
                }
                catch
                {
                }
            }
            return string.Empty;
        }

        private static string GetWindowsDescription()
        {
            try
            {
                using (var key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion", false))
                {
                    if (key != null)
                    {
                        string product = Convert.ToString(key.GetValue("ProductName"), CultureInfo.InvariantCulture);
                        string display = Convert.ToString(key.GetValue("DisplayVersion"), CultureInfo.InvariantCulture);
                        string build = Convert.ToString(key.GetValue("CurrentBuildNumber"), CultureInfo.InvariantCulture);
                        string ubr = Convert.ToString(key.GetValue("UBR"), CultureInfo.InvariantCulture);
                        int buildNumber;
                        if (int.TryParse(build, NumberStyles.Integer, CultureInfo.InvariantCulture, out buildNumber) &&
                            buildNumber >= 22000 &&
                            product.IndexOf("Windows 10", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            product = product.Replace("Windows 10", "Windows 11");
                        }
                        return Safe(string.Format(CultureInfo.InvariantCulture, "{0} {1} (build {2}.{3})", product, display, build, ubr).Trim());
                    }
                }
            }
            catch
            {
            }
            return Safe(Environment.OSVersion.ToString());
        }

        private static bool HasRegistryKey(string subKey)
        {
            foreach (RegistryView view in RegistryViews())
            {
                try
                {
                    using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                    using (var key = baseKey.OpenSubKey(subKey, false))
                    {
                        if (key != null) return true;
                    }
                }
                catch
                {
                }
            }
            return false;
        }

        private static string ReadInstallerValue(string valueName, string fallback)
        {
            foreach (RegistryView view in RegistryViews())
            {
                try
                {
                    using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                    using (var key = baseKey.OpenSubKey(InstallerRegistryPath, false))
                    {
                        if (key == null) continue;
                        string value = Convert.ToString(key.GetValue(valueName), CultureInfo.InvariantCulture);
                        if (!string.IsNullOrWhiteSpace(value)) return Safe(value);
                    }
                }
                catch
                {
                }
            }
            return fallback;
        }

        private static string GetServiceRegistration(string serviceName)
        {
            try
            {
                using (var key = Registry.LocalMachine.OpenSubKey(
                    @"SYSTEM\CurrentControlSet\Services\" + serviceName, false))
                {
                    if (key == null) return "missing";
                    object start = key.GetValue("Start");
                    return start == null ? "registered" : "registered (Start=" + start + ")";
                }
            }
            catch
            {
                return "unavailable";
            }
        }

        private static IEnumerable<RegistryView> RegistryViews()
        {
            if (Environment.Is64BitOperatingSystem)
            {
                yield return RegistryView.Registry64;
                yield return RegistryView.Registry32;
            }
            else
            {
                yield return RegistryView.Default;
            }
        }

        private static string SafeControllerStatus(string status)
        {
            if (string.Equals(status, "apex4", StringComparison.OrdinalIgnoreCase)) return "Flydigi Apex 4";
            if (string.Equals(status, "apex5", StringComparison.OrdinalIgnoreCase)) return "Flydigi Apex 5";
            if (string.Equals(status, "apex6", StringComparison.OrdinalIgnoreCase)) return "Flydigi Apex 6 Pro";
            return Safe(string.IsNullOrWhiteSpace(status) ? "unavailable" : status);
        }

        private static string YesNo(bool value)
        {
            return value ? "yes" : "no";
        }

        private static string Safe(string value)
        {
            return Sanitize(value, 240);
        }

        private static string Sanitize(string value, int maximumLength)
        {
            string safe = value ?? string.Empty;
            safe = ReplaceIgnoreCase(safe, Environment.UserName, "<user>");
            safe = ReplaceIgnoreCase(safe, Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "%USERPROFILE%");
            safe = ReplaceIgnoreCase(safe, Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "%LOCALAPPDATA%");
            safe = ReplaceIgnoreCase(safe, AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\', '/'), "<install-directory>");
            safe = safe.Replace("\r", " ").Replace("\n", " ").Trim();
            if (safe.Length > maximumLength) safe = safe.Substring(0, maximumLength) + "...";
            return safe;
        }

        private static string ReplaceIgnoreCase(string value, string oldValue, string newValue)
        {
            if (string.IsNullOrEmpty(value) || string.IsNullOrEmpty(oldValue)) return value;
            int index = value.IndexOf(oldValue, StringComparison.OrdinalIgnoreCase);
            while (index >= 0)
            {
                value = value.Substring(0, index) + newValue + value.Substring(index + oldValue.Length);
                index = value.IndexOf(oldValue, index + newValue.Length, StringComparison.OrdinalIgnoreCase);
            }
            return value;
        }

        private static IEnumerable<T> TakeLastCompat<T>(this IEnumerable<T> source, int count)
        {
            if (source == null || count <= 0) return Enumerable.Empty<T>();
            var queue = new Queue<T>(count);
            foreach (T item in source)
            {
                if (queue.Count == count) queue.Dequeue();
                queue.Enqueue(item);
            }
            return queue;
        }
    }
}
