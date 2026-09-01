using System;

namespace ApexSenseBridgeTray.Models
{
    public class SupportedGame
    {
        public string Title { get; set; }
        public string Normalized { get; set; }
        public bool AdaptiveTriggers { get; set; }
        public bool HapticFeedback { get; set; }
        public string Profile { get; set; }

        public SupportedGame()
        {
            Title = string.Empty;
            Normalized = string.Empty;
            Profile = "standard";
        }

        public override string ToString()
        {
            return string.Format("{0} (Profile: {1})", Title, Profile);
        }
    }
}
