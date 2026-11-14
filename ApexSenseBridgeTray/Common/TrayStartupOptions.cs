using ApexSenseBridgeTray.Models;
using System;

namespace ApexSenseBridgeTray.Common
{
    internal sealed class TrayStartupOptions
    {
        public const string ShowArgument = "--show";
        public const string ForceAlwaysOnArgument = "--force-always-on";

        public bool ShowDashboard { get; private set; }
        public bool ForceAlwaysOn { get; private set; }

        public static TrayStartupOptions Parse(string[] args)
        {
            var options = new TrayStartupOptions();
            if (args == null) return options;
            foreach (var argument in args)
            {
                if (string.Equals(argument, ShowArgument, StringComparison.OrdinalIgnoreCase))
                    options.ShowDashboard = true;
                else if (string.Equals(argument, ForceAlwaysOnArgument, StringComparison.OrdinalIgnoreCase))
                    options.ForceAlwaysOn = true;
            }
            return options;
        }

        public void ApplyTo(TraySettings settings)
        {
            if (settings != null && ForceAlwaysOn) settings.ForcedProfile = "standard";
        }
    }
}
