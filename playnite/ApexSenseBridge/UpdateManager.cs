using Playnite.SDK;
using Playnite.SDK.Data;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace ApexSenseBridge
{
    public class GitHubReleaseInfo
    {
        [SerializationPropertyName("tag_name")]
        public string TagName { get; set; }

        [SerializationPropertyName("name")]
        public string Name { get; set; }

        [SerializationPropertyName("html_url")]
        public string HtmlUrl { get; set; }

        [SerializationPropertyName("body")]
        public string Body { get; set; }

        [SerializationPropertyName("prerelease")]
        public bool Prerelease { get; set; }

        [SerializationPropertyName("draft")]
        public bool Draft { get; set; }

        [SerializationPropertyName("assets")]
        public List<GitHubReleaseAsset> Assets { get; set; }
    }

    public class GitHubReleaseAsset
    {
        [SerializationPropertyName("name")]
        public string Name { get; set; }

        [SerializationPropertyName("browser_download_url")]
        public string BrowserDownloadUrl { get; set; }

        [SerializationPropertyName("size")]
        public long Size { get; set; }
    }

    public class UpdateCheckResult
    {
        public bool IsUpdateAvailable { get; set; }
        public Version CurrentVersion { get; set; }
        public Version RemoteVersion { get; set; }
        public string TagName { get; set; }
        public string ReleaseName { get; set; }
        public string ReleaseNotes { get; set; }
        public string ReleaseUrl { get; set; }
        public string SetupDownloadUrl { get; set; }
        public string PextDownloadUrl { get; set; }
        public string ErrorMessage { get; set; }
    }

    public static class UpdateManager
    {
        private static readonly ILogger logger = LogManager.GetLogger();
        private const string GitHubOwner = "ReynArts";
        private const string GitHubRepo = "ApexSenseBridge";
        private const string ReleasesLatestUrl = "https://api.github.com/repos/" + GitHubOwner + "/" + GitHubRepo + "/releases/latest";

        public static Version GetCurrentVersion()
        {
            try
            {
                var assembly = Assembly.GetExecutingAssembly();
                var version = assembly.GetName().Version;
                return new Version(version.Major, version.Minor, Math.Max(0, version.Build));
            }
            catch (Exception ex)
            {
                logger.Warn(ex, "Could not determine local version from assembly.");
                return new Version(0, 3, 0);
            }
        }

        public static Version ParseVersionFromTag(string tag)
        {
            if (string.IsNullOrWhiteSpace(tag)) return null;

            var clean = tag.Trim();
            if (clean.StartsWith("v", StringComparison.OrdinalIgnoreCase))
            {
                clean = clean.Substring(1).Trim();
            }

            // Extract numeric version parts like 0.3.0 or 0.3.0.1
            var match = Regex.Match(clean, @"^(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:\.(\d+))?");
            if (match.Success)
            {
                int major = int.Parse(match.Groups[1].Value);
                int minor = match.Groups[2].Success ? int.Parse(match.Groups[2].Value) : 0;
                int build = match.Groups[3].Success ? int.Parse(match.Groups[3].Value) : 0;
                int revision = match.Groups[4].Success ? int.Parse(match.Groups[4].Value) : -1;

                return revision >= 0 ? new Version(major, minor, build, revision) : new Version(major, minor, build);
            }

            return null;
        }

        public static async Task<UpdateCheckResult> CheckForUpdateAsync(Version currentVersion = null)
        {
            currentVersion = currentVersion ?? GetCurrentVersion();
            var result = new UpdateCheckResult
            {
                CurrentVersion = currentVersion
            };

            try
            {
                using (var client = new HttpClient())
                {
                    client.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("ApexSenseBridge-PlaynitePlugin", currentVersion.ToString()));
                    client.Timeout = TimeSpan.FromSeconds(15);

                    var response = await client.GetAsync(ReleasesLatestUrl).ConfigureAwait(false);
                    if (!response.IsSuccessStatusCode)
                    {
                        result.ErrorMessage = $"Erreur HTTP {(int)response.StatusCode} lors de la requête GitHub.";
                        logger.Warn($"GitHub release check returned status: {response.StatusCode}");
                        return result;
                    }

                    var json = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                    var release = Serialization.FromJson<GitHubReleaseInfo>(json);

                    if (release == null || string.IsNullOrWhiteSpace(release.TagName))
                    {
                        result.ErrorMessage = "Réponse de l'API GitHub vide ou invalide.";
                        return result;
                    }

                    var remoteVersion = ParseVersionFromTag(release.TagName);
                    if (remoteVersion == null)
                    {
                        result.ErrorMessage = $"Impossible d'analyser le tag de version : {release.TagName}";
                        return result;
                    }

                    result.RemoteVersion = remoteVersion;
                    result.TagName = release.TagName;
                    result.ReleaseName = string.IsNullOrWhiteSpace(release.Name) ? release.TagName : release.Name;
                    result.ReleaseNotes = release.Body;
                    result.ReleaseUrl = release.HtmlUrl;

                    if (release.Assets != null)
                    {
                        var setupAsset = release.Assets.FirstOrDefault(a =>
                            a.Name.EndsWith("-Setup.exe", StringComparison.OrdinalIgnoreCase) ||
                            a.Name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase));
                        if (setupAsset != null)
                        {
                            result.SetupDownloadUrl = setupAsset.BrowserDownloadUrl;
                        }

                        var pextAsset = release.Assets.FirstOrDefault(a =>
                            a.Name.EndsWith(".pext", StringComparison.OrdinalIgnoreCase));
                        if (pextAsset != null)
                        {
                            result.PextDownloadUrl = pextAsset.BrowserDownloadUrl;
                        }
                    }

                    result.IsUpdateAvailable = remoteVersion > currentVersion;
                    return result;
                }
            }
            catch (Exception ex)
            {
                logger.Error(ex, "Failed to check for ApexSenseBridge updates.");
                result.ErrorMessage = $"Erreur : {ex.Message}";
                return result;
            }
        }

        public static async Task<string> DownloadSetupAsync(string downloadUrl, string tagName, IProgress<double> progress = null)
        {
            if (string.IsNullOrWhiteSpace(downloadUrl))
            {
                throw new ArgumentException("Download URL cannot be empty.", nameof(downloadUrl));
            }

            var tempDir = Path.GetTempPath();
            var sanitizedTag = string.Join("_", (tagName ?? "latest").Split(Path.GetInvalidFileNameChars()));
            var targetFile = Path.Combine(tempDir, $"ApexSenseBridge-Setup-{sanitizedTag}.exe");

            using (var client = new HttpClient())
            {
                client.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("ApexSenseBridge-PlaynitePlugin", GetCurrentVersion().ToString()));
                client.Timeout = TimeSpan.FromMinutes(5);

                using (var response = await client.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead).ConfigureAwait(false))
                {
                    response.EnsureSuccessStatusCode();

                    var totalBytes = response.Content.Headers.ContentLength ?? -1L;
                    using (var contentStream = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                    using (var fileStream = new FileStream(targetFile, FileMode.Create, FileAccess.Write, FileShare.None, 8192, true))
                    {
                        var buffer = new byte[8192];
                        long totalRead = 0;
                        int bytesRead;

                        while ((bytesRead = await contentStream.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false)) > 0)
                        {
                            await fileStream.WriteAsync(buffer, 0, bytesRead).ConfigureAwait(false);
                            totalRead += bytesRead;

                            if (totalBytes > 0 && progress != null)
                            {
                                progress.Report((double)totalRead / totalBytes * 100.0);
                            }
                        }
                    }
                }
            }

            return targetFile;
        }

        public static bool ExecuteInstallerSilently(string setupFilePath, out string errorMessage)
        {
            errorMessage = null;

            if (!File.Exists(setupFilePath))
            {
                errorMessage = $"Le fichier d'installation est introuvable : {setupFilePath}";
                return false;
            }

            try
            {
                // Launch Inno Setup with silent arguments:
                // /SILENT : Displays only the progress dialog
                // /VERYSILENT : No wizard or progress window at all
                // /SUPPRESSMSGBOXES : Suppresses message boxes
                // /NORESTART : Prevents automatic reboot
                // /CLOSEAPPLICATIONS : Instructs setup to close applications using files
                var startInfo = new ProcessStartInfo
                {
                    FileName = setupFilePath,
                    Arguments = "/SILENT /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS",
                    UseShellExecute = true,
                    Verb = "runas" // Elevate as Administrator for Program Files and drivers
                };

                logger.Info($"Launching silent update installer: {setupFilePath}");
                using (var proc = Process.Start(startInfo))
                {
                    return true;
                }
            }
            catch (Win32Exception winEx) when (winEx.NativeErrorCode == 1223)
            {
                // User cancelled UAC prompt
                errorMessage = "Installation annulée (autorisation administrateur refusée).";
                logger.Warn("User declined UAC prompt for ApexSenseBridge update installer.");
                return false;
            }
            catch (Exception ex)
            {
                errorMessage = $"Erreur lors du lancement de l'installateur : {ex.Message}";
                logger.Error(ex, "Failed to launch update installer.");
                return false;
            }
        }
    }
}
