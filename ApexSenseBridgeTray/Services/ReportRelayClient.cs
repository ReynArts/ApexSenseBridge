using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Reflection;
using System.Text;
using System.Web.Script.Serialization;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class ReportRelayResult
    {
        public bool Success;
        public string Url;
        public string Error;
        public bool SkippedAttachments;
    }

    internal static class ReportRelayClient
    {
        public const string DefaultEndpoint = "https://asb-reports.reynarts.workers.dev/v1/report";
        public const long MaximumLogsBytes = 10L * 1024L * 1024L;
        public const long MaximumImageBytes = 8L * 1024L * 1024L;

        public static ReportRelayResult Send(
            string endpoint,
            string category,
            string title,
            string description,
            string gameInfo,
            string diagnostics,
            string reportId,
            string logsZipPath,
            IEnumerable<string> screenshotPaths)
        {
            var result = new ReportRelayResult();
            try
            {
                using (var client = new HttpClient())
                using (var form = new MultipartFormDataContent())
                {
                    client.Timeout = TimeSpan.FromSeconds(90);
                    client.DefaultRequestHeaders.Add("User-Agent", "ApexSenseBridgeTray/1.0");
                    string clientVersion = "tray/" + Version();
                    client.DefaultRequestHeaders.Add("X-ASB-Client", clientVersion);

                    AddField(form, "category", category);
                    AddField(form, "title", title);
                    AddField(form, "description", description);
                    AddField(form, "gameInfo", gameInfo);
                    AddField(form, "diagnostics", diagnostics);
                    AddField(form, "reportId", reportId);
                    AddField(form, "clientVersion", clientVersion);

                    if (!string.IsNullOrEmpty(logsZipPath))
                    {
                        if (!AddFile(form, "logs", logsZipPath, "application/zip", MaximumLogsBytes))
                            result.SkippedAttachments = true;
                    }

                    foreach (string path in screenshotPaths ?? new string[0])
                    {
                        if (!AddFile(form, "screenshots", path, "application/octet-stream", MaximumImageBytes))
                            result.SkippedAttachments = true;
                    }

                    using (HttpResponseMessage response = client.PostAsync(endpoint, form).GetAwaiter().GetResult())
                    {
                        string body = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                        var data = Parse(body);
                        if (response.IsSuccessStatusCode && data != null && Convert.ToBoolean(Get(data, "ok") ?? false))
                        {
                            result.Url = Convert.ToString(Get(data, "url"));
                            result.Success = !string.IsNullOrWhiteSpace(result.Url);
                            if (!result.Success) result.Error = "invalid_response";
                        }
                        else
                        {
                            result.Error = Convert.ToString(Get(data, "error")) ?? ("http_" + (int)response.StatusCode);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                var inner = ex is AggregateException && ex.InnerException != null ? ex.InnerException : ex;
                result.Error = inner is System.Threading.Tasks.TaskCanceledException ? "timeout" : inner.GetType().Name;
            }
            return result;
        }

        private static string Version()
        {
            try
            {
                Version version = Assembly.GetExecutingAssembly().GetName().Version;
                if (version != null) return version.Major + "." + version.Minor + "." + version.Build;
            }
            catch
            {
            }
            return "0.0.0";
        }

        private static void AddField(MultipartFormDataContent form, string name, string value)
        {
            if (string.IsNullOrEmpty(value)) return;
            form.Add(new StringContent(value, Encoding.UTF8), name);
        }

        private static bool AddFile(MultipartFormDataContent form, string name, string path, string contentType, long maximumBytes)
        {
            try
            {
                var info = new FileInfo(path);
                if (!info.Exists || info.Length > maximumBytes) return false;
                var content = new ByteArrayContent(File.ReadAllBytes(path));
                content.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue(contentType);
                form.Add(content, name, Path.GetFileName(path));
                return true;
            }
            catch
            {
                return false;
            }
        }

        private static Dictionary<string, object> Parse(string body)
        {
            try
            {
                return new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(body);
            }
            catch
            {
                return null;
            }
        }

        private static object Get(Dictionary<string, object> data, string key)
        {
            object value;
            return data != null && data.TryGetValue(key, out value) ? value : null;
        }
    }
}
