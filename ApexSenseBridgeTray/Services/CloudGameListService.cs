using ApexSenseBridgeTray.Models;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace ApexSenseBridgeTray.Services
{
    public class CloudGameListService
    {
        private readonly object syncRoot = new object();
        private readonly Dictionary<string, SupportedGame> gamesByNormalizedName =
            new Dictionary<string, SupportedGame>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<int, SupportedGame> gamesBySteamAppId =
            new Dictionary<int, SupportedGame>();
        private readonly HashSet<int> ambiguousSteamAppIds = new HashSet<int>();
        private readonly List<SupportedGame> allGames = new List<SupportedGame>();
        private volatile Dictionary<string, SupportedGame> gamesByExecutableName =
            new Dictionary<string, SupportedGame>(StringComparer.OrdinalIgnoreCase);

        public event Action GamesUpdated;
        public DateTime? LastUpdated { get; private set; }
        public int TotalGamesLoaded
        {
            get
            {
                lock (syncRoot)
                {
                    return allGames.Count;
                }
            }
        }

        private static string LocalCachePath
        {
            get
            {
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "ApexSenseBridge", "cache", "supported_games.json");
            }
        }

        public void Initialize()
        {
            try
            {
                var cachePath = LocalCachePath;
                if (File.Exists(cachePath))
                {
                    var json = File.ReadAllText(cachePath, Encoding.UTF8);
                    if (ParseAndLoadJson(json))
                    {
                        LastUpdated = File.GetLastWriteTimeUtc(cachePath);
                        return;
                    }
                }
            }
            catch
            {
            }

            LoadEmbeddedDatabase();
        }

        public async Task<bool> FetchLatestFromCloudAsync()
        {
            var endpoints = new[]
            {
                "https://raw.githubusercontent.com/ReynArts/ApexSenseBridge/main/data/supported_games.json",
                "https://cdn.jsdelivr.net/gh/ReynArts/ApexSenseBridge@main/data/supported_games.json"
            };

            foreach (var url in endpoints)
            {
                try
                {
                    using (var client = new HttpClient())
                    {
                        client.Timeout = TimeSpan.FromSeconds(10);
                        client.DefaultRequestHeaders.Add("User-Agent", "ApexSenseBridgeTray/1.0");

                        var response = await client.GetAsync(url);
                        if (!response.IsSuccessStatusCode) continue;

                        var json = await response.Content.ReadAsStringAsync();
                        if (ParseAndLoadJson(json))
                        {
                            LastUpdated = DateTime.UtcNow;
                            try
                            {
                                var cachePath = LocalCachePath;
                                var dir = Path.GetDirectoryName(cachePath);
                                if (!Directory.Exists(dir)) Directory.CreateDirectory(dir);
                                File.WriteAllText(cachePath, json, Encoding.UTF8);
                            }
                            catch
                            {
                            }

                            var handler = GamesUpdated;
                            if (handler != null)
                            {
                                handler();
                            }
                            return true;
                        }
                    }
                }
                catch
                {
                }
            }
            return false;
        }

        public bool TryFindGame(string candidateName, out SupportedGame game)
        {
            game = null;
            if (string.IsNullOrWhiteSpace(candidateName)) return false;

            var normalized = Normalize(candidateName);
            if (string.IsNullOrEmpty(normalized)) return false;

            lock (syncRoot)
            {
                if (gamesByNormalizedName.TryGetValue(normalized, out game))
                {
                    return true;
                }

                SupportedGame bestMatch = null;
                int bestScore = 0;
                bool isAmbiguous = false;

                foreach (var kvp in gamesByNormalizedName)
                {
                    var key = kvp.Key;
                    int score = GetMatchScore(normalized, key);

                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestMatch = kvp.Value;
                        isAmbiguous = false;
                    }
                    else if (score > 0 && score == bestScore && !ReferenceEquals(bestMatch, kvp.Value))
                    {
                        isAmbiguous = true;
                    }
                }

                if (bestMatch != null && !isAmbiguous)
                {
                    game = bestMatch;
                    return true;
                }
            }
            return false;
        }

        public bool TryFindExactGame(string candidateName, out SupportedGame game)
        {
            game = null;
            var normalized = Normalize(candidateName);
            if (string.IsNullOrEmpty(normalized)) return false;

            lock (syncRoot)
            {
                return gamesByNormalizedName.TryGetValue(normalized, out game);
            }
        }

        public bool TryFindBySteamAppId(int steamAppId, out SupportedGame game)
        {
            game = null;
            if (steamAppId <= 0) return false;

            lock (syncRoot)
            {
                return gamesBySteamAppId.TryGetValue(steamAppId, out game);
            }
        }

        public bool TryFindByExecutable(string executablePathOrName, out SupportedGame game)
        {
            game = null;
            var executableName = GetExecutableName(executablePathOrName);
            if (string.IsNullOrEmpty(executableName)) return false;

            // The dictionary is fully built before publication and never mutated afterwards.
            // Reading the volatile snapshot therefore requires no lock or I/O in detection.
            var snapshot = gamesByExecutableName;
            return snapshot.TryGetValue(executableName, out game);
        }

        private static int GetMatchScore(string candidate, string gameName)
        {
            const int minimumFragmentLength = 6;

            if (gameName.Length >= minimumFragmentLength && candidate.Contains(gameName))
            {
                return 1000 + gameName.Length;
            }

            if (candidate.Length >= 8 &&
                candidate.Length * 2 >= gameName.Length &&
                gameName.Contains(candidate))
            {
                return 500 + candidate.Length;
            }

            return 0;
        }

        public IReadOnlyList<SupportedGame> GetAllGames()
        {
            lock (syncRoot)
            {
                return allGames.ToArray();
            }
        }

        private bool ParseAndLoadJson(string json)
        {
            if (string.IsNullOrWhiteSpace(json)) return false;

            try
            {
                var serializer = new JavaScriptSerializer();
                serializer.MaxJsonLength = int.MaxValue;
                var dict = serializer.Deserialize<Dictionary<string, object>>(json);
                if (dict == null || !dict.ContainsKey("games")) return false;

                var gamesArray = dict["games"] as System.Collections.ArrayList;
                if (gamesArray == null) return false;

                var parsedGames = new List<SupportedGame>();
                var parsedByNormalizedName =
                    new Dictionary<string, SupportedGame>(StringComparer.OrdinalIgnoreCase);
                var parsedBySteamAppId = new Dictionary<int, SupportedGame>();
                var parsedAmbiguousSteamAppIds = new HashSet<int>();
                var parsedByExecutableName =
                    new Dictionary<string, SupportedGame>(StringComparer.OrdinalIgnoreCase);
                var ambiguousExecutableNames =
                    new HashSet<string>(StringComparer.OrdinalIgnoreCase);

                foreach (Dictionary<string, object> item in gamesArray)
                {
                    var g = new SupportedGame();
                    g.Title = item.ContainsKey("title") && item["title"] != null ? item["title"].ToString() : string.Empty;
                    var providedNormalized = item.ContainsKey("normalized") && item["normalized"] != null
                        ? Normalize(item["normalized"].ToString())
                        : string.Empty;
                    g.AdaptiveTriggers = item.ContainsKey("adaptiveTriggers") && Convert.ToBoolean(item["adaptiveTriggers"]);
                    g.HapticFeedback = item.ContainsKey("hapticFeedback") && Convert.ToBoolean(item["hapticFeedback"]);
                    g.Profile = item.ContainsKey("profile") && item["profile"] != null ? item["profile"].ToString() : "standard";
                    g.IconUrl = item.ContainsKey("iconUrl") && item["iconUrl"] != null ? item["iconUrl"].ToString() : string.Empty;
                    if (item.ContainsKey("steamAppId") && item["steamAppId"] != null)
                    {
                        int sid;
                        if (int.TryParse(item["steamAppId"].ToString(), out sid)) g.SteamAppId = sid;
                    }
                    g.SteamAppIdVerified = item.ContainsKey("steamAppIdVerified") &&
                        item["steamAppIdVerified"] != null &&
                        Convert.ToBoolean(item["steamAppIdVerified"]);
                    g.Executables = g.SteamAppIdVerified
                        ? ParseExecutables(item)
                        : new string[0];

                    var titleNormalized = Normalize(g.Title);
                    g.Normalized = string.Equals(providedNormalized, titleNormalized, StringComparison.Ordinal)
                        ? providedNormalized
                        : titleNormalized;

                    if (!string.IsNullOrWhiteSpace(g.Normalized))
                    {
                        parsedByNormalizedName[g.Normalized] = g;
                        if (g.SteamAppIdVerified && g.SteamAppId > 0 &&
                            !parsedAmbiguousSteamAppIds.Contains(g.SteamAppId))
                        {
                            if (parsedBySteamAppId.ContainsKey(g.SteamAppId))
                            {
                                parsedBySteamAppId.Remove(g.SteamAppId);
                                parsedAmbiguousSteamAppIds.Add(g.SteamAppId);
                            }
                            else
                            {
                                parsedBySteamAppId[g.SteamAppId] = g;
                            }
                        }
                        IndexExecutables(
                            g, parsedByExecutableName, ambiguousExecutableNames);
                        parsedGames.Add(g);
                    }
                }

                parsedGames.Sort((a, b) => string.Compare(a.Title, b.Title, StringComparison.OrdinalIgnoreCase));

                lock (syncRoot)
                {
                    gamesByNormalizedName.Clear();
                    foreach (var entry in parsedByNormalizedName)
                        gamesByNormalizedName.Add(entry.Key, entry.Value);

                    gamesBySteamAppId.Clear();
                    foreach (var entry in parsedBySteamAppId)
                        gamesBySteamAppId.Add(entry.Key, entry.Value);

                    ambiguousSteamAppIds.Clear();
                    foreach (var appId in parsedAmbiguousSteamAppIds)
                        ambiguousSteamAppIds.Add(appId);

                    allGames.Clear();
                    allGames.AddRange(parsedGames);

                    // Publish only after every entry and collision has been validated.
                    gamesByExecutableName = parsedByExecutableName;
                }
                return true;
            }
            catch
            {
                return false;
            }
        }

        private static string[] ParseExecutables(Dictionary<string, object> item)
        {
            if (!item.ContainsKey("executables") || item["executables"] == null)
                return new string[0];

            var values = item["executables"] as System.Collections.IEnumerable;
            if (values == null || item["executables"] is string)
                return new string[0];

            var result = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var value in values)
            {
                var executableName = GetExecutableName(value as string);
                if (!string.IsNullOrEmpty(executableName)) result.Add(executableName);
            }
            return result.OrderBy(value => value, StringComparer.OrdinalIgnoreCase).ToArray();
        }

        private static void IndexExecutables(
            SupportedGame game,
            IDictionary<string, SupportedGame> index,
            ISet<string> ambiguousNames)
        {
            foreach (var executableName in game.Executables)
            {
                if (ambiguousNames.Contains(executableName)) continue;

                SupportedGame existing;
                if (index.TryGetValue(executableName, out existing) &&
                    !ReferenceEquals(existing, game))
                {
                    index.Remove(executableName);
                    ambiguousNames.Add(executableName);
                }
                else
                {
                    index[executableName] = game;
                }
            }
        }

        private static string GetExecutableName(string executablePathOrName)
        {
            if (string.IsNullOrWhiteSpace(executablePathOrName)) return string.Empty;

            try
            {
                var normalizedPath = executablePathOrName.Trim().Replace('/', '\\');
                var executableName = Path.GetFileName(normalizedPath);
                if (string.IsNullOrWhiteSpace(executableName) ||
                    !executableName.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ||
                    executableName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
                {
                    return string.Empty;
                }
                return executableName;
            }
            catch
            {
                return string.Empty;
            }
        }

        private void LoadEmbeddedDatabase()
        {
            try
            {
                var assembly = Assembly.GetExecutingAssembly();
                var resourceName = "ApexSenseBridgeTray.supported_games.json";

                using (var stream = assembly.GetManifestResourceStream(resourceName))
                {
                    if (stream != null)
                    {
                        using (var reader = new StreamReader(stream, Encoding.UTF8))
                        {
                            var json = reader.ReadToEnd();
                            ParseAndLoadJson(json);
                        }
                    }
                }
            }
            catch
            {
            }
        }

        public static string Normalize(string input)
        {
            if (string.IsNullOrWhiteSpace(input)) return string.Empty;

            var sb = new StringBuilder();
            foreach (var ch in input)
            {
                if (char.IsLetterOrDigit(ch))
                {
                    sb.Append(char.ToLowerInvariant(ch));
                }
            }
            return sb.ToString();
        }
    }
}
