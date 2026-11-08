using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridge.Security;
using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Threading;
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
        public string UpdatePackageUrl { get; set; }
        public bool RestartRequested { get; set; }
    }

    public class UpdateCheckerService
    {
        private const string GitHubApiLatestRelease = "https://api.github.com/repos/ReynArts/ApexSenseBridge/releases/latest";
        private const string GitHubReleasesPage = "https://github.com/ReynArts/ApexSenseBridge/releases";
        private const string RelayLatestRelease = "https://asb-reports.reynarts.workers.dev/v1/latest";

        private readonly TraySettings settings;

        private const int UacCancelledErrorCode = 1223;

        private int updateBusy;

        public event Action<UpdateInfo> UpdateAvailable;
        public event Action<string> UpdateStatusChanged;
        public event Action RestartRequested;

        public UpdateCheckerService(TraySettings settings = null)
        {
            this.settings = settings;
        }

        private async Task<HttpResponseMessage> RequestLatestAsync(HttpClient client)
        {
            if (settings != null && settings.SendVersionStats)
            {
                try
                {
                    using (CancellationTokenSource timeout = new CancellationTokenSource(TimeSpan.FromSeconds(4)))
                    using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, RelayLatestRelease))
                    {
                        request.Headers.Add("X-ASB-Client", "tray/" + GetCurrentVersion());
                        request.Headers.Add("X-ASB-Install", settings.EnsureInstallId());
                        HttpResponseMessage relayed = await client.SendAsync(request, timeout.Token);
                        if (relayed.IsSuccessStatusCode)
                        {
                            return relayed;
                        }
                        relayed.Dispose();
                    }
                }
                catch
                {
                }
            }
            return await client.GetAsync(GitHubApiLatestRelease);
        }

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
            return "1.1.0";
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

                    HttpResponseMessage response = await RequestLatestAsync(client);
                    if (!response.IsSuccessStatusCode)
                    {
                        if (!silent)
                        {
                            MessageBox.Show(
                                LocalizationManager.Get("Loc_UpdateCheckUnavailable"),
                                LocalizationManager.Get("Loc_UpdateDialogTitle"),
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
                                        string downloadUrl = asset.ContainsKey("browser_download_url") &&
                                            asset["browser_download_url"] != null
                                            ? asset["browser_download_url"].ToString()
                                            : string.Empty;
                                        if (string.IsNullOrEmpty(info.InstallerUrl) &&
                                            ReleaseSecurity.IsExpectedSetupAsset(name, downloadUrl))
                                        {
                                            info.InstallerUrl = downloadUrl;
                                        }
                                        else if (string.IsNullOrEmpty(info.UpdatePackageUrl) &&
                                            UpdatePackage.IsExpectedUpdateAsset(name, downloadUrl))
                                        {
                                            info.UpdatePackageUrl = downloadUrl;
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
                        LocalizationManager.Get("Loc_UpdateError") + ex.Message,
                        LocalizationManager.Get("Loc_UpdateDialogTitle"),
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
                    string msg = LocalizationManager.Format("Loc_UpdatePrompt", info.CurrentVersion, info.LatestVersion);

                    MessageBoxResult result = MessageBox.Show(
                        msg,
                        LocalizationManager.Get("Loc_UpdateDialogAvailableTitle"),
                        MessageBoxButton.YesNo,
                        MessageBoxImage.Information);

                    if (result == MessageBoxResult.Yes)
                    {
                        await DownloadOrOpenReleaseAsync(info);
                    }
                }
            }
            else if (!silent)
            {
                MessageBox.Show(
                    LocalizationManager.Format("Loc_UpdateUpToDate", info.CurrentVersion),
                    LocalizationManager.Get("Loc_UpdateDialogTitle"),
                    MessageBoxButton.OK,
                    MessageBoxImage.Information);
            }

            return info;
        }

        public void DownloadOrOpenRelease(UpdateInfo info)
        {
            Task ignored = DownloadOrOpenReleaseAsync(info);
        }

        public async Task DownloadOrOpenReleaseAsync(UpdateInfo info)
        {
            if (Interlocked.CompareExchange(ref updateBusy, 1, 0) != 0)
            {
                return;
            }

            try
            {
                if (await TryApplyUpdatePackageAsync(info))
                {
                    return;
                }
                if (await TryRunSetupAsync(info))
                {
                    return;
                }
                OpenReleasePage(info);
            }
            finally
            {
                Interlocked.Exchange(ref updateBusy, 0);
            }
        }

        private void RaiseStatus(string text)
        {
            Action<string> handler = UpdateStatusChanged;
            if (handler != null)
            {
                handler(text);
            }
        }

        private async Task<bool> TryApplyUpdatePackageAsync(UpdateInfo info)
        {
            if (info == null)
            {
                return false;
            }

            string installDirectory = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            if (string.IsNullOrEmpty(installDirectory))
            {
                return false;
            }
            string updaterPath = Path.Combine(installDirectory, UpdateLaunchPolicy.UpdaterFileName);
            string enginePath = Path.Combine(installDirectory, "ApexSenseBridge.exe");
            string failedVersion = settings != null ? settings.UpdatePackageFailedVersion : null;
            if (!UpdateLaunchPolicy.ShouldTryPackage(
                info.UpdatePackageUrl, info.LatestVersion, failedVersion,
                File.Exists(updaterPath), File.Exists(enginePath)))
            {
                return false;
            }

            if (ControllerTestService.IsBridgeSessionActive())
            {
                MessageBox.Show(
                    LocalizationManager.Get("Loc_UpdateSessionActive"),
                    LocalizationManager.Get("Loc_UpdateDialogTitle"),
                    MessageBoxButton.OK,
                    MessageBoxImage.Information);
                return true;
            }

            DeleteStaleUpdateFiles();
            string packagePath = Path.Combine(
                Path.GetTempPath(),
                "ApexSenseBridge-Update-" + Guid.NewGuid().ToString("N") + ".zip");
            bool launched = false;
            try
            {
                RaiseStatus(LocalizationManager.Format("Loc_UpdateDownloadingPackage", info.LatestVersion));
                if (!await DownloadPackageAsync(info.UpdatePackageUrl, packagePath))
                {
                    return false;
                }

                UpdatePackageManifest manifest;
                string error;
                if (!UpdatePackage.ReadManifest(packagePath, out manifest, out error) || manifest == null)
                {
                    return false;
                }
                string manifestVersion = UpdateLaunchPolicy.NormalizeVersion(manifest.Version);
                if (manifestVersion == null ||
                    manifestVersion != UpdateLaunchPolicy.NormalizeVersion(info.LatestVersion) ||
                    !IsNewerVersion(manifestVersion, info.CurrentVersion))
                {
                    return false;
                }
                string driversReason;
                if (!UpdatePackage.DriversMatchInstalled(manifest, out driversReason))
                {
                    return false;
                }

                string arguments = UpdateLaunchPolicy.BuildUpdaterArguments(
                    packagePath, installDirectory, info.LatestVersion, Process.GetCurrentProcess().Id);
                if (arguments == null)
                {
                    return false;
                }

                ProcessStartInfo psi = new ProcessStartInfo(updaterPath, arguments);
                psi.WorkingDirectory = installDirectory;
                psi.UseShellExecute = true;
                psi.Verb = "runas";

                if (settings != null)
                {
                    settings.BeginUpdateAttempt(info.LatestVersion);
                    settings.Save();
                }
                try
                {
                    Process.Start(psi);
                    launched = true;
                }
                catch (Win32Exception ex)
                {
                    if (settings != null)
                    {
                        settings.AbandonUpdateAttempt();
                        settings.Save();
                    }
                    if (ex.NativeErrorCode == UacCancelledErrorCode)
                    {
                        MessageBox.Show(
                            LocalizationManager.Get("Loc_UpdateCancelled"),
                            LocalizationManager.Get("Loc_UpdateDialogTitle"),
                            MessageBoxButton.OK,
                            MessageBoxImage.Information);
                        return true;
                    }
                    return false;
                }
                catch (Exception)
                {
                    if (settings != null)
                    {
                        settings.AbandonUpdateAttempt();
                        settings.Save();
                    }
                    return false;
                }
            }
            catch (Exception)
            {
                return false;
            }
            finally
            {
                if (!launched)
                {
                    try { File.Delete(packagePath); } catch { }
                }
            }

            info.RestartRequested = true;
            RaiseStatus(LocalizationManager.Format("Loc_UpdateRestarting", info.LatestVersion));
            Action restart = RestartRequested;
            if (restart != null)
            {
                restart();
            }
            return true;
        }

        private static async Task<bool> DownloadPackageAsync(string url, string destination)
        {
            using (HttpClient client = new HttpClient())
            using (CancellationTokenSource timeout = new CancellationTokenSource(TimeSpan.FromMinutes(5)))
            {
                client.Timeout = Timeout.InfiniteTimeSpan;
                client.DefaultRequestHeaders.Add("User-Agent", "ApexSenseBridgeTray/1.0");
                using (HttpResponseMessage response = await client.GetAsync(
                    url, HttpCompletionOption.ResponseHeadersRead, timeout.Token))
                {
                    if (!response.IsSuccessStatusCode)
                    {
                        return false;
                    }
                    long? length = response.Content.Headers.ContentLength;
                    if (length.HasValue && length.Value > UpdateLaunchPolicy.MaxPackageBytes)
                    {
                        return false;
                    }

                    using (Stream source = await response.Content.ReadAsStreamAsync())
                    using (FileStream target = new FileStream(
                        destination, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                    {
                        byte[] buffer = new byte[81920];
                        long total = 0;
                        int read;
                        while ((read = await source.ReadAsync(buffer, 0, buffer.Length, timeout.Token)) > 0)
                        {
                            total += read;
                            if (total > UpdateLaunchPolicy.MaxPackageBytes)
                            {
                                return false;
                            }
                            await target.WriteAsync(buffer, 0, read, timeout.Token);
                        }
                        return total > 0;
                    }
                }
            }
        }

        private static void DeleteStaleUpdateFiles()
        {
            try
            {
                foreach (string file in Directory.GetFiles(Path.GetTempPath(), "ApexSenseBridge-Update-*.zip"))
                {
                    try
                    {
                        if (File.GetLastWriteTimeUtc(file) < DateTime.UtcNow.AddDays(-1))
                        {
                            File.Delete(file);
                        }
                    }
                    catch
                    {
                    }
                }
            }
            catch
            {
            }
        }

        private async Task<bool> TryRunSetupAsync(UpdateInfo info)
        {
            if (info != null && !string.IsNullOrEmpty(info.InstallerUrl))
            {
                try
                {
                    string tempPath = Path.Combine(
                        Path.GetTempPath(),
                        "ApexSenseBridge-Setup-" + Guid.NewGuid().ToString("N") + ".exe");
                    RaiseStatus(LocalizationManager.Format("Loc_UpdateDownloadingSetup", info.LatestVersion));
                    using (WebClient wc = new WebClient())
                    {
                        await wc.DownloadFileTaskAsync(info.InstallerUrl, tempPath);
                    }

                    string verificationError;
                    if (ReleaseSecurity.VerifyInstaller(
                        tempPath,
                        info.LatestVersion,
                        Assembly.GetExecutingAssembly().Location,
                        out verificationError))
                    {
                        ProcessStartInfo psi = new ProcessStartInfo(tempPath);
                        psi.UseShellExecute = true;
                        Process.Start(psi);
                        return true;
                    }
                    try { File.Delete(tempPath); } catch { }
                    MessageBox.Show(
                        "The downloaded installer was not launched because its identity could not be verified.\n\n" +
                        verificationError,
                        LocalizationManager.Get("Loc_UpdateDialogTitle"),
                        MessageBoxButton.OK,
                        MessageBoxImage.Error);
                }
                catch (Exception ex)
                {
                    MessageBox.Show(
                        LocalizationManager.Get("Loc_UpdateError") + ex.Message,
                        LocalizationManager.Get("Loc_UpdateDialogTitle"),
                        MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                }
            }
            return false;
        }

        private static void OpenReleasePage(UpdateInfo info)
        {
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
