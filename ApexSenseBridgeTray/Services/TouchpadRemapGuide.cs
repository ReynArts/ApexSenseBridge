using System;
using System.Collections.Generic;
using System.Linq;

namespace ApexSenseBridgeTray.Services
{
    public enum TouchpadRemapOutput
    {
        Click,
        ClickRight,
        ClickLeft,
        SwipeLeft,
        SwipeUpDown,
        SwipeFlick,
        SwipeFour
    }

    public sealed class TouchpadRemapEntry
    {
        public string[] Buttons { get; set; }
        public bool Hold { get; set; }
        public TouchpadRemapOutput Output { get; set; }
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
                        Press("View", TouchpadRemapOutput.Click, "Loc_TouchpadOutClick", null, null),
                        Hold("View", TouchpadRemapOutput.SwipeLeft, "Loc_TouchpadOutSwipeLeft", null, "FNSM"),
                        Hold("D-pad Up", TouchpadRemapOutput.SwipeUpDown, "Loc_TouchpadOutSwipeUpDown", "Loc_TouchpadUseCamera", null)
                    };
                case "miles-morales":
                    return new[]
                    {
                        Press("View", TouchpadRemapOutput.Click, "Loc_TouchpadOutClick", null, null),
                        Hold("View", TouchpadRemapOutput.SwipeLeft, "Loc_TouchpadOutSwipeLeft", null, "FNSM")
                    };
                case "ghost-of-tsushima":
                    return new[]
                    {
                        new TouchpadRemapEntry
                        {
                            Buttons = new[] { "DpadRight", "RS" },
                            Hold = true,
                            Output = TouchpadRemapOutput.SwipeFour,
                            ApexKey = "Loc_TouchpadCtlFlick",
                            ApexArguments = new[] { "D-pad Right" },
                            DualSenseKey = "Loc_TouchpadOutSwipeFlick",
                            UseKey = "Loc_TouchpadUseWind"
                        }
                    };
                case "warframe":
                    return new[]
                    {
                        new TouchpadRemapEntry
                        {
                            Buttons = new[] { "RB", "ABXY" },
                            Hold = true,
                            Output = TouchpadRemapOutput.SwipeFour,
                            ApexKey = "Loc_TouchpadCtlHoldPress",
                            ApexArguments = new[] { "RB", "A / B / X / Y" },
                            DualSenseKey = "Loc_TouchpadOutSwipeFour",
                            UseKey = "Loc_TouchpadUseAbilities"
                        }
                    };
                case "death-stranding-2":
                    return new[]
                    {
                        Press("View", TouchpadRemapOutput.ClickRight, "Loc_TouchpadOutClickRight", "Loc_TouchpadUseLikes", null),
                        Press("LB + Menu", TouchpadRemapOutput.ClickLeft, "Loc_TouchpadOutClickLeft", "Loc_TouchpadUsePhoto", null)
                    };
                case "alan-wake-2":
                    return new[]
                    {
                        Press("View", TouchpadRemapOutput.ClickRight, "Loc_TouchpadOutClickRight", "Loc_TouchpadUseMindPlace", null),
                        Hold("View", TouchpadRemapOutput.ClickLeft, "Loc_TouchpadOutClickLeft", "Loc_TouchpadUseMap", null)
                    };
                default:
                    return new TouchpadRemapEntry[0];
            }
        }

        private static TouchpadRemapEntry Press(string control, TouchpadRemapOutput output,
                                                string dualSenseKey, string useKey, string useText)
        {
            return Entry("Loc_TouchpadCtlPress", control, false, output, dualSenseKey, useKey, useText);
        }

        private static TouchpadRemapEntry Hold(string control, TouchpadRemapOutput output,
                                               string dualSenseKey, string useKey, string useText)
        {
            return Entry("Loc_TouchpadCtlHold", control, true, output, dualSenseKey, useKey, useText);
        }

        private static TouchpadRemapEntry Entry(string apexKey, string control, bool hold,
                                                TouchpadRemapOutput output, string dualSenseKey,
                                                string useKey, string useText)
        {
            return new TouchpadRemapEntry
            {
                Buttons = control.Replace("-", "").Split(new[] { " + " }, StringSplitOptions.None).Select(token => token.Replace(" ", "")).ToArray(),
                Hold = hold,
                Output = output,
                ApexKey = apexKey,
                ApexArguments = new[] { control },
                DualSenseKey = dualSenseKey,
                UseKey = useKey,
                UseText = useText
            };
        }
    }
}
