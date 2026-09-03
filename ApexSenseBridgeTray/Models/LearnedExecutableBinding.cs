namespace ApexSenseBridgeTray.Models
{
    public sealed class LearnedExecutableBinding
    {
        public string Path { get; set; }
        public string Executable { get; set; }
        public string GameTitle { get; set; }
        public string GameNormalized { get; set; }
        public int SteamAppId { get; set; }
        public string DetectionMethod { get; set; }
        public string FirstSeenUtc { get; set; }
        public string LastSeenUtc { get; set; }
        public int SuccessfulSessions { get; set; }

        public LearnedExecutableBinding()
        {
            Path = string.Empty;
            Executable = string.Empty;
            GameTitle = string.Empty;
            GameNormalized = string.Empty;
            DetectionMethod = string.Empty;
            FirstSeenUtc = string.Empty;
            LastSeenUtc = string.Empty;
        }

        public LearnedExecutableBinding Clone()
        {
            return new LearnedExecutableBinding
            {
                Path = Path,
                Executable = Executable,
                GameTitle = GameTitle,
                GameNormalized = GameNormalized,
                SteamAppId = SteamAppId,
                DetectionMethod = DetectionMethod,
                FirstSeenUtc = FirstSeenUtc,
                LastSeenUtc = LastSeenUtc,
                SuccessfulSessions = SuccessfulSessions
            };
        }
    }
}
