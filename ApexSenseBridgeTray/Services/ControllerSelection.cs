using System.Globalization;

namespace ApexSenseBridgeTray.Services
{
    internal static class ControllerSelection
    {
        internal static string DeviceIndexArgument(string selectedKey)
        {
            return DeviceIndexArgument(selectedKey, ControllerDetectionService.ReadCandidate);
        }

        internal static string DeviceIndexArgument(string selectedKey, ControllerDetectionService.CandidateReader reader)
        {
            if (string.IsNullOrEmpty(selectedKey)) return string.Empty;
            string candidate;
            if (reader(out candidate) != ControllerDetectionService.Ambiguous) return string.Empty;
            var fingerprints = ControllerDetectionService.SplitCandidates(candidate);
            for (int i = 0; i < fingerprints.Length; i++)
                if (string.Equals(ControllerDetectionService.KeyOf(fingerprints[i]), selectedKey, System.StringComparison.Ordinal))
                    return " " + i.ToString(CultureInfo.InvariantCulture);
            return string.Empty;
        }
    }
}
