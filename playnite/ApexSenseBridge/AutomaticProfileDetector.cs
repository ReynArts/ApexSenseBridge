using Playnite.SDK.Models;
using System;
using System.IO;
using System.Text;

namespace ApexSenseBridge
{
    internal static class AutomaticProfileDetector
    {
        public static bool TryDetect(Game game, out BridgeProfileType profileType, out string reason)
        {
            profileType = BridgeProfileType.StandardDualSense;
            reason = null;
            if (game == null)
            {
                return false;
            }

            if (TryDetectValue(game.Name, out profileType))
            {
                reason = "nom Playnite « " + game.Name + " »";
                return true;
            }

            var installFolder = GetInstallFolderName(game.InstallDirectory);
            if (TryDetectValue(installFolder, out profileType))
            {
                reason = "dossier d'installation « " + installFolder + " »";
                return true;
            }

            return false;
        }

        internal static bool TryDetectValue(string value, out BridgeProfileType profileType)
        {
            profileType = BridgeProfileType.StandardDualSense;
            var normalized = Normalize(value);
            if (string.IsNullOrEmpty(normalized))
            {
                return false;
            }
            if (normalized.Contains("soundtrack") || normalized.Contains("artbook"))
            {
                return false;
            }

            // Put the most specific aliases first. Punctuation, spaces and
            // apostrophe variants have already been removed by Normalize.
            if (normalized.Contains("milesmorales"))
            {
                profileType = BridgeProfileType.MilesMorales;
                return true;
            }
            if (normalized.Contains("spiderman2"))
            {
                profileType = BridgeProfileType.SpiderMan2;
                return true;
            }
            if (normalized.Contains("ghostoftsushima"))
            {
                profileType = BridgeProfileType.GhostOfTsushima;
                return true;
            }
            if (normalized == "warframe" || normalized.StartsWith("warframe", StringComparison.Ordinal))
            {
                profileType = BridgeProfileType.Warframe;
                return true;
            }
            return false;
        }

        private static string Normalize(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                return string.Empty;
            }

            var result = new StringBuilder(value.Length);
            foreach (var character in value)
            {
                if (char.IsLetterOrDigit(character))
                {
                    result.Append(char.ToLowerInvariant(character));
                }
            }
            return result.ToString();
        }

        private static string GetInstallFolderName(string installDirectory)
        {
            if (string.IsNullOrWhiteSpace(installDirectory))
            {
                return string.Empty;
            }
            try
            {
                return Path.GetFileName(installDirectory.TrimEnd(
                    Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            }
            catch
            {
                return string.Empty;
            }
        }
    }
}
