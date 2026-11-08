using System;
using System.Collections.Generic;
using System.Linq;
using ApexSenseBridgeTray.Models;

namespace ApexSenseBridgeTray.Services
{
    public sealed class ManualFixGuideEntry
    {
        public string[] FeatureKeys { get; set; }
        public string Note { get; set; }
        public ManualFixLink[] Links { get; set; }
        public string[] Tools { get; set; }
    }

    public static class ManualFixGuide
    {
        public const int MaxLinks = 6;

        public static IReadOnlyList<ManualFixGuideEntry> BuildEntries(SupportedGame game)
        {
            return BuildEntries(game, "en");
        }

        public static IReadOnlyList<ManualFixGuideEntry> BuildEntries(SupportedGame game, string language)
        {
            var entries = new List<ManualFixGuideEntry>();
            if (game == null) return entries;

            AddEntry(entries, "Loc_PillTriggers",
                game.AdaptiveTriggersManualFix ? game.AdaptiveTriggersFix : null, language);
            AddEntry(entries, "Loc_PillHaptics",
                game.HapticFeedbackManualFix ? game.HapticFeedbackFix : null, language);
            return entries;
        }

        private static string Localize(Dictionary<string, string> translations, string language, string source)
        {
            string translated;
            return translations != null && !string.IsNullOrWhiteSpace(language) &&
                   translations.TryGetValue(language, out translated) && !string.IsNullOrWhiteSpace(translated)
                ? translated.Trim()
                : source;
        }

        public static string PageUrl(SupportedGame game)
        {
            return game != null && IsOpenableUrl(game.ManualFixUrl) ? game.ManualFixUrl.Trim() : string.Empty;
        }

        public static bool IsOpenableUrl(string url)
        {
            Uri uri;
            return !string.IsNullOrWhiteSpace(url) &&
                   Uri.TryCreate(url.Trim(), UriKind.Absolute, out uri) &&
                   uri.Scheme == Uri.UriSchemeHttps &&
                   !string.IsNullOrEmpty(uri.Host) &&
                   string.IsNullOrEmpty(uri.UserInfo);
        }

        private static void AddEntry(
            List<ManualFixGuideEntry> entries, string featureKey, ManualFixDetail detail, string language)
        {
            if (detail == null) return;

            var note = Localize(detail.Translations, language, (detail.Note ?? string.Empty).Trim());
            var links = (detail.Links ?? new ManualFixLink[0])
                .Where(link => link != null && IsOpenableUrl(link.Url))
                .Take(MaxLinks)
                .Select(link => new ManualFixLink
                {
                    Label = Localize(link.Translations, language, link.Label),
                    Url = link.Url
                })
                .ToArray();
            if (note.Length == 0 && links.Length == 0) return;

            var existing = entries.FirstOrDefault(entry => HasSameContent(entry, note, links));
            if (existing != null)
            {
                existing.FeatureKeys = existing.FeatureKeys.Concat(new[] { featureKey }).ToArray();
                return;
            }

            entries.Add(new ManualFixGuideEntry
            {
                FeatureKeys = new[] { featureKey },
                Note = note,
                Links = links,
                Tools = (detail.Tools ?? new string[0]).Where(tool => !string.IsNullOrWhiteSpace(tool)).ToArray()
            });
        }

        private static bool HasSameContent(ManualFixGuideEntry entry, string note, ManualFixLink[] links)
        {
            return string.Equals(entry.Note, note, StringComparison.Ordinal) &&
                   entry.Links.Select(link => link.Url).SequenceEqual(links.Select(link => link.Url));
        }
    }
}
