using ApexSenseBridgeTray.Models;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace ApexSenseBridgeTray.Services
{
    public class CloudGameListService
    {
        private const string PrimaryCloudUrl = "https://raw.githubusercontent.com/ReynArts/ApexSenseBridge/main/data/supported_games.json";
        private const string FallbackCdnUrl = "https://cdn.jsdelivr.net/gh/ReynArts/ApexSenseBridge@main/data/supported_games.json";
        private readonly Dictionary<string, SupportedGame> gamesByNormalizedName = new Dictionary<string, SupportedGame>(StringComparer.OrdinalIgnoreCase);
        private readonly List<SupportedGame> allGames = new List<SupportedGame>();
        private readonly object syncRoot = new object();

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

        public DateTime? LastUpdated { get; private set; }

        public event Action GamesUpdated;

        private static string LocalCachePath
        {
            get
            {
                return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                    "ApexSenseBridge", "supported_games.json");
            }
        }

        public void Initialize()
        {
            var localCache = LocalCachePath;
            if (File.Exists(localCache))
            {
                try
                {
                    var json = File.ReadAllText(localCache, Encoding.UTF8);
                    if (ParseAndLoadJson(json))
                    {
                        LastUpdated = File.GetLastWriteTimeUtc(localCache);
                        return;
                    }
                }
                catch
                {
                }
            }

            LoadEmbeddedResource();
        }

        public async Task<bool> FetchLatestFromCloudAsync()
        {
            string[] endpoints = new string[] { PrimaryCloudUrl, FallbackCdnUrl };

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

                foreach (var kvp in gamesByNormalizedName)
                {
                    if (normalized.Contains(kvp.Key) || (kvp.Key.Length > 5 && kvp.Key.Contains(normalized)))
                    {
                        game = kvp.Value;
                        return true;
                    }
                }
            }
            return false;
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

                lock (syncRoot)
                {
                    gamesByNormalizedName.Clear();
                    allGames.Clear();

                    foreach (Dictionary<string, object> item in gamesArray)
                    {
                        var g = new SupportedGame();
                        g.Title = item.ContainsKey("title") && item["title"] != null ? item["title"].ToString() : string.Empty;
                        g.Normalized = item.ContainsKey("normalized") && item["normalized"] != null ? item["normalized"].ToString() : string.Empty;
                        g.AdaptiveTriggers = item.ContainsKey("adaptiveTriggers") && Convert.ToBoolean(item["adaptiveTriggers"]);
                        g.HapticFeedback = item.ContainsKey("hapticFeedback") && Convert.ToBoolean(item["hapticFeedback"]);
                        g.Profile = item.ContainsKey("profile") && item["profile"] != null ? item["profile"].ToString() : "standard";

                        if (string.IsNullOrWhiteSpace(g.Normalized) && !string.IsNullOrWhiteSpace(g.Title))
                        {
                            g.Normalized = Normalize(g.Title);
                        }

                        if (!string.IsNullOrWhiteSpace(g.Normalized))
                        {
                            gamesByNormalizedName[g.Normalized] = g;
                            allGames.Add(g);
                        }
                    }
                }
                return allGames.Count > 0;
            }
            catch
            {
                return false;
            }
        }

        private void LoadEmbeddedResource()
        {
            try
            {
                var assembly = Assembly.GetExecutingAssembly();
                using (var stream = assembly.GetManifestResourceStream("ApexSenseBridgeTray.supported_games.json"))
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

        public static string Normalize(string value)
        {
            if (string.IsNullOrWhiteSpace(value)) return string.Empty;
            var sb = new StringBuilder(value.Length);
            foreach (var c in value)
            {
                if (char.IsLetterOrDigit(c))
                {
                    sb.Append(char.ToLowerInvariant(c));
                }
            }
            return sb.ToString();
        }
    }
}
