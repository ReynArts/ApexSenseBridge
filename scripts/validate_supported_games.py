#!/usr/bin/env python3
"""Strict validation for the generated supported-games database."""

import json
import pathlib
import sys


ROOT = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import update_pcgw_list as updater


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
