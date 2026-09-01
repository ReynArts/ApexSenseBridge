using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;

namespace ApexSenseBridgeTray.Services
{
    public class UpdateInfo
    {
        public bool HasUpdate { get; set; }
        public string CurrentVersion { get; set; }
        public string LatestVersion { get; set; }
        public string ReleaseNotes { get; set; }
        public string ReleaseUrl { get; set; }
        public string InstallerUrl { get; set; }
    }

    public class UpdateCheckerService
    {
        private const string GitHubApiLatestRelease = "https://api.github.com/repos/ReynArts/ApexSenseBridge/releases/latest";
        private const string GitHubReleasesPage = "https://github.com/ReynArts/ApexSenseBridge/releases";

        public event Action<UpdateInfo> UpdateAvailable;

        public string GetCurrentVersion()
        {
            try
            {
                Version ver = Assembly.GetExecutingAssembly().GetName().Version;
                if (ver != null)
                {
                    return string.Format("{0}.{1}.{2}", ver.Major, ver.Minor, ver.Build);
                }
            }
            catch
            {
            }
            return "0.3.0";
        }

        public async Task<UpdateInfo> CheckForUpdatesAsync(bool silent)
        {
            UpdateInfo info = new UpdateInfo();
            info.CurrentVersion = GetCurrentVersion();
            info.HasUpdate = false;
            info.ReleaseUrl = GitHubReleasesPage;

            try
            {
                using (HttpClient client = new HttpClient())
                {
                    client.Timeout = TimeSpan.FromSeconds(10);
                    client.DefaultRequestHeaders.Add("User-Agent", "ApexSenseBridgeTray/1.0");

                    HttpResponseMessage response = await client.GetAsync(GitHubApiLatestRelease);
                    if (!response.IsSuccessStatusCode)
                    {
                        if (!silent)
                        {
                            MessageBox.Show(
                                "Impossible de vérifier les mises à jour pour le moment.\nVérifiez votre connexion Internet.",
                                "ApexSenseBridge — Mises à jour",
                                MessageBoxButton.OK,
                                MessageBoxImage.Warning);
                        }
                        return info;
                    }

                    string json = await response.Content.ReadAsStringAsync();
                    JavaScriptSerializer serializer = new JavaScriptSerializer();
                    serializer.MaxJsonLength = int.MaxValue;
                    Dictionary<string, object> dict = serializer.Deserialize<Dictionary<string, object>>(json);

                    if (dict != null)
                    {
                        string tagName = dict.ContainsKey("tag_name") && dict["tag_name"] != null
                            ? dict["tag_name"].ToString().TrimStart('v', 'V')
                            : string.Empty;

                        info.LatestVersion = tagName;

                        if (dict.ContainsKey("html_url") && dict["html_url"] != null)
                        {
                            info.ReleaseUrl = dict["html_url"].ToString();
                        }

                        if (dict.ContainsKey("body") && dict["body"] != null)
                        {
                            info.ReleaseNotes = dict["body"].ToString();
                        }

                        // Check for installer asset
                        if (dict.ContainsKey("assets") && dict["assets"] is ArrayList)
                        {
                            ArrayList assets = dict["assets"] as ArrayList;
                            if (assets != null)
                            {
                                foreach (object rawAsset in assets)
                                {
                                    Dictionary<string, object> asset = rawAsset as Dictionary<string, object>;
                                    if (asset != null && asset.ContainsKey("name") && asset["name"] != null)
                                    {
                                        string name = asset["name"].ToString();
                                        if (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) &&
                                            asset.ContainsKey("browser_download_url") &&
                                            asset["browser_download_url"] != null)
                                        {
                                            info.InstallerUrl = asset["browser_download_url"].ToString();
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        if (!string.IsNullOrEmpty(info.LatestVersion))
                        {
                            info.HasUpdate = IsNewerVersion(info.LatestVersion, info.CurrentVersion);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                if (!silent)
                {
                    MessageBox.Show(
                        "Erreur lors de la vérification : " + ex.Message,
                        "ApexSenseBridge — Mises à jour",
                        MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                }
                return info;
            }

            if (info.HasUpdate)
            {
                Action<UpdateInfo> handler = UpdateAvailable;
                if (handler != null)
                {
                    handler(info);
                }

                if (!silent)
                {
                    string msg = string.Format(
                        "Une nouvelle version d'ApexSenseBridge est disponible !\n\n" +
                        "Version actuelle : v{0}\n" +
                        "Dernière version : v{1}\n\n" +
                        "Souhaitez-vous la télécharger maintenant ?",
                        info.CurrentVersion,
                        info.LatestVersion);

                    MessageBoxResult result = MessageBox.Show(
                        msg,
                        "ApexSenseBridge — Mise à jour disponible",
                        MessageBoxButton.YesNo,
                        MessageBoxImage.Information);

                    if (result == MessageBoxResult.Yes)
                    {
                        DownloadOrOpenRelease(info);
                    }
                }
            }
            else if (!silent)
            {
                MessageBox.Show(
                    string.Format("ApexSenseBridge est à jour (version v{0}).", info.CurrentVersion),
                    "ApexSenseBridge — Mises à jour",
                    MessageBoxButton.OK,
                    MessageBoxImage.Information);
            }

            return info;
        }

        public void DownloadOrOpenRelease(UpdateInfo info)
        {
            if (info != null && !string.IsNullOrEmpty(info.InstallerUrl))
            {
                try
                {
                    string tempPath = Path.Combine(Path.GetTempPath(), "ApexSenseBridge-Setup.exe");
                    using (WebClient wc = new WebClient())
                    {
                        wc.DownloadFile(info.InstallerUrl, tempPath);
                    }

                    if (File.Exists(tempPath))
                    {
                        ProcessStartInfo psi = new ProcessStartInfo(tempPath);
                        psi.UseShellExecute = true;
                        Process.Start(psi);
                        return;
                    }
                }
                catch
                {
                }
            }

            // Fallback: open browser to GitHub release page
            string targetUrl = (info != null && !string.IsNullOrEmpty(info.ReleaseUrl)) ? info.ReleaseUrl : GitHubReleasesPage;
            try
            {
                ProcessStartInfo psi = new ProcessStartInfo(targetUrl);
                psi.UseShellExecute = true;
                Process.Start(psi);
            }
            catch
            {
            }
        }

        private bool IsNewerVersion(string latestStr, string currentStr)
        {
            try
            {
                Version latest;
                Version current;

                if (Version.TryParse(latestStr, out latest) && Version.TryParse(currentStr, out current))
                {
                    return latest > current;
                }

                // Fallback basic segment comparison
                string[] lParts = latestStr.Split('.');
                string[] cParts = currentStr.Split('.');
                int maxLen = Math.Max(lParts.Length, cParts.Length);

                for (int i = 0; i < maxLen; i++)
                {
                    int lNum = 0;
                    int cNum = 0;
                    if (i < lParts.Length) int.TryParse(lParts[i], out lNum);
                    if (i < cParts.Length) int.TryParse(cParts[i], out cNum);

                    if (lNum > cNum) return true;
                    if (lNum < cNum) return false;
                }
            }
            catch
            {
            }
            return false;
        }
    }
}
