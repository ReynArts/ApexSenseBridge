using System;

namespace ApexSenseBridgeTray.Models
{
    public class SupportedGame
    {
        public string Title { get; set; }
        public string Normalized { get; set; }
        public bool AdaptiveTriggers { get; set; }
        public bool HapticFeedback { get; set; }
        public bool AdaptiveTriggersManualFix { get; set; }
        public bool HapticFeedbackManualFix { get; set; }
        public bool RequiresManualFix
        {
            get { return AdaptiveTriggersManualFix || HapticFeedbackManualFix; }
        }
        public string ManualFixUrl { get; set; }
        public string Profile { get; set; }
        public string IconUrl { get; set; }
        public int SteamAppId { get; set; }
        public bool SteamAppIdVerified { get; set; }
        public string[] Executables { get; set; }
        /// <summary>Date the game first entered the catalogue (UTC calendar date), when known.</summary>
        public DateTime? AddedAt { get; set; }

        public SupportedGame()
        {
            Title = string.Empty;
            Normalized = string.Empty;
            Profile = "standard";
            IconUrl = string.Empty;
            ManualFixUrl = string.Empty;
            Executables = new string[0];
        }

        public override string ToString()
        {
            return string.Format("{0} (Profile: {1})", Title, Profile);
        }
    }
}
