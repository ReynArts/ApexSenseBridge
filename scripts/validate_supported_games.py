#!/usr/bin/env python3
"""Strict validation for the generated supported-games database."""

import json
import pathlib
import sys


ROOT = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import update_pcgw_list as updater


def validate_manual_fix_details(game: dict) -> None:
    title = game.get("title", "<untitled>")
    details = game.get("manualFixDetails")
    if details is None:
        return
    if not game.get("requiresManualFix"):
        raise ValueError(f"{title}: manualFixDetails requires a manual-fix game")
    if not isinstance(details, dict) or not details:
        raise ValueError(f"{title}: manualFixDetails must be a non-empty object")

    known_tools = {name for name, _ in updater.MANUAL_FIX_TOOLS}
    for feature, entry in details.items():
        if feature not in updater.MANUAL_FIX_FEATURES or not game.get(f"{feature}ManualFix"):
            raise ValueError(f"{title}: manualFixDetails.{feature} has no matching manual fix")
        if not isinstance(entry, dict):
            raise ValueError(f"{title}: manualFixDetails.{feature} must be an object")
        note = entry.get("note")
        links = entry.get("links")
        tools = entry.get("tools")
        if not isinstance(note, str) or len(note) > updater.MANUAL_FIX_NOTE_LIMIT + 1:
            raise ValueError(f"{title}: manualFixDetails.{feature}.note is invalid")
        if not isinstance(links, list) or len(links) > updater.MANUAL_FIX_LINK_LIMIT:
            raise ValueError(f"{title}: manualFixDetails.{feature}.links is invalid")
        for link in links:
            if (
                not isinstance(link, dict)
                or not isinstance(link.get("label"), str)
                or not link["label"]
                or not isinstance(link.get("url"), str)
                or not link["url"].startswith("https://")
            ):
                raise ValueError(f"{title}: manualFixDetails.{feature} has an invalid link {link!r}")
        if not isinstance(tools, list) or any(tool not in known_tools for tool in tools):
            raise ValueError(f"{title}: manualFixDetails.{feature}.tools is invalid")
        if not note and not links:
            raise ValueError(f"{title}: manualFixDetails.{feature} is empty")


def main() -> int:
    database_path = ROOT / "data" / "supported_games.json"
    with database_path.open("r", encoding="utf-8") as stream:
        data = json.load(stream)

    games = data.get("games", [])
    if len(games) < 200:
        raise ValueError(f"Expected at least 200 games, got {len(games)}")

    verified_app_ids = {}
    executable_owners = {}
    games_with_executables = 0

    for game in games:
        title = game.get("title", "<untitled>")
        normalized = game.get("normalized", "")
        steam_app_id = game.get("steamAppId", 0)
        verified = game.get("steamAppIdVerified", False) is True
        executables = game.get("executables", [])

        if not isinstance(executables, list):
            raise ValueError(f"{title}: executables must be an array")
        if executables and (not verified or not isinstance(steam_app_id, int) or steam_app_id <= 0):
            raise ValueError(f"{title}: executable names require a verified Steam AppID")

        if verified:
            if not isinstance(steam_app_id, int) or steam_app_id <= 0:
                raise ValueError(f"{title}: verified Steam AppID is invalid")
            previous = verified_app_ids.setdefault(steam_app_id, title)
            if previous != title:
                raise ValueError(
                    f"Steam AppID {steam_app_id} is shared by {previous!r} and {title!r}"
                )

        added_at = game.get("addedAt")
        if added_at is not None and not updater.is_valid_added_date(added_at):
            raise ValueError(f"{title}: addedAt must be an ISO date (YYYY-MM-DD), got {added_at!r}")

        validate_manual_fix_details(game)

        if executables:
            games_with_executables += 1
        for executable in executables:
            name = updater.normalize_executable_name(executable)
            if not name or name != executable:
                raise ValueError(f"{title}: invalid executable name {executable!r}")
            if updater.is_ancillary_executable(name):
                raise ValueError(f"{title}: ancillary executable was retained: {name}")
            owner = executable_owners.setdefault(name.casefold(), normalized)
            if owner != normalized:
                raise ValueError(f"Executable {name!r} is shared by multiple games")

    declared_count = data.get("discordExecutableGames", -1)
    if declared_count != games_with_executables:
        raise ValueError(
            f"discordExecutableGames is {declared_count}, expected {games_with_executables}"
        )

    print(
        f"Validated {len(games)} games, {len(verified_app_ids)} verified Steam AppIDs, "
        f"and {games_with_executables} games with Discord executables."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
