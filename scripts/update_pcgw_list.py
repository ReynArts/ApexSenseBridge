#!/usr/bin/env python3
"""
update_pcgw_list.py
Fetches PC games with DualSense Adaptive Triggers and Haptic Feedback
from PCGamingWiki API, resolves Steam cover thumbnails, and generates data/supported_games.json.
"""

import html
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime, timezone

PCGW_API = "https://www.pcgamingwiki.com/w/api.php"
STEAM_SEARCH_API = "https://store.steampowered.com/api/storesearch/"
DISCORD_DETECTABLE_API = "https://discord.com/api/v9/applications/detectable"
USER_AGENT = "ApexSenseBridge-Updater/1.0 (https://github.com/ReynArts/ApexSenseBridge)"

ADAPTIVE_PAGES = [
    "List of games that support PlayStation adaptive triggers",
    "List of games that support Playstation adaptive triggers",
]
HAPTIC_PAGES = [
    "List of games that support Dualsense haptic feedback",
    "List of games that support DualSense haptic feedback",
]

SPECIAL_PROFILES = {
    "spiderman2": "spider-man-2",
    "marvelsspiderman2": "spider-man-2",
    "milesmorales": "miles-morales",
    "marvelsspidermanmilesmorales": "miles-morales",
    "ghostoftsushima": "ghost-of-tsushima",
    "ghostoftsushimadirectorscut": "ghost-of-tsushima",
    "warframe": "warframe",
}

BUILTIN_GAMES = [
    {
        "title": "Marvel's Spider-Man 2",
        "normalized": "marvelsspiderman2",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "spider-man-2",
        "steamAppId": 0,
        "steamAppIdVerified": False,
        "iconUrl": "",
    },
    {
        "title": "Marvel's Spider-Man: Miles Morales",
        "normalized": "marvelsspidermanmilesmorales",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "miles-morales",
        "steamAppId": 1817190,
        "steamAppIdVerified": True,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1817190/library_600x900.jpg",
    },
    {
        "title": "Ghost of Tsushima DIRECTOR'S CUT",
        "normalized": "ghostoftsushimadirectorscut",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "ghost-of-tsushima",
        "steamAppId": 2215430,
        "steamAppIdVerified": True,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2215430/library_600x900.jpg",
    },
    {
        "title": "Warframe",
        "normalized": "warframe",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "warframe",
        "steamAppId": 230410,
        "steamAppIdVerified": True,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/230410/library_600x900.jpg",
    },
    {
        "title": "Call of Duty: Modern Warfare 4 Beta",
        "normalized": "callofdutymodernwarfare4beta",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "standard",
        "steamAppId": 1938090,
        "steamAppIdVerified": False,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1938090/library_600x900.jpg",
    },
    {
        "title": "Grand Theft Auto V",
        "normalized": "grandtheftautov",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "standard",
        "steamAppId": 271590,
        "steamAppIdVerified": True,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/271590/library_600x900.jpg",
    },
]


def normalize_title(title: str) -> str:
    """Removes all non-alphanumeric characters and converts to lowercase."""
    if not title:
        return ""
    return "".join(character.lower() for character in title if character.isalnum())


def get_special_profile(norm: str) -> str:
    for key, prof in SPECIAL_PROFILES.items():
        if key in norm:
            return prof
    return "standard"


def normalize_executable_name(value: str) -> str:
    """Returns a safe Windows executable basename from a Discord path."""
    if not isinstance(value, str):
        return ""
    name = value.strip().replace("\\", "/").rsplit("/", 1)[-1].strip()
    if not name.lower().endswith(".exe"):
        return ""
    if not name or re.search(r'[<>:"/\\|?*\x00-\x1f]', name):
        return ""
    return name


def is_ancillary_executable(name: str) -> bool:
    """Rejects launchers and tools that Discord does not always flag as launchers."""
    normalized = name.casefold()
    exact_tools = {
        "content manager.exe",
        "crashreportclient.exe",
    }
    tool_markers = (
        "launcher",
        "servermanager",
        "showroom",
        "editor",
        "benchmark",
        "crashreport",
        "crashpad",
        "configurator",
        "configurationtool",
        "updater",
        "uninstaller",
        "diagnostic",
    )
    return normalized in exact_tools or any(marker in normalized for marker in tool_markers)


def extract_discord_executables(applications: list) -> dict:
    """Builds a Steam AppID -> Windows non-launcher executable names index."""
    result = {}
    for application in applications:
        if not isinstance(application, dict):
            continue

        steam_app_ids = set()
        for sku in application.get("third_party_skus") or []:
            if not isinstance(sku, dict):
                continue
            if str(sku.get("distributor", "")).strip().lower() != "steam":
                continue
            raw_id = str(sku.get("id", "")).strip()
            if raw_id.isdigit() and int(raw_id) > 0:
                steam_app_ids.add(int(raw_id))

        if not steam_app_ids:
            continue

        executable_names = set()
        for executable in application.get("executables") or []:
            if not isinstance(executable, dict):
                continue
            if str(executable.get("os", "")).strip().lower() != "win32":
                continue
            if executable.get("is_launcher") is True:
                continue
            name = normalize_executable_name(executable.get("name"))
            if name and not is_ancillary_executable(name):
                executable_names.add(name)

        if not executable_names:
            continue

        for steam_app_id in steam_app_ids:
            result.setdefault(steam_app_id, set()).update(executable_names)

    return {
        steam_app_id: sorted(names, key=str.casefold)
        for steam_app_id, names in result.items()
    }


def fetch_discord_executables() -> dict:
    """Downloads Discord's detectable-app list once for offline database generation."""
    req = urllib.request.Request(
        DISCORD_DETECTABLE_API,
        headers={"User-Agent": USER_AGENT, "Accept": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            applications = json.loads(resp.read().decode("utf-8"))
        if not isinstance(applications, list):
            raise ValueError("Discord response was not an application array")
        index = extract_discord_executables(applications)
        print(
            f"[+] Indexed {sum(len(names) for names in index.values())} executable mappings "
            f"for {len(index)} Steam AppIDs from Discord."
        )
        return index
    except Exception as err:
        print(
            f"[WARN] Discord executable enrichment unavailable: {err}. "
            "Keeping previously cached executable names.",
            file=sys.stderr,
        )
        return None


def cached_executables(cached: dict) -> list:
    values = cached.get("executables", []) if isinstance(cached, dict) else []
    if not isinstance(values, list):
        return []
    names = {normalize_executable_name(value) for value in values}
    names.discard("")
    return sorted(names, key=str.casefold)


def enrich_with_discord_executables(games: list, discord_index: dict) -> dict:
    """Adds exact AppID matches and removes executable names ambiguous in our DB."""
    matched_games = 0
    imported_names = 0

    if discord_index is not None:
        for game in games:
            if not game.get("steamAppIdVerified", False):
                game.pop("executables", None)
                continue
            steam_app_id = game.get("steamAppId", 0)
            names = discord_index.get(steam_app_id)
            if names:
                game["executables"] = list(names)
                matched_games += 1
                imported_names += len(names)

    owners = {}
    for game in games:
        if not game.get("steamAppIdVerified", False):
            game.pop("executables", None)
            continue
        normalized = game.get("normalized", "")
        clean_names = cached_executables(game)
        if clean_names:
            game["executables"] = clean_names
        else:
            game.pop("executables", None)
        for name in clean_names:
            owners.setdefault(name.casefold(), set()).add(normalized)

    ambiguous = {name for name, game_ids in owners.items() if len(game_ids) > 1}
    if ambiguous:
        for game in games:
            names = game.get("executables")
            if not names:
                continue
            unique_names = [name for name in names if name.casefold() not in ambiguous]
            if unique_names:
                game["executables"] = unique_names
            else:
                game.pop("executables", None)

    return {
        "matchedGames": matched_games,
        "importedNames": imported_names,
        "ambiguousNames": len(ambiguous),
    }


def fetch_pcgw_titles(page_titles: list) -> list:
    """Queries PCGW parse API for table game titles across candidate page titles."""
    for page_title in page_titles:
        params = {
            "action": "parse",
            "page": page_title,
            "prop": "text",
            "format": "json",
        }
        url = f"{PCGW_API}?{urllib.parse.urlencode(params)}"
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})

        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                data = json.loads(resp.read().decode("utf-8"))
        except Exception as err:
            print(f"[WARN] Failed to fetch {page_title}: {err}", file=sys.stderr)
            continue

        if data.get("error"):
            print(f"[INFO] Page '{page_title}' not found on PCGW: {data['error'].get('info', '')}", file=sys.stderr)
            continue

        parse_data = data.get("parse", {})
        text_content = parse_data.get("text", {}).get("*", "")
        if not text_content:
            continue

        pattern = re.compile(r'<tr>\s*<td><a href="[^"]*" title="([^"]*)">([^<]*)</a></td>')
        matches = pattern.findall(text_content)

        titles = []
        for raw_title, text_title in matches:
            title = html.unescape(raw_title or text_title).strip()
            if title:
                titles.append(title)

        if titles:
            print(f"[+] Successfully fetched {len(titles)} titles from PCGW page: '{page_title}'")
            return titles

    print(f"[ERROR] Could not fetch any titles from candidate pages: {page_titles}", file=sys.stderr)
    return []


def resolve_steam_identity(title: str) -> tuple:
    """Resolves a unique exact normalized Steam title; never accepts the first result blindly."""
    try:
        clean_title = re.sub(r"[\u2122\u00AE\u00A9]", "", title).strip()
        params = {"term": clean_title, "l": "english", "cc": "US"}
        url = f"{STEAM_SEARCH_API}?{urllib.parse.urlencode(params)}"
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})

        with urllib.request.urlopen(req, timeout=8) as resp:
            data = json.loads(resp.read().decode("utf-8"))

        expected = normalize_title(clean_title)
        exact_matches = {}
        for item in data.get("items", []):
            if not isinstance(item, dict):
                continue
            candidate_name = re.sub(
                r"[\u2122\u00AE\u00A9]", "", str(item.get("name", ""))
            ).strip()
            if normalize_title(candidate_name) != expected:
                continue
            app_id = int(item.get("id", 0))
            if app_id > 0:
                exact_matches[app_id] = candidate_name

        if len(exact_matches) == 1:
            app_id = next(iter(exact_matches))
            icon = (
                "https://shared.akamai.steamstatic.com/store_item_assets/steam/"
                f"apps/{app_id}/library_600x900.jpg"
            )
            return "verified", app_id, icon
        return "no_match", 0, ""
    except Exception as err:
        return "error", 0, str(err)


def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output_path = os.path.join(root_dir, "data", "supported_games.json")

    # 1. Load existing cache to avoid re-querying Steam for known games
    existing_cache = {}
    if os.path.exists(output_path):
        try:
            with open(output_path, "r", encoding="utf-8") as f:
                old_data = json.load(f)
                for g in old_data.get("games", []):
                    norm = g.get("normalized")
                    if norm:
                        existing_cache[norm] = g
        except Exception:
            pass

    print(f"[*] Fetching Adaptive Triggers list from PCGamingWiki...")
    adaptive_titles = fetch_pcgw_titles(ADAPTIVE_PAGES)
    print(f"[+] Found {len(adaptive_titles)} games with Adaptive Triggers.")

    print(f"[*] Fetching Haptic Feedback list from PCGamingWiki...")
    haptic_titles = fetch_pcgw_titles(HAPTIC_PAGES)
    print(f"[+] Found {len(haptic_titles)} games with Haptic Feedback.")

    if not adaptive_titles or not haptic_titles:
        print(f"[ERROR] Incomplete fetch (Adaptive: {len(adaptive_titles)}, Haptic: {len(haptic_titles)}). Aborting to prevent data loss.", file=sys.stderr)
        sys.exit(1)

    games_dict = {}

    for title in adaptive_titles:
        norm = normalize_title(title)
        if not norm:
            continue
        cached = existing_cache.get(norm, {})
        if norm not in games_dict:
            games_dict[norm] = {
                "title": title,
                "normalized": norm,
                "adaptiveTriggers": True,
                "hapticFeedback": False,
                "profile": get_special_profile(norm),
                "steamAppId": cached.get("steamAppId", 0),
                "steamAppIdVerified": bool(cached.get("steamAppIdVerified", False)),
                "iconUrl": cached.get("iconUrl", ""),
                "executables": cached_executables(cached),
            }
        else:
            games_dict[norm]["adaptiveTriggers"] = True

    for title in haptic_titles:
        norm = normalize_title(title)
        if not norm:
            continue
        cached = existing_cache.get(norm, {})
        if norm not in games_dict:
            games_dict[norm] = {
                "title": title,
                "normalized": norm,
                "adaptiveTriggers": False,
                "hapticFeedback": True,
                "profile": get_special_profile(norm),
                "steamAppId": cached.get("steamAppId", 0),
                "steamAppIdVerified": bool(cached.get("steamAppIdVerified", False)),
                "iconUrl": cached.get("iconUrl", ""),
                "executables": cached_executables(cached),
            }
        else:
            games_dict[norm]["hapticFeedback"] = True

    # Merge built-in verified entries
    for b in BUILTIN_GAMES:
        norm = b["normalized"]
        if norm not in games_dict:
            games_dict[norm] = dict(b)
        else:
            if b.get("profile") and b["profile"] != "standard":
                games_dict[norm]["profile"] = b["profile"]
            if b.get("iconUrl"):
                games_dict[norm]["iconUrl"] = b["iconUrl"]
            if b.get("steamAppId"):
                games_dict[norm]["steamAppId"] = b["steamAppId"]
                games_dict[norm]["steamAppIdVerified"] = bool(
                    b.get("steamAppIdVerified", False)
                )
            if b.get("adaptiveTriggers"):
                games_dict[norm]["adaptiveTriggers"] = True
            if b.get("hapticFeedback"):
                games_dict[norm]["hapticFeedback"] = True

    # Audit every legacy/unverified AppID once. Future runs reuse verified identities.
    need_resolve = [
        g for g in games_dict.values() if not g.get("steamAppIdVerified", False)
    ]
    if need_resolve:
        print(f"[*] Verifying exact Steam identities for {len(need_resolve)} games...")
        verified_count = 0
        unresolved_count = 0
        error_count = 0
        for g in need_resolve:
            status, app_id, detail = resolve_steam_identity(g["title"])
            if status == "verified":
                g["steamAppId"] = app_id
                g["steamAppIdVerified"] = True
                g["iconUrl"] = detail
                verified_count += 1
            elif status == "no_match":
                g["steamAppId"] = 0
                g["steamAppIdVerified"] = False
                if not g.get("iconUrl"):
                    g["iconUrl"] = ""
                unresolved_count += 1
            else:
                g["steamAppIdVerified"] = False
                error_count += 1
            # Slight delay to respect Steam rate limits
            time.sleep(0.05)
        print(
            f"[+] Steam identity audit: {verified_count} verified, "
            f"{unresolved_count} unresolved, {error_count} request errors."
        )

    output_list = sorted(games_dict.values(), key=lambda g: g["title"].lower())

    print("[*] Fetching Discord detectable executables...")
    discord_index = fetch_discord_executables()
    discord_stats = enrich_with_discord_executables(output_list, discord_index)
    print(
        "[+] Discord enrichment: "
        f"{discord_stats['matchedGames']} games matched, "
        f"{discord_stats['importedNames']} names imported, "
        f"{discord_stats['ambiguousNames']} ambiguous names excluded."
    )

    if len(output_list) < 150:
        print(f"[ERROR] Extracted list suspiciously small ({len(output_list)} games). Aborting write to prevent data loss.", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    payload = {
        "version": 1,
        "updatedAt": datetime.now(timezone.utc).isoformat(),
        "totalGames": len(output_list),
        "discordExecutableGames": sum(1 for game in output_list if game.get("executables")),
        "games": output_list,
    }

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)

    print(f"[SUCCESS] Successfully generated {output_path} with {len(output_list)} supported games.")


if __name__ == "__main__":
    main()
