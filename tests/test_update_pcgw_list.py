import importlib.util
import json
import pathlib
import unittest
from unittest import mock


SCRIPT = pathlib.Path(__file__).parents[1] / "scripts" / "update_pcgw_list.py"
SPEC = importlib.util.spec_from_file_location("update_pcgw_list", SCRIPT)
UPDATER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UPDATER)


class DiscordExecutableTests(unittest.TestCase):
    def test_death_stranding_2_profile_survives_catalogue_regeneration(self):
        for title in ("Death Stranding 2", "Death Stranding 2: On the Beach"):
            self.assertEqual(
                "death-stranding-2",
                UPDATER.get_special_profile(UPDATER.normalize_title(title)),
            )
        for title in ("Death Stranding", "Death Stranding Director's Cut"):
            self.assertEqual(
                "standard", UPDATER.get_special_profile(UPDATER.normalize_title(title))
            )

    def test_pcgw_support_parser_preserves_manual_fix_state_and_page_url(self):
        markup = """
        <table><tbody>
          <tr>
            <td><a href="/wiki/Native_Game" title="Native Game">Native Game</a></td>
            <td><div title="Native support" class="svg-icon tickcross-true"></div></td>
          </tr>
          <tr>
            <td><a href="/wiki/Dying_Light" title="Dying Light">Dying Light</a></td>
            <td><div title="Hackable" class="svg-icon svg-25 tickcross-hackable"></div></td>
          </tr>
        </tbody></table>
        """

        games = UPDATER.parse_pcgw_support_rows(markup)

        self.assertFalse(games["Native Game"]["requiresManualFix"])
        self.assertTrue(games["Dying Light"]["requiresManualFix"])
        self.assertEqual(
            "https://www.pcgamingwiki.com/wiki/Dying_Light",
            games["Dying Light"]["pcgwUrl"],
        )

    def test_extracts_only_windows_non_launcher_executables_by_exact_steam_id(self):
        applications = [
            {
                "executables": [
                    {"name": "Bin\\Game.exe", "os": "win32", "is_launcher": False},
                    {"name": "Launcher.exe", "os": "win32", "is_launcher": True},
                    {"name": "Tools\\GameEditor.exe", "os": "win32", "is_launcher": False},
                    {"name": "CrashReportClient.exe", "os": "win32", "is_launcher": False},
                    {"name": "Game", "os": "win32", "is_launcher": False},
                    {"name": "Game.app", "os": "darwin", "is_launcher": False},
                ],
                "third_party_skus": [
                    {"distributor": "steam", "id": "123456"},
                    {"distributor": "xbox", "id": "ignored"},
                ],
            }
        ]

        self.assertEqual({123456: ["Game.exe"]}, UPDATER.extract_discord_executables(applications))

    def test_enrichment_replaces_matched_cache_and_preserves_unmatched_cache(self):
        games = [
            {
                "title": "Alpha",
                "normalized": "alpha",
                "steamAppId": 10,
                "steamAppIdVerified": True,
                "executables": ["OldAlpha.exe"],
            },
            {
                "title": "Beta",
                "normalized": "beta",
                "steamAppId": 20,
                "steamAppIdVerified": True,
                "executables": ["CachedBeta.exe"],
            },
        ]

        stats = UPDATER.enrich_with_discord_executables(games, {10: ["Alpha.exe"]})

        self.assertEqual(["Alpha.exe"], games[0]["executables"])
        self.assertEqual(["CachedBeta.exe"], games[1]["executables"])
        self.assertEqual(1, stats["matchedGames"])

    def test_ambiguous_executable_names_are_removed_from_every_game(self):
        games = [
            {"title": "Alpha", "normalized": "alpha", "steamAppId": 10, "steamAppIdVerified": True},
            {"title": "Beta", "normalized": "beta", "steamAppId": 20, "steamAppIdVerified": True},
        ]

        stats = UPDATER.enrich_with_discord_executables(
            games,
            {10: ["Shared.exe", "Alpha.exe"], 20: ["shared.EXE", "Beta.exe"]},
        )

        self.assertEqual(["Alpha.exe"], games[0]["executables"])
        self.assertEqual(["Beta.exe"], games[1]["executables"])
        self.assertEqual(1, stats["ambiguousNames"])

    def test_failed_download_keeps_cached_names_and_still_removes_collisions(self):
        games = [
            {"title": "Alpha", "normalized": "alpha", "steamAppIdVerified": True, "executables": ["Same.exe"]},
            {"title": "Beta", "normalized": "beta", "steamAppIdVerified": True, "executables": ["same.EXE"]},
        ]

        stats = UPDATER.enrich_with_discord_executables(games, None)

        self.assertNotIn("executables", games[0])
        self.assertNotIn("executables", games[1])
        self.assertEqual(1, stats["ambiguousNames"])

    def test_unverified_steam_id_cannot_import_or_retain_discord_executables(self):
        games = [
            {
                "title": "Wrong Identity",
                "normalized": "wrongidentity",
                "steamAppId": 10,
                "steamAppIdVerified": False,
                "executables": ["PreviouslyCached.exe"],
            }
        ]

        stats = UPDATER.enrich_with_discord_executables(games, {10: ["Wrong.exe"]})

        self.assertNotIn("executables", games[0])
        self.assertEqual(0, stats["matchedGames"])

    def test_pinned_call_of_duty_hub_executable_survives_discord_enrichment(self):
        games = [
            {
                "title": "Call of Duty",
                "normalized": "callofduty",
                "steamAppId": 1938090,
                "steamAppIdVerified": True,
                "executables": ["cod.exe"],
            },
            {
                "title": "Call of Duty: Black Ops 7",
                "normalized": "callofdutyblackops7",
                "steamAppId": 3606480,
                "steamAppIdVerified": True,
            },
        ]

        UPDATER.enrich_with_discord_executables(
            games, {3606480: ["cod.exe", "cod25-cod.exe"]}
        )

        self.assertEqual(["cod.exe"], games[0]["executables"])
        self.assertEqual(["cod25-cod.exe"], games[1]["executables"])

    def test_steam_resolution_selects_unique_exact_title_not_first_result(self):
        response = _FakeResponse(
            {
                "items": [
                    {"id": 999, "name": "Some Other Game"},
                    {"id": 123, "name": "Alpha Game®"},
                ]
            }
        )
        with mock.patch.object(UPDATER.urllib.request, "urlopen", return_value=response):
            status, app_id, icon = UPDATER.resolve_steam_identity("Alpha Game")

        self.assertEqual("verified", status)
        self.assertEqual(123, app_id)
        self.assertIn("/123/", icon)

    def test_steam_resolution_rejects_ambiguous_or_inexact_results(self):
        response = _FakeResponse(
            {
                "items": [
                    {"id": 123, "name": "Alpha Game"},
                    {"id": 456, "name": "Alpha Game®"},
                    {"id": 789, "name": "Alpha Game Demo"},
                ]
            }
        )
        with mock.patch.object(UPDATER.urllib.request, "urlopen", return_value=response):
            status, app_id, icon = UPDATER.resolve_steam_identity("Alpha Game")

        self.assertEqual(("no_match", 0, ""), (status, app_id, icon))

    def test_fetch_steam_icon_url_extracts_library_capsule_2x(self):
        response = _FakeResponse(
            {
                "response": {
                    "store_items": [
                        {
                            "id": 123,
                            "assets": {
                                "asset_url_format": "steam/apps/123/${FILENAME}?t=12345",
                                "library_capsule_2x": "hash123/library_capsule_2x.jpg",
                                "library_capsule": "hash456/library_capsule.jpg",
                                "header": "hash789/header.jpg",
                            }
                        }
                    ]
                }
            }
        )
        with mock.patch.object(UPDATER.urllib.request, "urlopen", return_value=response):
            icon = UPDATER.fetch_steam_icon_url(123)

        self.assertIn("/123/", icon)
        self.assertIn("library_capsule_2x", icon)
        self.assertIn("https://shared.akamai.steamstatic.com", icon)

    def test_fetch_steam_icon_url_falls_back_to_header(self):
        response = _FakeResponse(
            {
                "response": {
                    "store_items": [
                        {
                            "id": 456,
                            "assets": {
                                "asset_url_format": "steam/apps/456/${FILENAME}",
                                "library_capsule": "hash/library_capsule.jpg",
                                "header": "hash/header.jpg",
                            }
                        }
                    ]
                }
            }
        )
        with mock.patch.object(UPDATER.urllib.request, "urlopen", return_value=response):
            icon = UPDATER.fetch_steam_icon_url(456)

        self.assertIn("library_capsule", icon)
        self.assertNotIn("header", icon)


class AddedDateTests(unittest.TestCase):
    TODAY = "2026-10-07"

    def test_known_games_keep_their_first_catalogue_date(self):
        games = [{"normalized": "known", "addedAt": "1999-01-01", "title": "Known"}]
        cache = {"known": {"normalized": "known", "addedAt": "2026-09-11"}}

        new_games = UPDATER.assign_added_dates(games, cache, self.TODAY)

        self.assertEqual(0, new_games)
        self.assertEqual("2026-09-11", games[0]["addedAt"])

    def test_only_games_absent_from_previous_catalogue_are_dated_today(self):
        games = [{"normalized": "known"}, {"normalized": "fresh"}]
        cache = {"known": {"normalized": "known", "addedAt": "2026-09-01"}}

        new_games = UPDATER.assign_added_dates(games, cache, self.TODAY)

        self.assertEqual(1, new_games)
        self.assertEqual("2026-09-01", games[0]["addedAt"])
        self.assertEqual(self.TODAY, games[1]["addedAt"])

    def test_known_games_without_valid_date_are_never_relabelled_as_new(self):
        games = [{"normalized": "legacy"}, {"normalized": "broken"}]
        cache = {"legacy": {"normalized": "legacy"}, "broken": {"addedAt": "2026-13-45"}}

        UPDATER.assign_added_dates(games, cache, self.TODAY)

        self.assertNotIn("addedAt", games[0])
        self.assertNotIn("addedAt", games[1])

    def test_missing_previous_catalogue_does_not_mark_everything_new(self):
        games = [{"normalized": "first"}, {"normalized": "second"}]

        new_games = UPDATER.assign_added_dates(games, {}, self.TODAY)

        self.assertEqual(0, new_games)
        self.assertTrue(all("addedAt" not in game for game in games))

    def test_added_date_is_written_last_for_stable_files(self):
        games = [{"addedAt": "2026-09-01", "normalized": "known", "title": "Known", "executables": ["a.exe"]}]
        cache = {"known": {"addedAt": "2026-09-01"}}

        UPDATER.assign_added_dates(games, cache, self.TODAY)

        self.assertEqual("addedAt", list(games[0])[-1])

    def test_added_date_format_validation(self):
        self.assertTrue(UPDATER.is_valid_added_date("2026-09-30"))
        for value in ("2026-9-30", "2026-09-31", "2026-09-30T00:00:00", "", None, 20260930):
            self.assertFalse(UPDATER.is_valid_added_date(value), value)


FORZA_WIKITEXT = """{{Infobox game
|cover = x.jpg
}}
{{Input
|playstation controllers   = hackable
|light bar support notes   = Requires [[Controller:DualSense#DSX|DSX]] and [https://github.com/cosmii02/RacingDSX RacingDSX].
|dualsense adaptive trigger support = hackable
|dualsense adaptive trigger support notes = Requires [[Controller:DualSense#DSX|DSX]] and [https://github.com/cosmii02/RacingDSX RacingDSX].<ref>{{Refcheck|user=a|date=2026-01-18|comment=ok}}</ref>
|dualsense haptics support  = false
|dualsense haptics support notes =
|haptic feedback notes     =
}}
"""

MULTILINE_WIKITEXT = """{{Input
|dualsense haptics support notes = Use the [https://github.com/AInine9/Onimusha-DualSense Onimusha DualSense] mod.
Wired only.<br>Use {{code|dirt4}} and '''bold''' [[Special K]].
|dualsense adaptive trigger support notes = [https://example.com/a Custom config]
}}
"""


class ManualFixDetailsTests(unittest.TestCase):
    def test_extracts_field_value_and_continuation_lines(self):
        raw = UPDATER.extract_wikitext_field(
            MULTILINE_WIKITEXT, "dualsense haptics support notes"
        )

        self.assertTrue(raw.startswith("Use the [https://github.com/AInine9"))
        self.assertIn("Wired only.", raw)
        self.assertNotIn("adaptive trigger", raw)

    def test_missing_or_empty_field_gives_empty_value(self):
        self.assertEqual("", UPDATER.extract_wikitext_field(FORZA_WIKITEXT, "absent field"))
        self.assertEqual(
            "",
            UPDATER.extract_wikitext_field(FORZA_WIKITEXT, "dualsense haptics support notes"),
        )

    def test_clean_wikitext_keeps_text_labels_and_links(self):
        raw = UPDATER.extract_wikitext_field(
            FORZA_WIKITEXT, "dualsense adaptive trigger support notes"
        )

        note, links = UPDATER.clean_wikitext(raw)

        self.assertEqual("Requires DSX and RacingDSX.", note)
        self.assertEqual(
            [
                {
                    "label": "DSX",
                    "url": "https://www.pcgamingwiki.com/wiki/Controller:DualSense#DSX",
                },
                {"label": "RacingDSX", "url": "https://github.com/cosmii02/RacingDSX"},
            ],
            links,
        )

    def test_clean_wikitext_strips_markup_and_skips_plain_wiki_pages(self):
        raw = UPDATER.extract_wikitext_field(
            MULTILINE_WIKITEXT, "dualsense haptics support notes"
        )

        note, links = UPDATER.clean_wikitext(raw)

        self.assertEqual(
            "Use the Onimusha DualSense mod. Wired only. Use dirt4 and bold Special K.",
            note,
        )
        self.assertEqual(["Onimusha DualSense"], [link["label"] for link in links])

    def test_clean_wikitext_bounds_note_and_link_count(self):
        raw = " ".join(["word"] * 400) + " " + " ".join(
            f"[https://example.com/{i} L{i}]" for i in range(10)
        )

        note, links = UPDATER.clean_wikitext(raw)

        self.assertLessEqual(len(note), UPDATER.MANUAL_FIX_NOTE_LIMIT + 1)
        self.assertTrue(note.endswith("…"))
        self.assertEqual(UPDATER.MANUAL_FIX_LINK_LIMIT, len(links))

    def test_clean_wikitext_deduplicates_links(self):
        _, links = UPDATER.clean_wikitext(
            "[https://example.com/a A] and [https://example.com/a again]"
        )

        self.assertEqual(1, len(links))

    def test_tool_detection_names_known_helpers_only(self):
        note, links = UPDATER.clean_wikitext(
            "Requires [[Controller:DualSense#DSX|DSX]] and "
            "[https://github.com/cosmii02/RacingDSX RacingDSX], or use [[Special K]]."
        )
        self.assertEqual(
            ["DSX", "RacingDSX", "Special K"],
            UPDATER.detect_manual_fix_tools(note, links),
        )

        note, links = UPDATER.clean_wikitext(
            "[https://github.com/josealissonbr/DualSenseAT Custom config]. Requires DSX."
        )
        self.assertEqual(["DSX", "DualSenseAT"], UPDATER.detect_manual_fix_tools(note, links))

        self.assertEqual([], UPDATER.detect_manual_fix_tools("Wired only.", []))

    def test_details_are_limited_to_requested_features_and_skip_empty_ones(self):
        details = UPDATER.parse_manual_fix_details(
            FORZA_WIKITEXT, ["adaptiveTriggers", "hapticFeedback"]
        )

        self.assertEqual(["adaptiveTriggers"], list(details))
        self.assertEqual(["DSX", "RacingDSX"], details["adaptiveTriggers"]["tools"])
        self.assertEqual({}, UPDATER.parse_manual_fix_details(FORZA_WIKITEXT, ["hapticFeedback"]))

    def test_page_title_comes_from_the_manual_fix_url(self):
        self.assertEqual(
            "Forza Horizon 5",
            UPDATER.pcgw_page_title("https://www.pcgamingwiki.com/wiki/Forza_Horizon_5"),
        )
        self.assertEqual(
            "Grand Theft Auto: San Andreas",
            UPDATER.pcgw_page_title(
                "https://www.pcgamingwiki.com/wiki/Grand_Theft_Auto%3A_San_Andreas"
            ),
        )
        self.assertEqual("", UPDATER.pcgw_page_title("https://example.com/other"))
        self.assertEqual("", UPDATER.pcgw_page_title(""))

    def test_enrichment_fills_manual_fix_games_and_clears_the_others(self):
        games = [
            {
                "normalized": "forzahorizon5",
                "adaptiveTriggersManualFix": True,
                "manualFixUrl": "https://www.pcgamingwiki.com/wiki/Forza_Horizon_5",
            },
            {"normalized": "native", "manualFixDetails": {"adaptiveTriggers": {}}},
        ]
        requested = []

        def fake_fetch(title):
            requested.append(title)
            return FORZA_WIKITEXT

        stats = UPDATER.enrich_with_manual_fix_details(games, {}, fetch=fake_fetch, pause=0)

        self.assertEqual(["Forza Horizon 5"], requested)
        self.assertEqual(["adaptiveTriggers"], list(games[0]["manualFixDetails"]))
        self.assertNotIn("manualFixDetails", games[1])
        self.assertEqual({"fetched": 1, "cached": 0, "empty": 0}, stats)

    def test_failed_fetch_keeps_cached_details_for_current_features_only(self):
        cached_entry = {"note": "Cached", "links": [], "tools": []}
        cache = {
            "game": {
                "manualFixDetails": {
                    "adaptiveTriggers": cached_entry,
                    "hapticFeedback": cached_entry,
                }
            }
        }
        games = [
            {
                "normalized": "game",
                "adaptiveTriggersManualFix": True,
                "manualFixUrl": "https://www.pcgamingwiki.com/wiki/Game",
            }
        ]

        stats = UPDATER.enrich_with_manual_fix_details(
            games, cache, fetch=lambda title: None, pause=0
        )

        self.assertEqual({"adaptiveTriggers": cached_entry}, games[0]["manualFixDetails"])
        self.assertEqual({"fetched": 0, "cached": 1, "empty": 0}, stats)

    def test_game_without_any_note_has_no_details_entry(self):
        games = [
            {
                "normalized": "game",
                "hapticFeedbackManualFix": True,
                "manualFixUrl": "https://www.pcgamingwiki.com/wiki/Game",
            }
        ]

        stats = UPDATER.enrich_with_manual_fix_details(
            games, {}, fetch=lambda title: FORZA_WIKITEXT, pause=0
        )

        self.assertNotIn("manualFixDetails", games[0])
        self.assertEqual(1, stats["empty"])

    def test_fetch_reads_wikitext_and_tolerates_errors(self):
        payload = {"parse": {"wikitext": {"*": "{{Input}}"}}}
        with mock.patch.object(
            UPDATER.urllib.request, "urlopen", return_value=_FakeResponse(payload)
        ):
            self.assertEqual("{{Input}}", UPDATER.fetch_pcgw_wikitext("Game"))

        with mock.patch.object(
            UPDATER.urllib.request,
            "urlopen",
            return_value=_FakeResponse({"error": {"info": "missing"}}),
        ):
            self.assertIsNone(UPDATER.fetch_pcgw_wikitext("Game"))

        with mock.patch.object(UPDATER.urllib.request, "urlopen", side_effect=OSError("down")):
            self.assertIsNone(UPDATER.fetch_pcgw_wikitext("Game"))


class ManualFixValidationTests(unittest.TestCase):
    @staticmethod
    def _validator():
        path = pathlib.Path(__file__).parents[1] / "scripts" / "validate_supported_games.py"
        spec = importlib.util.spec_from_file_location("validate_supported_games", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    @staticmethod
    def _game(**details):
        return {
            "title": "Game",
            "requiresManualFix": True,
            "adaptiveTriggersManualFix": True,
            "manualFixDetails": {
                "adaptiveTriggers": {
                    "note": "Requires DSX.",
                    "links": [{"label": "DSX", "url": "https://example.com/dsx"}],
                    "tools": ["DSX"],
                    **details,
                }
            },
        }

    def test_valid_details_and_absent_details_pass(self):
        validator = self._validator()

        validator.validate_manual_fix_details(self._game())
        validator.validate_manual_fix_details({"title": "Native"})

    def test_invalid_details_are_rejected(self):
        validator = self._validator()
        broken = [
            self._game(tools=["Unknown"]),
            self._game(links=[{"label": "x", "url": "http://insecure"}]),
            self._game(note=5),
            self._game(note="", links=[]),
        ]
        no_flag = self._game()
        no_flag.pop("adaptiveTriggersManualFix")
        wrong_game = self._game()
        wrong_game["requiresManualFix"] = False

        for game in broken + [no_flag, wrong_game]:
            with self.assertRaises(ValueError):
                validator.validate_manual_fix_details(game)


class _FakeResponse:
    def __init__(self, payload):
        self.payload = json.dumps(payload).encode("utf-8")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, traceback):
        return False

    def read(self):
        return self.payload


if __name__ == "__main__":
    unittest.main()
