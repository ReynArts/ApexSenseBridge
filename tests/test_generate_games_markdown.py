import importlib.util
import pathlib
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "generate_games_markdown.py"
SPEC = importlib.util.spec_from_file_location("generate_games_markdown", SCRIPT)
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class GamesMarkdownTests(unittest.TestCase):
    def test_support_is_reported_per_feature_and_legacy_manual_flags_are_conservative(self):
        mixed = {"adaptiveTriggers": True, "adaptiveTriggersManualFix": True, "hapticFeedback": True,
                 "requiresManualFix": True}
        self.assertEqual("🛠️ Manual fix", GENERATOR.feature_status(mixed, "adaptiveTriggers"))
        self.assertEqual("✅ Native", GENERATOR.feature_status(mixed, "hapticFeedback"))
        self.assertEqual("—", GENERATOR.feature_status({}, "hapticFeedback"))
        legacy = {"adaptiveTriggers": True, "requiresManualFix": True}
        self.assertEqual("🛠️ Manual fix", GENERATOR.feature_status(legacy, "adaptiveTriggers"))

    def test_titles_and_external_links_cannot_break_tables_or_inject_html(self):
        game = {"title": 'A | [Game] <script>\nNext', "adaptiveTriggers": True,
                "requiresManualFix": True, "iconUrl": 'https://example.com/cover.jpg?x="bad"',
                "addedAt": "2026-11-04", "manualFixUrl": "javascript:alert(1)",
                "manualFixDetails": {"adaptiveTriggers": {"links": [
                    {"label": "unsafe", "url": "javascript:alert(1)"},
                    {"label": "Mod | guide", "url": "https://example.com/mod_(1)"}]}}}
        rendered = GENERATOR.render_catalogue({"games": [game]})
        self.assertNotIn("<script>", rendered)
        self.assertNotIn("javascript:", rendered)
        self.assertIn("&#124;", rendered)
        self.assertIn("mod_%281%29", rendered)
        self.assertIn("&lt;script&gt;", rendered)

    def test_counts_sorting_recent_dates_and_verified_store_links_come_from_source_data(self):
        games = [
            {"title": "Zulu", "hapticFeedback": True, "addedAt": "2026-11-04", "steamAppId": 123},
            {"title": "Alpha", "adaptiveTriggers": True, "steamAppId": 456, "steamAppIdVerified": True,
             "addedAt": "2026-11-03"},
            {"title": "Éclair", "adaptiveTriggers": True, "adaptiveTriggersManualFix": True,
             "addedAt": "not-a-date"},
        ]
        rendered = GENERATOR.render_catalogue({"games": games, "totalGames": 999,
                                                "updatedAt": "2026-11-04T14:00:00+02:00"})
        self.assertIn("| **3** | **2** | **1** | **1** |", rendered)
        self.assertIn("2026-11-04 12:00 UTC", rendered)
        self.assertIn("### E", rendered)
        self.assertNotIn("/app/123/", rendered)
        self.assertIn("/app/456/", rendered)
        recent, catalogue = rendered.split("## 📚 Full catalogue")
        self.assertLess(recent.index("<strong>Zulu"), recent.index("<strong>Alpha"))
        self.assertLess(catalogue.index("| [Alpha]"), catalogue.index("| [Zulu]"))
        self.assertEqual(rendered, GENERATOR.render_catalogue({"games": list(reversed(games)),
                                                              "updatedAt": "2026-11-04T14:00:00+02:00"}))

    def test_check_detects_a_missing_or_stale_page_and_generation_repairs_it(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "games.json"
            output = pathlib.Path(directory) / "GAMES.md"
            source.write_text('{"games": []}', encoding="utf-8")
            args = ["--input", str(source), "--output", str(output)]
            self.assertEqual(1, GENERATOR.main(args + ["--check"]))
            self.assertEqual(0, GENERATOR.main(args))
            self.assertEqual(0, GENERATOR.main(args + ["--check"]))
            output.write_text("stale", encoding="utf-8")
            self.assertEqual(1, GENERATOR.main(args + ["--check"]))


if __name__ == "__main__":
    unittest.main()
