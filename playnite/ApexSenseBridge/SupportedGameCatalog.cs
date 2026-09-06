using Playnite.SDK.Models;
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;
using System.Web.Script.Serialization;

namespace ApexSenseBridge
{
    internal static class SupportedGameCatalog
    {
        private sealed class Entry
        {
            public string Title { get; set; }
            public string Profile { get; set; }
            public int SteamAppId { get; set; }
            public bool SteamAppIdVerified { get; set; }
        }

        private static readonly object SyncRoot = new object();
        private static Dictionary<string, Entry> byName;
        private static Dictionary<int, Entry> bySteamAppId;

        public static bool TryResolve(
            Game game, out BridgeProfileType profileType, out string reason)
        {
            profileType = BridgeProfileType.StandardDualSense;
            reason = null;
            if (game == null) return false;
            EnsureLoaded();

            Entry entry;
            var normalizedName = Normalize(game.Name);
            if (!string.IsNullOrEmpty(normalizedName) &&
                byName.TryGetValue(normalizedName, out entry))
            {
                profileType = ParseProfile(entry.Profile);
                reason = "catalogue vérifié (« " + entry.Title + " »)";
                return true;
            }

            int steamAppId;
            if (game.Source != null &&
                string.Equals(game.Source.Name, "Steam", StringComparison.OrdinalIgnoreCase) &&
                !string.IsNullOrWhiteSpace(game.GameId) &&
                int.TryParse(game.GameId, out steamAppId) &&
                bySteamAppId.TryGetValue(steamAppId, out entry))
            {
                profileType = ParseProfile(entry.Profile);
                reason = "Steam AppID vérifié " + steamAppId;
                return true;
            }

            var folder = GetInstallFolderName(game.InstallDirectory);
            if (!string.IsNullOrEmpty(folder) && byName.TryGetValue(folder, out entry))
            {
                profileType = ParseProfile(entry.Profile);
                reason = "dossier reconnu (« " + entry.Title + " »)";
                return true;
            }
            return false;
        }

        private static void EnsureLoaded()
        {
            if (byName != null) return;
            lock (SyncRoot)
            {
                if (byName != null) return;
                var names = new Dictionary<string, Entry>(StringComparer.OrdinalIgnoreCase);
                var steamIds = new Dictionary<int, Entry>();
                try
                {
                    var assembly = Assembly.GetExecutingAssembly();
                    using (var stream = assembly.GetManifestResourceStream(
                        "ApexSenseBridge.supported_games.json"))
                    using (var reader = stream == null
                        ? null
                        : new StreamReader(stream, Encoding.UTF8))
                    {
                        if (reader != null)
                        {
                            var serializer = new JavaScriptSerializer
                            {
                                MaxJsonLength = int.MaxValue
                            };
                            var root = serializer.Deserialize<Dictionary<string, object>>(
                                reader.ReadToEnd());
                            var games = root != null && root.ContainsKey("games")
                                ? root["games"] as IEnumerable
                                : null;
                            if (games != null)
                            {
                                foreach (var rawItem in games)
                                {
                                    var item = rawItem as Dictionary<string, object>;
                                    var entry = ParseEntry(item);
                                    if (entry == null) continue;
                                    names[Normalize(entry.Title)] = entry;
                                    if (entry.SteamAppIdVerified && entry.SteamAppId > 0)
                                    {
                                        steamIds[entry.SteamAppId] = entry;
                                    }
                                }
                            }
                        }
                    }
                }
                catch
                {
                    // The small built-in name detector remains available if
                    // an installation has a missing or damaged catalogue.
                }
                byName = names;
                bySteamAppId = steamIds;
            }
        }

        private static Entry ParseEntry(Dictionary<string, object> item)
        {
            if (item == null || !item.ContainsKey("title") || item["title"] == null)
                return null;
            var entry = new Entry
            {
                Title = item["title"].ToString(),
                Profile = item.ContainsKey("profile") && item["profile"] != null
                    ? item["profile"].ToString()
                    : "standard"
            };
            int steamAppId;
            if (item.ContainsKey("steamAppId") && item["steamAppId"] != null &&
                int.TryParse(item["steamAppId"].ToString(), out steamAppId))
            {
                entry.SteamAppId = steamAppId;
            }
            entry.SteamAppIdVerified = item.ContainsKey("steamAppIdVerified") &&
                item["steamAppIdVerified"] != null &&
                Convert.ToBoolean(item["steamAppIdVerified"]);
            return entry;
        }

        private static BridgeProfileType ParseProfile(string profile)
        {
            switch ((profile ?? string.Empty).Trim().ToLowerInvariant())
            {
                case "spider-man-2": return BridgeProfileType.SpiderMan2;
                case "miles-morales": return BridgeProfileType.MilesMorales;
                case "ghost-of-tsushima": return BridgeProfileType.GhostOfTsushima;
                case "warframe": return BridgeProfileType.Warframe;
                default: return BridgeProfileType.StandardDualSense;
            }
        }

        private static string GetInstallFolderName(string installDirectory)
        {
            if (string.IsNullOrWhiteSpace(installDirectory)) return string.Empty;
            try
            {
                return Normalize(Path.GetFileName(installDirectory.TrimEnd(
                    Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)));
            }
            catch
            {
                return string.Empty;
            }
        }

        private static string Normalize(string value)
        {
            if (string.IsNullOrWhiteSpace(value)) return string.Empty;
            var result = new StringBuilder(value.Length);
            foreach (var character in value)
            {
                if (char.IsLetterOrDigit(character))
                    result.Append(char.ToLowerInvariant(character));
            }
            return result.ToString();
        }
    }
}
