using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows;

namespace ApexSenseBridgeTray.Common
{
    public static class LocalizationManager
    {
        public const string LangEnglish = "en";
        public const string LangFrench = "fr";
        public const string LangSpanish = "es";
        public const string LangChinese = "zh";

        public static readonly string[] SupportedLanguages = new[]
        {
            LangEnglish,
            LangFrench,
            LangSpanish,
            LangChinese
        };

        private static string currentLanguage = LangEnglish;
        private static readonly object syncLock = new object();
        private static readonly Dictionary<string, Dictionary<string, string>> loadedDictionaries =
            new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);

        public static string CurrentLanguage
        {
            get { return currentLanguage; }
        }

        public static event Action LanguageChanged;

        public static void Initialize(string preferredLanguage)
        {
            string lang = preferredLanguage;
            if (string.IsNullOrWhiteSpace(lang) || string.Equals(lang, "auto", StringComparison.OrdinalIgnoreCase))
            {
                lang = DetectSystemLanguage();
            }
            SetLanguage(lang);
        }

        public static string DetectSystemLanguage()
        {
            try
            {
                var culture = CultureInfo.CurrentUICulture;
                string iso = culture.TwoLetterISOLanguageName.ToLowerInvariant();
                if (iso == "fr") return LangFrench;
                if (iso == "es") return LangSpanish;
                if (iso == "zh") return LangChinese;
            }
            catch { }
            return LangEnglish;
        }

        public static void SetLanguage(string lang)
        {
            string normalized = NormalizeLanguageCode(lang);
            currentLanguage = normalized;

            // Ensure base English and requested language are loaded
            GetDictionary(LangEnglish);
            GetDictionary(currentLanguage);

            ApplyResources();

            var handler = LanguageChanged;
            if (handler != null)
            {
                handler();
            }
        }

        public static string Get(string key)
        {
            if (string.IsNullOrEmpty(key)) return string.Empty;

            var active = GetDictionary(currentLanguage);
            string val;
            if (active != null && active.TryGetValue(key, out val))
            {
                return val;
            }

            var fallback = GetDictionary(LangEnglish);
            if (fallback != null && fallback.TryGetValue(key, out val))
            {
                return val;
            }

            return key;
        }

        public static string Format(string key, params object[] args)
        {
            string template = Get(key);
            try
            {
                return string.Format(template, args);
            }
            catch
            {
                return template;
            }
        }

        private static string NormalizeLanguageCode(string lang)
        {
            if (string.IsNullOrWhiteSpace(lang)) return LangEnglish;
            string lower = lang.Trim().ToLowerInvariant();
            if (lower.StartsWith("fr")) return LangFrench;
            if (lower.StartsWith("es")) return LangSpanish;
            if (lower.StartsWith("zh")) return LangChinese;
            return LangEnglish;
        }

        private static Dictionary<string, string> GetDictionary(string lang)
        {
            lock (syncLock)
            {
                Dictionary<string, string> dict;
                if (loadedDictionaries.TryGetValue(lang, out dict))
                {
                    return dict;
                }

                dict = LoadLanguageDictionary(lang);
                if (dict == null)
                {
                    dict = new Dictionary<string, string>(StringComparer.Ordinal);
                }
                loadedDictionaries[lang] = dict;
                return dict;
            }
        }

        private static Dictionary<string, string> LoadLanguageDictionary(string lang)
        {
            try
            {
                // 1. Check for external file override: Languages/<lang>.json
                string externalPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Languages", lang + ".json");
                if (File.Exists(externalPath))
                {
                    string json = File.ReadAllText(externalPath, Encoding.UTF8);
                    var parsed = ParseDictionary(json);
                    if (parsed != null && parsed.Count > 0) return parsed;
                }

                // 2. Load embedded resource: ApexSenseBridgeTray.Resources.Languages.<lang>.json
                var assembly = Assembly.GetExecutingAssembly();
                string resourceName = string.Format("ApexSenseBridgeTray.Resources.Languages.{0}.json", lang);

                using (var stream = assembly.GetManifestResourceStream(resourceName))
                {
                    if (stream != null)
                    {
                        using (var reader = new StreamReader(stream, Encoding.UTF8))
                        {
                            string json = reader.ReadToEnd();
                            var parsed = ParseDictionary(json);
                            if (parsed != null && parsed.Count > 0) return parsed;
                        }
                    }
                }
            }
            catch
            {
            }

            return null;
        }

        private static Dictionary<string, string> ParseDictionary(string json)
        {
            if (string.IsNullOrWhiteSpace(json)) return null;
            try
            {
                var serializer = new JavaScriptSerializer();
                var rawDict = serializer.Deserialize<Dictionary<string, object>>(json);
                if (rawDict == null) return null;

                var result = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var kvp in rawDict)
                {
                    result[kvp.Key] = kvp.Value != null ? kvp.Value.ToString() : string.Empty;
                }
                return result;
            }
            catch
            {
                return null;
            }
        }

        private static void ApplyResources()
        {
            if (Application.Current == null) return;
            var res = Application.Current.Resources;
            if (res == null) return;

            // Apply English first so all keys are guaranteed to be populated
            var enDict = GetDictionary(LangEnglish);
            if (enDict != null)
            {
                foreach (var kvp in enDict)
                {
                    res[kvp.Key] = kvp.Value;
                }
            }

            if (!string.Equals(currentLanguage, LangEnglish, StringComparison.OrdinalIgnoreCase))
            {
                var activeDict = GetDictionary(currentLanguage);
                if (activeDict != null)
                {
                    foreach (var kvp in activeDict)
                    {
                        res[kvp.Key] = kvp.Value;
                    }
                }
            }
        }
    }
}
