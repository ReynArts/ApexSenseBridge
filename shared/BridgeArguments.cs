using System;
using System.Collections.Generic;
using System.Globalization;

namespace ApexSenseBridge.Common
{
    internal static class BridgeArguments
    {
        public static string Build(
            string touchpadProfile,
            bool enableRumble,
            int hapticThresholdPercent,
            bool syncLightbar,
            int apexProfileSlot,
            int triggerStrengthPercent = 100,
            int vibrationStrengthPercent = 100)
        {
            var arguments = new List<string>
            {
                "bridge-triggers",
                "--touchpad-profile",
                NormalizeTouchpadProfile(touchpadProfile)
            };

            arguments.Add("--trigger-strength");
            arguments.Add(Math.Max(0, Math.Min(100, triggerStrengthPercent)).ToString(CultureInfo.InvariantCulture));
            arguments.Add("--vibration-strength");
            arguments.Add(Math.Max(0, Math.Min(100, vibrationStrengthPercent)).ToString(CultureInfo.InvariantCulture));

            if (enableRumble)
            {
                arguments.Add("--rumble");
                arguments.Add("--haptic-threshold");
                arguments.Add(Math.Max(0, Math.Min(95, hapticThresholdPercent))
                    .ToString(CultureInfo.InvariantCulture));
            }

            if (syncLightbar)
            {
                arguments.Add("--sync-lightbar");
            }

            if (apexProfileSlot >= 1 && apexProfileSlot <= 4)
            {
                arguments.Add("--apex-profile");
                arguments.Add(apexProfileSlot.ToString(CultureInfo.InvariantCulture));
            }

            return string.Join(" ", arguments.ToArray());
        }

        private static string NormalizeTouchpadProfile(string profile)
        {
            switch ((profile ?? string.Empty).Trim().ToLowerInvariant())
            {
                case "spider-man-2":
                case "miles-morales":
                case "ghost-of-tsushima":
                case "warframe":
                    return profile.Trim().ToLowerInvariant();
                default:
                    return "none";
            }
        }
    }
}
