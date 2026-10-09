using ApexSenseBridgeTray.Services;

namespace ApexSenseBridgeTray.Common
{
    internal static class HidHideBusyMessage
    {
        // Localized text for the engine's "HidHide control device is busy" failure; null for any other reason.
        public static string TryLocalize(string reason)
        {
            string[] executables;
            if (!EngineSessionManager.TryParseHidHideBusy(reason, out executables)) return null;
            return executables.Length == 0
                ? LocalizationManager.Get("Loc_HidHideBusyUnknown")
                : LocalizationManager.Format("Loc_HidHideBusy", string.Join(", ", executables));
        }
    }
}
