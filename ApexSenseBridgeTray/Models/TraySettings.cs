using System;
using System.Collections.Generic;
using System.IO;
using System.Web.Script.Serialization;

namespace ApexSenseBridgeTray.Models
{
    public sealed class ControllerCalibration
    {
        public int TriggerStrengthPercent { get; set; } = 100;
        public int VibrationStrengthPercent { get; set; } = 100;
        public int HapticThresholdPercent { get; set; } = 12;
        public bool EnableRumble { get; set; } = true;
        public bool SyncLightbar { get; set; }
        // Upper limit for the synchronized lightbar. The engine uses the lower
        // of this and the brightness saved in Flydigi Space Station.
        public int LightbarBrightnessPercent { get; set; } = 100;
        public int GyroStrengthPercent { get; set; } = 100;
        public int GyroYawStrengthPercent { get; set; } = 100;

        internal void Normalize(string model)
        {
            TriggerStrengthPercent = Math.Max(0, Math.Min(100, TriggerStrengthPercent));
            VibrationStrengthPercent = Math.Max(0, Math.Min(model == "apex6" ? 100 : 200, VibrationStrengthPercent));
            HapticThresholdPercent = model == "apex6" ? 0 : Math.Max(0, Math.Min(95, HapticThresholdPercent));
            SyncLightbar = model == "apex5" && SyncLightbar;
            LightbarBrightnessPercent = model == "apex5" ? Math.Max(0, Math.Min(100, LightbarBrightnessPercent)) : 100;
            GyroStrengthPercent = model == "apex4" ? Math.Max(25, Math.Min(400, GyroStrengthPercent)) : 100;
            GyroYawStrengthPercent = model == "apex4" ? Math.Max(25, Math.Min(400, GyroYawStrengthPercent)) : 100;
        }
    }

    public sealed class GameTuning
    {
        public int TriggerStrengthPercent { get; set; }
        public int VibrationStrengthPercent { get; set; }
        public int HapticThresholdPercent { get; set; }

        public GameTuning()
        {
            TriggerStrengthPercent = 100;
            VibrationStrengthPercent = 100;
            HapticThresholdPercent = 12;
        }

        public GameTuning Clone()
        {
            return new GameTuning
            {
                TriggerStrengthPercent = Math.Max(0, Math.Min(100, TriggerStrengthPercent)),
                VibrationStrengthPercent = Math.Max(0, Math.Min(200, VibrationStrengthPercent)),
                HapticThresholdPercent = Math.Max(0, Math.Min(95, HapticThresholdPercent))
            };
        }
    }

    public class TraySettings
    {
        public bool AutoDetectGames { get; set; }
        public bool TriggerOnAdaptiveTriggers { get; set; }
        public bool TriggerOnHapticFeedback { get; set; }
        public bool EnableNotifications { get; set; }
        public bool EnableRumble { get; set; }
        public bool SyncLightbar { get; set; }
        public bool ZoneEmulation { get; set; }
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
        // The support callout is introduced once, then never again.
        public bool SupportHintShown { get; set; }
        public List<string> ExcludedGames { get; set; }
        public List<string> ManualFixAllowedGames { get; set; }
        public Dictionary<string, int> ApexProfileSlots { get; set; }
        public Dictionary<string, GameTuning> GameTunings { get; set; }
        // Legacy scalar preferences remain for migration, not as an active
        // cross-model calibration. Never persist the last connected model.
        public Dictionary<string, ControllerCalibration> ControllerCalibrations { get; set; }

        public static bool IsCalibrationModel(string model)
        {
            return model == "apex4" || model == "apex5" || model == "apex6";
        }

        public ControllerCalibration GetControllerCalibration(string model)
        {
            if (!IsCalibrationModel(model)) return null;
            lock (this)
            {
                if (ControllerCalibrations == null)
                    ControllerCalibrations = new Dictionary<string, ControllerCalibration>();
                ControllerCalibration calibration;
                if (!ControllerCalibrations.TryGetValue(model, out calibration) || calibration == null)
                {
                    // Preserve existing users' strengths/rumble on first migration.
                    // Hardware-specific settings only migrate to their own model.
                    calibration = new ControllerCalibration
                    {
                        TriggerStrengthPercent = TriggerStrengthPercent,
                        VibrationStrengthPercent = VibrationStrengthPercent,
                        HapticThresholdPercent = HapticThresholdPercent,
                        EnableRumble = EnableRumble,
                        SyncLightbar = SyncLightbar,
                        GyroStrengthPercent = Apex4GyroStrengthPercent,
                        GyroYawStrengthPercent = Apex4GyroYawStrengthPercent
                    };
                    ControllerCalibrations[model] = calibration;
                }
                calibration.Normalize(model);
                return calibration;
            }
        }

        internal string BuildControllerCalibrationArguments(GameTuning gameTuning = null)
        {
            lock (this)
            {
                var result = new System.Text.StringBuilder();
                var tuning = gameTuning != null ? gameTuning.Clone() : null;
                foreach (var model in new[] { "apex4", "apex5", "apex6" })
                {
                    var c = GetControllerCalibration(model);
                    int trigger = c.TriggerStrengthPercent;
                    int vibration = c.VibrationStrengthPercent;
                    int threshold = c.HapticThresholdPercent;
                    if (tuning != null)
                    {
                        trigger = tuning.TriggerStrengthPercent;
                        vibration = Math.Min(model == "apex6" ? 100 : 200, tuning.VibrationStrengthPercent);
                        if (model != "apex6") threshold = tuning.HapticThresholdPercent;
                    }
                    result.AppendFormat(System.Globalization.CultureInfo.InvariantCulture,
                        " --controller-calibration {0}:{1}:{2}:{3}:{4}:{5}:{6}:{7}:{8}",
                        model, trigger, vibration,
                        threshold, c.EnableRumble ? 1 : 0, c.SyncLightbar ? 1 : 0,
                        c.GyroStrengthPercent, c.GyroYawStrengthPercent, c.LightbarBrightnessPercent);
                }
                return result.ToString();
            }
        }

        public TraySettings()
        {
            AutoDetectGames = true;
            TriggerOnAdaptiveTriggers = true;
            TriggerOnHapticFeedback = true;
            EnableNotifications = true;
            EnableRumble = true;
            SyncLightbar = false;
            ZoneEmulation = true;
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
            ManualFixAllowedGames = new List<string>();
            ApexProfileSlots = new Dictionary<string, int>();
            GameTunings = new Dictionary<string, GameTuning>();
        }

        public GameTuning GetGameTuning(string normalizedOrTitle)
        {
            if (string.IsNullOrWhiteSpace(normalizedOrTitle)) return null;
            lock (this)
            {
                if (GameTunings == null) return null;
                foreach (var item in GameTunings)
                {
                    if (item.Value != null &&
                        string.Equals(item.Key, normalizedOrTitle.Trim(), StringComparison.OrdinalIgnoreCase))
                        return item.Value.Clone();
                }
                return null;
            }
        }

        public void SetGameTuning(string normalized, string title, GameTuning tuning)
        {
            lock (this)
            {
                if (GameTunings == null) GameTunings = new Dictionary<string, GameTuning>();
                foreach (var key in new[] { normalized, title })
                {
                    if (string.IsNullOrWhiteSpace(key)) continue;
                    var trimmed = key.Trim();
                    string existing = null;
                    foreach (var current in GameTunings.Keys)
                    {
                        if (string.Equals(current, trimmed, StringComparison.OrdinalIgnoreCase)) { existing = current; break; }
                    }
                    if (existing != null) GameTunings.Remove(existing);
                    if (tuning != null) GameTunings[trimmed] = tuning.Clone();
                }
            }
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

        public bool IsManualFixAllowed(string normalized)
        {
            if (string.IsNullOrWhiteSpace(normalized) || ManualFixAllowedGames == null) return false;
            foreach (var item in ManualFixAllowedGames)
            {
                if (string.Equals(item, normalized, StringComparison.OrdinalIgnoreCase)) return true;
            }
            return false;
        }

        public void SetManualFixAllowed(string normalized, bool allowed)
        {
            if (string.IsNullOrWhiteSpace(normalized)) return;
            if (ManualFixAllowedGames == null) ManualFixAllowedGames = new List<string>();
            ManualFixAllowedGames.RemoveAll(
                item => string.Equals(item, normalized, StringComparison.OrdinalIgnoreCase));
            if (allowed) ManualFixAllowedGames.Add(normalized);
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
                        settings.VibrationStrengthPercent = Math.Max(0, Math.Min(200, settings.VibrationStrengthPercent));
                        settings.Apex4GyroStrengthPercent = Math.Max(25, Math.Min(400, settings.Apex4GyroStrengthPercent));
                        settings.Apex4GyroYawStrengthPercent = Math.Max(25, Math.Min(400, settings.Apex4GyroYawStrengthPercent));
                        settings.HapticThresholdPercent = Math.Max(0, Math.Min(95, settings.HapticThresholdPercent));
                        if (settings.GameExecutables == null) settings.GameExecutables = new Dictionary<string, string>();
                        if (settings.ExcludedGames == null) settings.ExcludedGames = new List<string>();
                        if (settings.ManualFixAllowedGames == null) settings.ManualFixAllowedGames = new List<string>();
                        if (settings.ApexProfileSlots == null)
                        {
                            settings.ApexProfileSlots = new Dictionary<string, int>();
                        }
                        if (settings.GameTunings == null) settings.GameTunings = new Dictionary<string, GameTuning>();
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
                string json;
                lock (this) json = new JavaScriptSerializer().Serialize(this);
                File.WriteAllText(path, json);
            }
            catch
            {
            }
        }
    }
}
