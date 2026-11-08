using Playnite.SDK;
using Playnite.SDK.Data;
using ApexSenseBridge.Security;
using ApexSenseBridge.Common;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Threading;
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
        public string UpdatePackageUrl { get; set; }
        public string ErrorMessage { get; set; }
    }

    public enum UpdateInstallOutcome
    {
        Installed,
        InstalledRestartRequired,
        Cancelled,
        Blocked,
        Failed
    }

    public class UpdateInstallResult
    {
        public UpdateInstallOutcome Outcome { get; set; }
        public string Message { get; set; }
    }

    internal sealed class StagedFile
    {
        public string Target;
        public string Staged;
        public string Backup;
        public bool IsModule;
        public bool BackedUp;
        public bool Committed;
    }

    public static class UpdateManager
    {
        private static readonly ILogger logger = LogManager.GetLogger();
        private const string GitHubOwner = "ReynArts";
        private const string GitHubRepo = "ApexSenseBridge";
        private const string ReleasesLatestUrl = "https://api.github.com/repos/" + GitHubOwner + "/" + GitHubRepo + "/releases/latest";
        private const string RelayLatestUrl = "https://asb-reports.reynarts.workers.dev/v1/latest";

        private const long MaxPackageBytes = 200L * 1024 * 1024;
        private const string UpdaterFileName = "ApexSenseBridgeUpdater.exe";
        private const string ExtensionManifestFileName = "extension.yaml";
        private const string StagedSuffix = ".asbnew";
        private const string ReplacedSuffix = ".old";
        private const string PackageTempPrefix = "ApexSenseBridge-Update-";
        private const string PlayniteStagingPrefix = "ApexSenseBridge-PlayniteUpdate-";
        private const string SessionActiveMessage = "Une session ApexSenseBridge est active (un jeu est en cours). Fermez le jeu puis relancez la mise à jour.";
        private const int IoRetryCount = 6;
        private const int UpdaterWaitMilliseconds = 5 * 60 * 1000;
        private const int UpdaterExitPackageRejected = 3;
        private const int UpdaterExitDriversChanged = 4;
        private const int UpdaterExitRollbackFailed = 6;
        private const int UpdaterDeclined = -1001;
        private const int UpdaterNotStarted = -1002;
        private const int UpdaterTimedOut = -1003;
        private static bool restartPending;
        private static string packageFailedTag;

        private static async Task<HttpResponseMessage> RequestLatestAsync(HttpClient client, Version currentVersion, string installId)
        {
            if (!string.IsNullOrEmpty(installId))
            {
                try
                {
                    using (var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(4)))
                    using (var request = new HttpRequestMessage(HttpMethod.Get, RelayLatestUrl))
                    {
                        request.Headers.Add("X-ASB-Client", "playnite/" + currentVersion.ToString(3));
                        request.Headers.Add("X-ASB-Install", installId);
                        var relayed = await client.SendAsync(request, timeout.Token).ConfigureAwait(false);
                        if (relayed.IsSuccessStatusCode)
                        {
                            return relayed;
                        }
                        relayed.Dispose();
                    }
                }
                catch (Exception ex)
                {
                    logger.Debug(ex, "Update relay unavailable, falling back to GitHub.");
                }
            }
            return await client.GetAsync(ReleasesLatestUrl).ConfigureAwait(false);
        }

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

        public static async Task<UpdateCheckResult> CheckForUpdateAsync(Version currentVersion = null, string installId = null)
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

                    var response = await RequestLatestAsync(client, currentVersion, installId).ConfigureAwait(false);
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
                            ReleaseSecurity.IsExpectedSetupAsset(
                                a.Name, a.BrowserDownloadUrl));
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

                        var packageAsset = release.Assets.FirstOrDefault(a =>
                            UpdatePackage.IsExpectedUpdateAsset(
                                a.Name, a.BrowserDownloadUrl));
                        if (packageAsset != null)
                        {
                            result.UpdatePackageUrl = packageAsset.BrowserDownloadUrl;
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

            string verificationError;
            if (!ReleaseSecurity.VerifyInstaller(
                targetFile,
                tagName,
                Assembly.GetExecutingAssembly().Location,
                out verificationError))
            {
                try { File.Delete(targetFile); } catch { }
                throw new InvalidDataException(
                    "Le programme d'installation téléchargé n'a pas pu être vérifié : " +
                    verificationError);
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
                string verificationError;
                if (!ReleaseSecurity.VerifyInstaller(
                    setupFilePath,
                    null,
                    Assembly.GetExecutingAssembly().Location,
                    out verificationError))
                {
                    errorMessage = "L'identité du programme d'installation n'a pas pu être vérifiée : " +
                        verificationError;
                    return false;
                }

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

        public static bool IsRestartPending
        {
            get { return restartPending; }
        }

        public static async Task<UpdateInstallResult> InstallUpdateAsync(
            string tagName,
            string setupDownloadUrl,
            string packageDownloadUrl,
            string enginePath,
            Func<bool> isSessionActive,
            IProgress<double> progress,
            IProgress<string> status)
        {
            if (restartPending)
            {
                return CreateResult(UpdateInstallOutcome.InstalledRestartRequired,
                    "La mise à jour a déjà été appliquée.\n\nVeuillez redémarrer Playnite pour recharger la nouvelle version de l'extension.");
            }

            var packageAllowed = !string.IsNullOrWhiteSpace(packageDownloadUrl) &&
                !string.Equals(packageFailedTag, tagName, StringComparison.OrdinalIgnoreCase);
            if (packageAllowed)
            {
                UpdateInstallResult packaged = null;
                try
                {
                    packaged = await TryInstallPackageAsync(
                        tagName, packageDownloadUrl, enginePath, isSessionActive, progress, status).ConfigureAwait(false);
                }
                catch (Exception ex)
                {
                    logger.Warn(ex, "Update package path failed unexpectedly, falling back to the full setup.");
                }

                if (packaged != null)
                {
                    return packaged;
                }
            }

            return await InstallSetupAsync(tagName, setupDownloadUrl, progress, status).ConfigureAwait(false);
        }

        private static UpdateInstallResult CreateResult(UpdateInstallOutcome outcome, string message)
        {
            return new UpdateInstallResult { Outcome = outcome, Message = message };
        }

        private static async Task<UpdateInstallResult> InstallSetupAsync(
            string tagName, string setupDownloadUrl, IProgress<double> progress, IProgress<string> status)
        {
            try
            {
                if (status != null) status.Report("Téléchargement de l'installateur...");
                var installerPath = await DownloadSetupAsync(setupDownloadUrl, tagName, progress).ConfigureAwait(false);
                if (status != null) status.Report("Lancement de l'installation silencieuse...");

                string error;
                if (ExecuteInstallerSilently(installerPath, out error))
                {
                    return CreateResult(UpdateInstallOutcome.Installed, null);
                }

                return CreateResult(UpdateInstallOutcome.Failed, error ?? "Échec du lancement de l'installation.");
            }
            catch (Exception ex)
            {
                logger.Error(ex, "Failed to download and install update.");
                return CreateResult(UpdateInstallOutcome.Failed,
                    $"Erreur lors du téléchargement/installation : {ex.Message}");
            }
        }

        private static async Task<UpdateInstallResult> TryInstallPackageAsync(
            string tagName,
            string packageDownloadUrl,
            string enginePath,
            Func<bool> isSessionActive,
            IProgress<double> progress,
            IProgress<string> status)
        {
            string zipPath = null;
            string stagingRoot = null;
            var staged = new List<StagedFile>();
            var keepZip = false;
            var committed = false;

            try
            {
                if (!UpdatePackage.IsExpectedUpdateAsset(UpdatePackage.AssetName, packageDownloadUrl))
                {
                    return PackageUnavailable("the package URL is not an expected release asset");
                }

                var remoteVersion = ParseVersionFromTag(tagName);
                if (remoteVersion == null)
                {
                    return PackageUnavailable("the release tag cannot be parsed");
                }
                var expectedVersion = new Version(remoteVersion.Major, remoteVersion.Minor, Math.Max(0, remoteVersion.Build)).ToString(3);

                if (!InstallLocator.IsEngine(enginePath))
                {
                    return PackageUnavailable("ApexSenseBridge.exe was not found");
                }
                var installDir = Path.GetDirectoryName(Path.GetFullPath(enginePath));
                var updaterPath = Path.Combine(installDir, UpdaterFileName);
                if (!File.Exists(updaterPath))
                {
                    return PackageUnavailable("the installed updater is missing");
                }
                if (installDir.IndexOf('"') >= 0)
                {
                    return PackageUnavailable("the install directory contains a quote");
                }

                var extensionDir = GetExtensionDirectory();
                if (extensionDir == null)
                {
                    return PackageUnavailable("the extension directory cannot be resolved");
                }

                if (status != null) status.Report("Téléchargement du paquet de mise à jour...");
                var safeTag = string.Join("_", (tagName ?? "latest").Split(Path.GetInvalidFileNameChars()));
                zipPath = Path.Combine(Path.GetTempPath(),
                    PackageTempPrefix + safeTag + "-" + Guid.NewGuid().ToString("N").Substring(0, 8) + ".zip");
                await DownloadFileAsync(packageDownloadUrl, zipPath, MaxPackageBytes, progress).ConfigureAwait(false);

                if (status != null) status.Report("Vérification du paquet de mise à jour...");
                UpdatePackageManifest manifest;
                string error;
                if (!UpdatePackage.Verify(zipPath, expectedVersion, enginePath, out manifest, out error))
                {
                    return PackageUnavailable("verification failed: " + error);
                }
                if (!UpdatePackage.DriversMatchInstalled(manifest, out error))
                {
                    return PackageUnavailable("drivers differ: " + error);
                }

                if (IsSessionActive(isSessionActive))
                {
                    return CreateResult(UpdateInstallOutcome.Blocked, SessionActiveMessage);
                }

                if (status != null) status.Report("Préparation de la mise à jour de l'extension...");
                stagingRoot = Path.Combine(Path.GetTempPath(), PlayniteStagingPrefix + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(stagingRoot);
                List<string> written;
                if (!UpdatePackage.Extract(zipPath, manifest, UpdatePackage.SectionPlaynite, stagingRoot, out written, out error))
                {
                    return PackageUnavailable("extraction failed: " + error);
                }
                if (!PrepareStagedFiles(stagingRoot, extensionDir, manifest, written, staged, out error))
                {
                    return PackageUnavailable("staging failed: " + error);
                }

                if (IsSessionActive(isSessionActive))
                {
                    return CreateResult(UpdateInstallOutcome.Blocked, SessionActiveMessage);
                }

                if (status != null) status.Report("Mise à jour du moteur (autorisation administrateur requise)...");
                var exitCode = await RunUpdaterAsync(updaterPath, zipPath, installDir, manifest.Version).ConfigureAwait(false);
                if (exitCode == UpdaterTimedOut)
                {
                    keepZip = true;
                }

                if (exitCode == UpdaterDeclined)
                {
                    return CreateResult(UpdateInstallOutcome.Cancelled,
                        "Mise à jour annulée (autorisation administrateur refusée). La version actuelle est conservée.");
                }
                if (exitCode == UpdaterExitPackageRejected || exitCode == UpdaterExitDriversChanged)
                {
                    return PackageUnavailable("the updater rejected the package (exit code " + exitCode + ")");
                }
                if (exitCode != 0)
                {
                    packageFailedTag = tagName;
                    logger.Warn($"ApexSenseBridgeUpdater failed with code {exitCode}.");
                    var detail = exitCode == UpdaterExitRollbackFailed
                        ? "La restauration automatique a échoué : réinstallez ApexSenseBridge avec le programme d'installation complet."
                        : "La version actuelle est conservée. Cliquez de nouveau sur « Mettre à jour » pour utiliser le programme d'installation complet.";
                    return CreateResult(UpdateInstallOutcome.Failed,
                        $"La mise à jour du moteur a échoué (code {exitCode}). {detail}");
                }

                if (status != null) status.Report("Application de la mise à jour de l'extension...");
                string commitError;
                if (!CommitStagedFiles(staged, out commitError))
                {
                    packageFailedTag = tagName;
                    logger.Error("Playnite extension update could not be applied: " + commitError);
                    return CreateResult(UpdateInstallOutcome.Failed,
                        "Le moteur a été mis à jour mais l'extension n'a pas pu l'être : " + commitError +
                        "\n\nL'ancienne extension reste en place. Cliquez de nouveau sur « Mettre à jour » pour utiliser le programme d'installation complet.");
                }

                committed = true;
                restartPending = true;
                logger.Info($"ApexSenseBridge update {tagName} applied from the update package; restart required.");
                return CreateResult(UpdateInstallOutcome.InstalledRestartRequired,
                    $"La mise à jour {tagName} a été installée avec succès.\n\nVeuillez redémarrer Playnite pour recharger la nouvelle version de l'extension.");
            }
            finally
            {
                if (!committed)
                {
                    foreach (var file in staged)
                    {
                        TryDelete(file.Staged);
                    }
                }
                if (stagingRoot != null)
                {
                    TryDeleteDirectory(stagingRoot);
                }
                if (zipPath != null && !keepZip)
                {
                    TryDelete(zipPath);
                }
            }
        }

        private static UpdateInstallResult PackageUnavailable(string reason)
        {
            logger.Info("Update package not used (" + reason + "); using the full setup.");
            return null;
        }

        private static bool IsSessionActive(Func<bool> isSessionActive)
        {
            try
            {
                return isSessionActive != null && isSessionActive();
            }
            catch (Exception ex)
            {
                logger.Warn(ex, "Could not determine whether a bridge session is active; assuming it is.");
                return true;
            }
        }

        private static string GetExtensionDirectory()
        {
            try
            {
                var location = Assembly.GetExecutingAssembly().Location;
                if (string.IsNullOrWhiteSpace(location))
                {
                    return null;
                }
                var directory = Path.GetDirectoryName(location);
                if (string.IsNullOrWhiteSpace(directory) ||
                    !File.Exists(Path.Combine(directory, ExtensionManifestFileName)))
                {
                    return null;
                }
                return directory;
            }
            catch
            {
                return null;
            }
        }

        private static string GetModuleFileName()
        {
            return Path.GetFileName(Assembly.GetExecutingAssembly().Location);
        }

        private static async Task DownloadFileAsync(string url, string targetFile, long maxBytes, IProgress<double> progress)
        {
            using (var client = new HttpClient())
            {
                client.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("ApexSenseBridge-PlaynitePlugin", GetCurrentVersion().ToString()));
                client.Timeout = TimeSpan.FromMinutes(5);

                using (var response = await client.GetAsync(url, HttpCompletionOption.ResponseHeadersRead).ConfigureAwait(false))
                {
                    response.EnsureSuccessStatusCode();

                    var totalBytes = response.Content.Headers.ContentLength ?? -1L;
                    if (totalBytes > maxBytes)
                    {
                        throw new InvalidDataException("Le paquet de mise à jour est trop volumineux.");
                    }

                    using (var contentStream = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                    using (var fileStream = new FileStream(targetFile, FileMode.CreateNew, FileAccess.Write, FileShare.None, 8192, true))
                    {
                        var buffer = new byte[8192];
                        long totalRead = 0;
                        int bytesRead;

                        while ((bytesRead = await contentStream.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false)) > 0)
                        {
                            totalRead += bytesRead;
                            if (totalRead > maxBytes)
                            {
                                throw new InvalidDataException("Le paquet de mise à jour est trop volumineux.");
                            }

                            await fileStream.WriteAsync(buffer, 0, bytesRead).ConfigureAwait(false);
                            if (totalBytes > 0 && progress != null)
                            {
                                progress.Report((double)totalRead / totalBytes * 100.0);
                            }
                        }
                        await fileStream.FlushAsync().ConfigureAwait(false);
                    }
                }
            }
        }

        private static async Task<int> RunUpdaterAsync(string updaterPath, string zipPath, string installDir, string version)
        {
            return await Task.Run(() =>
            {
                try
                {
                    var startInfo = new ProcessStartInfo
                    {
                        FileName = updaterPath,
                        Arguments = "--package \"" + zipPath + "\" --install-dir \"" + installDir +
                                    "\" --expected-version " + version + " --relaunch-tray",
                        WorkingDirectory = installDir,
                        UseShellExecute = true,
                        Verb = "runas"
                    };

                    logger.Info("Launching ApexSenseBridgeUpdater for the engine update.");
                    using (var process = Process.Start(startInfo))
                    {
                        if (process == null)
                        {
                            return UpdaterNotStarted;
                        }
                        if (!process.WaitForExit(UpdaterWaitMilliseconds))
                        {
                            return UpdaterTimedOut;
                        }
                        return process.ExitCode;
                    }
                }
                catch (Win32Exception winEx) when (winEx.NativeErrorCode == 1223)
                {
                    logger.Warn("User declined UAC prompt for ApexSenseBridgeUpdater.");
                    return UpdaterDeclined;
                }
                catch (Exception ex)
                {
                    logger.Error(ex, "Failed to run ApexSenseBridgeUpdater.");
                    return UpdaterNotStarted;
                }
            }).ConfigureAwait(false);
        }

        internal static bool PrepareStagedFiles(
            string stagingRoot,
            string extensionDir,
            UpdatePackageManifest manifest,
            List<string> written,
            List<StagedFile> staged,
            out string error)
        {
            error = null;
            var expected = (manifest.Files ?? new List<UpdatePackageFile>())
                .Where(f => string.Equals(f.Section, UpdatePackage.SectionPlaynite, StringComparison.OrdinalIgnoreCase))
                .ToList();
            var moduleName = GetModuleFileName();
            if (expected.Count == 0 ||
                !expected.Any(f => string.Equals(f.Path, moduleName, StringComparison.OrdinalIgnoreCase)) ||
                !expected.Any(f => string.Equals(f.Path, ExtensionManifestFileName, StringComparison.OrdinalIgnoreCase)))
            {
                error = "the package has no extension files";
                return false;
            }
            if (written == null || written.Count != expected.Count)
            {
                error = "the extracted file list does not match the manifest";
                return false;
            }

            var stagedId = ReadExtensionId(Path.Combine(stagingRoot, ExtensionManifestFileName));
            var installedId = ReadExtensionId(Path.Combine(extensionDir, ExtensionManifestFileName));
            if (string.IsNullOrEmpty(stagedId) || !string.Equals(stagedId, installedId, StringComparison.Ordinal))
            {
                error = "the package targets a different extension";
                return false;
            }

            var root = Path.GetFullPath(extensionDir).TrimEnd('\\') + "\\";
            foreach (var file in expected)
            {
                var relative = (file.Path ?? string.Empty).Replace('/', '\\');
                if (relative.Length == 0 || Path.IsPathRooted(relative) ||
                    relative.Split('\\').Any(part => part.Length == 0 || part == "." || part == ".." || part.IndexOf(':') >= 0))
                {
                    error = "unsafe path in the package";
                    return false;
                }

                var source = Path.Combine(stagingRoot, relative);
                var target = Path.GetFullPath(Path.Combine(extensionDir, relative));
                if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                {
                    error = "path outside the extension directory";
                    return false;
                }
                if (!FileMatches(source, file.Size, file.Sha256))
                {
                    error = "extracted file does not match the manifest: " + relative;
                    return false;
                }

                Directory.CreateDirectory(Path.GetDirectoryName(target));
                var entry = new StagedFile
                {
                    Target = target,
                    Staged = target + StagedSuffix,
                    IsModule = string.Equals(relative, moduleName, StringComparison.OrdinalIgnoreCase)
                };
                staged.Add(entry);
                File.Copy(source, entry.Staged, true);
                if (!FileMatches(entry.Staged, file.Size, file.Sha256))
                {
                    error = "staged copy does not match the manifest: " + relative;
                    return false;
                }
            }

            return true;
        }

        internal static bool CommitStagedFiles(List<StagedFile> staged, out string error)
        {
            error = null;
            var ordered = staged.Where(f => !f.IsModule).Concat(staged.Where(f => f.IsModule)).ToList();
            var touched = new List<StagedFile>();

            foreach (var file in ordered)
            {
                try
                {
                    touched.Add(file);
                    if (File.Exists(file.Target))
                    {
                        file.Backup = ChooseBackupPath(file.Target);
                        var backup = file.Backup;
                        var target = file.Target;
                        RetryIo(() => File.Move(target, backup));
                        file.BackedUp = true;
                    }

                    var source = file.Staged;
                    var destination = file.Target;
                    RetryIo(() => File.Move(source, destination));
                    file.Committed = true;
                }
                catch (Exception ex)
                {
                    logger.Error(ex, "Failed to replace " + file.Target);
                    error = "impossible de remplacer " + Path.GetFileName(file.Target) + " (" + ex.Message + ")";
                    var restoreError = RollbackCommit(touched);
                    if (restoreError != null)
                    {
                        error += ". La restauration est incomplète : " + restoreError;
                    }
                    return false;
                }
            }

            return true;
        }

        private static string RollbackCommit(List<StagedFile> touched)
        {
            string failure = null;
            for (var i = touched.Count - 1; i >= 0; i--)
            {
                var file = touched[i];
                try
                {
                    if (file.Committed)
                    {
                        var target = file.Target;
                        var aside = target + "." + Guid.NewGuid().ToString("N").Substring(0, 8) + ReplacedSuffix;
                        RetryIo(() => File.Move(target, aside));
                        file.Committed = false;
                    }
                    if (file.BackedUp)
                    {
                        var backup = file.Backup;
                        var restored = file.Target;
                        RetryIo(() => File.Move(backup, restored));
                        file.BackedUp = false;
                    }
                }
                catch (Exception ex)
                {
                    logger.Error(ex, "Failed to restore " + file.Target);
                    failure = failure ?? ("restauration impossible pour " + Path.GetFileName(file.Target) +
                        " (l'ancienne version est dans " + (file.Backup ?? file.Target) + ")");
                }
            }
            return failure;
        }

        private static string ChooseBackupPath(string target)
        {
            var backup = target + ReplacedSuffix;
            if (File.Exists(backup))
            {
                TryDelete(backup);
            }
            if (File.Exists(backup))
            {
                backup = target + "." + Guid.NewGuid().ToString("N").Substring(0, 8) + ReplacedSuffix;
            }
            return backup;
        }

        private static void RetryIo(Action action)
        {
            for (var attempt = 1; ; attempt++)
            {
                try
                {
                    action();
                    return;
                }
                catch (Exception ex) when (attempt < IoRetryCount && (ex is IOException || ex is UnauthorizedAccessException))
                {
                    Thread.Sleep(150 * attempt);
                }
            }
        }

        private static bool FileMatches(string path, long size, string sha256)
        {
            try
            {
                var info = new FileInfo(path);
                if (!info.Exists || info.Length != size)
                {
                    return false;
                }
                using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
                using (var sha = SHA256.Create())
                {
                    var hash = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", string.Empty);
                    return string.Equals(hash, sha256, StringComparison.OrdinalIgnoreCase);
                }
            }
            catch
            {
                return false;
            }
        }

        private static string ReadExtensionId(string path)
        {
            try
            {
                foreach (var line in File.ReadLines(path))
                {
                    if (line.StartsWith("Id:", StringComparison.Ordinal))
                    {
                        return line.Substring(3).Trim();
                    }
                }
            }
            catch
            {
            }
            return null;
        }

        private static void TryDelete(string path)
        {
            try
            {
                if (File.Exists(path))
                {
                    File.Delete(path);
                }
            }
            catch
            {
            }
        }

        private static void TryDeleteDirectory(string path)
        {
            try
            {
                if (Directory.Exists(path))
                {
                    Directory.Delete(path, true);
                }
            }
            catch
            {
            }
        }

        public static void CleanupUpdateLeftovers()
        {
            try
            {
                var extensionDir = GetExtensionDirectory();
                if (extensionDir != null)
                {
                    foreach (var file in Directory.EnumerateFiles(extensionDir, "*", SearchOption.AllDirectories))
                    {
                        if (file.EndsWith(ReplacedSuffix, StringComparison.OrdinalIgnoreCase) ||
                            file.EndsWith(StagedSuffix, StringComparison.OrdinalIgnoreCase))
                        {
                            TryDelete(file);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                logger.Debug(ex, "Could not clean update leftovers in the extension directory.");
            }

            try
            {
                var threshold = DateTime.UtcNow.AddDays(-1);
                var temp = Path.GetTempPath();
                foreach (var file in Directory.EnumerateFiles(temp, PackageTempPrefix + "*.zip"))
                {
                    if (File.GetLastWriteTimeUtc(file) < threshold)
                    {
                        TryDelete(file);
                    }
                }
                foreach (var directory in Directory.EnumerateDirectories(temp, PlayniteStagingPrefix + "*"))
                {
                    if (Directory.GetLastWriteTimeUtc(directory) < threshold)
                    {
                        TryDeleteDirectory(directory);
                    }
                }
            }
            catch (Exception ex)
            {
                logger.Debug(ex, "Could not clean temporary update files.");
            }
        }
    }
}
