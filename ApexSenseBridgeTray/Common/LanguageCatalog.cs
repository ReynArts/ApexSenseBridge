using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace ApexSenseBridgeTray.Common
{
    // Every embedded Resources/Languages/<code>.json is offered in the UI.
    internal static class LanguageCatalog
    {
        internal sealed class Entry
        {
            public string Code { get; set; }
            public string NativeName { get; set; }
            public string Badge { get; set; }
        }

        private static readonly Entry[] Known =
        {
            new Entry { Code = "en", NativeName = "English", Badge = "EN" },
            new Entry { Code = "fr", NativeName = "Français", Badge = "FR" },
            new Entry { Code = "es", NativeName = "Español", Badge = "ES" },
            new Entry { Code = "pt", NativeName = "Português", Badge = "PT" },
            new Entry { Code = "ru", NativeName = "Русский", Badge = "RU" },
            new Entry { Code = "vi", NativeName = "Tiếng Việt", Badge = "VI" },
            new Entry { Code = "ja", NativeName = "日本語", Badge = "日" },
            new Entry { Code = "ko", NativeName = "한국어", Badge = "한" },
            new Entry { Code = "zh", NativeName = "简体中文", Badge = "中" },
        };

        private const string ResourcePrefix = "ApexSenseBridgeTray.Resources.Languages.";
        private static readonly Lazy<Entry[]> available = new Lazy<Entry[]>(LoadAvailable);

        public static IReadOnlyList<Entry> Available => available.Value;

        public static string GetNativeName(string code)
        {
            var entry = Available.FirstOrDefault(e => string.Equals(e.Code, code, StringComparison.OrdinalIgnoreCase))
                ?? Known.FirstOrDefault(e => string.Equals(e.Code, code, StringComparison.OrdinalIgnoreCase));
            return entry != null ? entry.NativeName : (code ?? string.Empty).ToUpperInvariant();
        }

        private static Entry[] LoadAvailable()
        {
            var embedded = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                foreach (var name in Assembly.GetExecutingAssembly().GetManifestResourceNames())
                {
                    if (name.StartsWith(ResourcePrefix, StringComparison.Ordinal) &&
                        name.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
                    {
                        embedded.Add(name.Substring(ResourcePrefix.Length, name.Length - ResourcePrefix.Length - 5));
                    }
                }
            }
            catch
            {
            }
            embedded.Add("en");

            var result = Known.Where(e => embedded.Contains(e.Code)).ToList();
            foreach (var code in embedded.OrderBy(c => c, StringComparer.OrdinalIgnoreCase))
            {
                if (!result.Any(e => string.Equals(e.Code, code, StringComparison.OrdinalIgnoreCase)))
                    result.Add(new Entry { Code = code.ToLowerInvariant(), NativeName = code.ToUpperInvariant(), Badge = code.ToUpperInvariant() });
            }
            return result.ToArray();
        }
    }
}
