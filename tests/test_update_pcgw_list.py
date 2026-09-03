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
