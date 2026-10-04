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
        public bool SyncLightbar { get; set; }
        public int HapticThresholdPercent { get; set; }
        public int TriggerStrengthPercent { get; set; }
        public int VibrationStrengthPercent { get; set; }
        public int Apex4GyroStrengthPercent { get; set; }
        public int Apex4GyroYawStrengthPercent { get; set; }
        public Dictionary<string, string> GameExecutables { get; set; }
        public int InitializationTimeoutSeconds { get; set; }
        // Manual bridge mode describes the current process session, not a
        // durable preference. Persisting it leaves automatic detection paused
        // after a Tray restart without recreating the manual engine session.
        [ScriptIgnore]
        public string ForcedProfile { get; set; }
        public string Language { get; set; }
        public List<string> ExcludedGames { get; set; }
        public Dictionary<string, int> ApexProfileSlots { get; set; }

        public TraySettings()
        {
            AutoDetectGames = true;
            TriggerOnAdaptiveTriggers = true;
            TriggerOnHapticFeedback = true;
            EnableNotifications = true;
            EnableRumble = true;
            SyncLightbar = false;
            HapticThresholdPercent = 12;
            TriggerStrengthPercent = 100;
            VibrationStrengthPercent = 100;
            Apex4GyroStrengthPercent = 100;
            Apex4GyroYawStrengthPercent = 100;
            GameExecutables = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            InitializationTimeoutSeconds = 20;
            ForcedProfile = "none";
            Language = "auto";
            ExcludedGames = new List<string>();
            ApexProfileSlots = new Dictionary<string, int>();
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

        public void SetGameExcludedAliases(string normalized, string title, bool excluded)
        {
            SetGameExcluded(normalized, excluded);
            SetGameExcluded(title, excluded);
        }

        public int GetApexProfileSlot(string normalizedOrTitle)
        {
            if (string.IsNullOrWhiteSpace(normalizedOrTitle) || ApexProfileSlots == null)
            {
                return 0;
            }

            foreach (var item in ApexProfileSlots)
            {
                if (string.Equals(item.Key, normalizedOrTitle, StringComparison.OrdinalIgnoreCase))
                {
                    return item.Value >= 1 && item.Value <= 4 ? item.Value : 0;
                }
            }
            return 0;
        }

        public void SetApexProfileSlot(string normalizedOrTitle, int slot)
        {
            if (string.IsNullOrWhiteSpace(normalizedOrTitle)) return;
            if (slot < 0 || slot > 4)
            {
                throw new ArgumentOutOfRangeException("slot", "The Apex profile slot must be between 0 and 4.");
            }
            if (ApexProfileSlots == null)
            {
                ApexProfileSlots = new Dictionary<string, int>();
            }

            string existingKey = null;
            foreach (var key in ApexProfileSlots.Keys)
            {
                if (string.Equals(key, normalizedOrTitle, StringComparison.OrdinalIgnoreCase))
                {
                    existingKey = key;
                    break;
                }
            }
            if (existingKey != null) ApexProfileSlots.Remove(existingKey);
            if (slot != 0) ApexProfileSlots[normalizedOrTitle.Trim()] = slot;
        }

        internal void ResetTransientState()
        {
            ForcedProfile = "none";
        }

        public string GetGameExecutable(string game)
        {
            var current = GameExecutables;
            if (current == null || string.IsNullOrWhiteSpace(game)) return string.Empty;
            foreach (var entry in current)
                if (string.Equals(entry.Key, game, StringComparison.OrdinalIgnoreCase)) return entry.Value ?? string.Empty;
            return string.Empty;
        }

        public void SetGameExecutable(string game, string executable)
        {
            if (string.IsNullOrWhiteSpace(game)) return;
            var path = string.IsNullOrWhiteSpace(executable) ? string.Empty : NormalizeExecutable(executable);
            if (!string.IsNullOrWhiteSpace(executable) && path == null)
                throw new ArgumentException("Select an absolute .exe path.", "executable");
            var next = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (GameExecutables != null)
                foreach (var item in GameExecutables) next[item.Key] = item.Value;
            next.Remove(game);
            if (path.Length > 0) next[game] = path;
            GameExecutables = next;
        }

        public bool TryGetGameForExecutable(string executable, out string game)
        {
            game = null;
            var path = NormalizeExecutable(executable);
            var current = GameExecutables;
            if (path == null || current == null) return false;
            foreach (var item in current)
            {
                if (!string.Equals(path, NormalizeExecutable(item.Value), StringComparison.OrdinalIgnoreCase)) continue;
                // A path assigned to several games is ambiguous.
                if (game != null) { game = null; return false; }
                game = item.Key;
            }
            return game != null;
        }

        private static string NormalizeExecutable(string path)
        {
            try
            {
                if (string.IsNullOrWhiteSpace(path) || !Path.IsPathRooted(path) ||
                    !string.Equals(Path.GetExtension(path), ".exe", StringComparison.OrdinalIgnoreCase)) return null;
                return Path.GetFullPath(path.Trim());
            }
            catch { return null; }
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
                    if (settings != null)
                    {
                        settings.ResetTransientState();
                        settings.TriggerStrengthPercent = Math.Max(0, Math.Min(100, settings.TriggerStrengthPercent));
                        settings.VibrationStrengthPercent = Math.Max(0, Math.Min(100, settings.VibrationStrengthPercent));
                        settings.Apex4GyroStrengthPercent = Math.Max(25, Math.Min(400, settings.Apex4GyroStrengthPercent));
                        settings.Apex4GyroYawStrengthPercent = Math.Max(25, Math.Min(400, settings.Apex4GyroYawStrengthPercent));
                        settings.HapticThresholdPercent = Math.Max(0, Math.Min(95, settings.HapticThresholdPercent));
                        if (settings.GameExecutables == null) settings.GameExecutables = new Dictionary<string, string>();
                        if (settings.ExcludedGames == null) settings.ExcludedGames = new List<string>();
                        if (settings.ApexProfileSlots == null)
                        {
                            settings.ApexProfileSlots = new Dictionary<string, int>();
                        }
                        return settings;
                    }
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
