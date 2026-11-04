using System;
using System.Collections.Generic;

namespace ApexSenseBridgeTray.Services
{
    public sealed class TouchpadRemapEntry
    {
        public string ApexKey { get; set; }
        public string[] ApexArguments { get; set; }
        public string DualSenseKey { get; set; }
        public string UseKey { get; set; }
        public string UseText { get; set; }
    }

    public static class TouchpadRemapGuide
    {
        public static IReadOnlyList<TouchpadRemapEntry> BuildEntries(string profile)
        {
            switch ((profile ?? string.Empty).Trim().ToLowerInvariant())
            {
                case "spider-man-2":
                    return new[]
                    {
                        Entry("Loc_TouchpadCtlPress", "View", "Loc_TouchpadOutClick", null, null),
                        Entry("Loc_TouchpadCtlHold", "View", "Loc_TouchpadOutSwipeLeft", null, "FNSM"),
                        Entry("Loc_TouchpadCtlHold", "D-pad Up", "Loc_TouchpadOutSwipeUpDown", "Loc_TouchpadUseCamera", null)
                    };
                case "miles-morales":
                    return new[]
                    {
                        Entry("Loc_TouchpadCtlPress", "View", "Loc_TouchpadOutClick", null, null),
                        Entry("Loc_TouchpadCtlHold", "View", "Loc_TouchpadOutSwipeLeft", null, "FNSM")
                    };
                case "ghost-of-tsushima":
                    return new[]
                    {
                        Entry("Loc_TouchpadCtlFlick", "D-pad Right", "Loc_TouchpadOutSwipeFlick", "Loc_TouchpadUseWind", null)
                    };
                case "warframe":
                    return new[]
                    {
                        new TouchpadRemapEntry
                        {
                            ApexKey = "Loc_TouchpadCtlHoldPress",
                            ApexArguments = new[] { "RB", "A / B / X / Y" },
                            DualSenseKey = "Loc_TouchpadOutSwipeFour",
                            UseKey = "Loc_TouchpadUseAbilities"
                        }
                    };
                case "death-stranding-2":
                    return new[]
                    {
                        Entry("Loc_TouchpadCtlPress", "View", "Loc_TouchpadOutClickRight", "Loc_TouchpadUseLikes", null),
                        Entry("Loc_TouchpadCtlPress", "LB + Menu", "Loc_TouchpadOutClickLeft", "Loc_TouchpadUsePhoto", null)
                    };
                case "alan-wake-2":
                    return new[]
                    {
                        Entry("Loc_TouchpadCtlPress", "View", "Loc_TouchpadOutClickRight", "Loc_TouchpadUseMindPlace", null),
                        Entry("Loc_TouchpadCtlHold", "View", "Loc_TouchpadOutClickLeft", "Loc_TouchpadUseMap", null)
                    };
                default:
                    return new TouchpadRemapEntry[0];
            }
        }

        private static TouchpadRemapEntry Entry(string apexKey, string control, string dualSenseKey,
                                                string useKey, string useText)
        {
            return new TouchpadRemapEntry
            {
                ApexKey = apexKey,
                ApexArguments = new[] { control },
                DualSenseKey = dualSenseKey,
                UseKey = useKey,
                UseText = useText
            };
        }
    }
}
