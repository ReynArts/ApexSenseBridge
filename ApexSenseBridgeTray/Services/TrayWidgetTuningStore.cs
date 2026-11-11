using ApexSenseBridge.Common;
using ApexSenseBridgeTray.Models;
using System;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class TrayWidgetTuningStore : IWidgetTuningStore
    {
        private readonly TraySettings settings;
        private readonly Func<string, string> normalizeTitle;
        private readonly Action persist;

        internal TrayWidgetTuningStore(TraySettings settings, Func<string, string> normalizeTitle, Action persist)
        {
            this.settings = settings;
            this.normalizeTitle = normalizeTitle ?? (title => title);
            this.persist = persist;
        }

        public bool SaveGameTuning(string game, uint model, uint? trigger, uint? vibration, SessionTuningState applied)
        {
            if (settings == null || persist == null || string.IsNullOrWhiteSpace(game)) return false;
            var key = normalizeTitle(game);
            var modelName = model == 6 ? "apex6" : model == 4 ? "apex4" : "apex5";
            var tuning = settings.GetGameTuning(key) ?? settings.GetGameTuning(game);
            if (tuning == null)
            {
                var calibration = settings.GetControllerCalibration(modelName);
                if (calibration == null) return false;
                tuning = new GameTuning
                {
                    TriggerStrengthPercent = calibration.TriggerStrengthPercent,
                    VibrationStrengthPercent = calibration.VibrationStrengthPercent,
                    HapticThresholdPercent = calibration.HapticThresholdPercent
                };
            }
            tuning.TriggerStrengthPercent = (int)(trigger ?? applied.TriggerStrengthPercent);
            tuning.VibrationStrengthPercent = (int)(vibration ?? applied.VibrationStrengthPercent);
            settings.SetGameTuning(key, game, tuning);
            persist();
            return true;
        }
    }
}