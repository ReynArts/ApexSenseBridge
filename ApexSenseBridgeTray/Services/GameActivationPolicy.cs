using ApexSenseBridgeTray.Models;

namespace ApexSenseBridgeTray.Services
{
    public static class GameActivationPolicy
    {
        public static bool ShouldActivate(
            SupportedGame game,
            TraySettings settings,
            string executableTitle,
            string folderName,
            string fileName)
        {
            if (game == null || settings == null) return false;

            if (IsExcluded(
                game, settings, executableTitle, folderName, fileName))
            {
                return false;
            }

            return (settings.TriggerOnAdaptiveTriggers && game.AdaptiveTriggers &&
                    !game.AdaptiveTriggersManualFix) ||
                   (settings.TriggerOnHapticFeedback && game.HapticFeedback &&
                    !game.HapticFeedbackManualFix);
        }

        public static bool IsBlockedByManualFix(
            SupportedGame game,
            TraySettings settings,
            string executableTitle,
            string folderName,
            string fileName)
        {
            if (game == null || settings == null) return false;
            if (IsExcluded(
                game, settings, executableTitle, folderName, fileName)) return false;

            bool requestedManualFeature =
                (settings.TriggerOnAdaptiveTriggers && game.AdaptiveTriggers &&
                 game.AdaptiveTriggersManualFix) ||
                (settings.TriggerOnHapticFeedback && game.HapticFeedback &&
                 game.HapticFeedbackManualFix);
            bool requestedReadyFeature =
                (settings.TriggerOnAdaptiveTriggers && game.AdaptiveTriggers &&
                 !game.AdaptiveTriggersManualFix) ||
                (settings.TriggerOnHapticFeedback && game.HapticFeedback &&
                 !game.HapticFeedbackManualFix);

            return requestedManualFeature && !requestedReadyFeature;
        }

        private static bool IsExcluded(
            SupportedGame game,
            TraySettings settings,
            string executableTitle,
            string folderName,
            string fileName)
        {
            return settings.IsGameExcluded(game.Normalized) ||
                   settings.IsGameExcluded(game.Title) ||
                   settings.IsGameExcluded(executableTitle) ||
                   settings.IsGameExcluded(folderName) ||
                   settings.IsGameExcluded(fileName) ||
                   (game.SteamAppIdVerified && game.SteamAppId > 0 &&
                    settings.IsGameExcluded(game.SteamAppId.ToString()));
        }
    }
}
