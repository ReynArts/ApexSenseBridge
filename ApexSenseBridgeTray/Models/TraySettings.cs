using System;
using System.Collections.Generic;
using System.IO;
using System.Web.Script.Serialization;

namespace ApexSenseBridgeTray.Models
{
    public class TraySettings
    {
        public bool AutoDetectGames { get; set; }
        public bool TriggerOnAdaptiveTriggers { get; set; }
        public bool TriggerOnHapticFeedback { get; set; }
        public bool EnableNotifications { get; set; }
        public bool EnableRumble { get; set; }
        public int HapticThresholdPercent { get; set; }
        public int InitializationTimeoutSeconds { get; set; }
        public string ForcedProfile { get; set; }
        public string Language { get; set; }
        public List<string> ExcludedGames { get; set; }

        public TraySettings()
        {
            AutoDetectGames = true;
            TriggerOnAdaptiveTriggers = true;
            TriggerOnHapticFeedback = true;
            EnableNotifications = true;
            EnableRumble = true;
            HapticThresholdPercent = 12;
            InitializationTimeoutSeconds = 20;
            ForcedProfile = "none";
            Language = "auto";
            ExcludedGames = new List<string>();
        }

        public bool IsGameExcluded(string normalizedOrTitle)
        {
            if (string.IsNullOrWhiteSpace(normalizedOrTitle) || ExcludedGames == null) return false;
            foreach (var item in ExcludedGames)
            {
                if (string.Equals(item, normalizedOrTitle, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
            return false;
        }

        public void SetGameExcluded(string normalizedOrTitle, bool excluded)
        {
            if (string.IsNullOrWhiteSpace(normalizedOrTitle)) return;
            if (ExcludedGames == null) ExcludedGames = new List<string>();

            for (int i = ExcludedGames.Count - 1; i >= 0; i--)
            {
                if (string.Equals(ExcludedGames[i], normalizedOrTitle, StringComparison.OrdinalIgnoreCase))
                {
                    if (!excluded)
                    {
                        ExcludedGames.RemoveAt(i);
                    }
                    else
                    {
                        return;
                    }
                }
            }

            if (excluded)
            {
                ExcludedGames.Add(normalizedOrTitle);
            }
        }

        private static string SettingsFilePath
        {
            get
            {
                return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                    "ApexSenseBridge", "tray_settings.json");
            }
        }

        public static TraySettings Load()
        {
            try
            {
                var path = SettingsFilePath;
                if (File.Exists(path))
                {
                    var json = File.ReadAllText(path);
                    var serializer = new JavaScriptSerializer();
                    var settings = serializer.Deserialize<TraySettings>(json);
                    if (settings != null) return settings;
                }
            }
            catch
            {
            }
            return new TraySettings();
        }

        public void Save()
        {
            try
            {
                var path = SettingsFilePath;
                var dir = Path.GetDirectoryName(path);
                if (!Directory.Exists(dir))
                {
                    Directory.CreateDirectory(dir);
                }
                var serializer = new JavaScriptSerializer();
                var json = serializer.Serialize(this);
                File.WriteAllText(path, json);
            }
            catch
            {
            }
        }
    }
}
