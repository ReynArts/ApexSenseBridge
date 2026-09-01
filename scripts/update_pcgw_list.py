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
USER_AGENT = "ApexSenseBridge-Updater/1.0 (https://github.com/ReynArts/ApexSenseBridge)"

ADAPTIVE_PAGE = "List of games that support PlayStation adaptive triggers"
HAPTIC_PAGE = "List of games that support DualSense haptic feedback"

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
        "iconUrl": "",
    },
    {
        "title": "Marvel's Spider-Man: Miles Morales",
        "normalized": "marvelsspidermanmilesmorales",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "miles-morales",
        "steamAppId": 1817190,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1817190/library_600x900.jpg",
    },
    {
        "title": "Ghost of Tsushima DIRECTOR'S CUT",
        "normalized": "ghostoftsushimadirectorscut",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "ghost-of-tsushima",
        "steamAppId": 2215430,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2215430/library_600x900.jpg",
    },
    {
        "title": "Warframe",
        "normalized": "warframe",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "warframe",
        "steamAppId": 230410,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/230410/library_600x900.jpg",
    },
    {
        "title": "Call of Duty: Modern Warfare 4 Beta",
        "normalized": "callofduty",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "standard",
        "steamAppId": 1938090,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1938090/library_600x900.jpg",
    },
    {
        "title": "Grand Theft Auto V",
        "normalized": "grandtheftautov",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "standard",
        "steamAppId": 271590,
        "iconUrl": "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/271590/library_600x900.jpg",
    },
]


def normalize_title(title: str) -> str:
    """Removes all non-alphanumeric characters and converts to lowercase."""
    if not title:
        return ""
    return re.sub(r"[^a-zA-Z0-9]", "", title).lower()


def get_special_profile(norm: str) -> str:
    for key, prof in SPECIAL_PROFILES.items():
        if key in norm:
            return prof
    return "standard"


def fetch_pcgw_titles(page_title: str) -> list:
    """Queries PCGW parse API for table game titles."""
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
        return []

    parse_data = data.get("parse", {})
    text_content = parse_data.get("text", {}).get("*", "")
    if not text_content:
        print(f"[WARN] Empty text content for {page_title}", file=sys.stderr)
        return []

    pattern = re.compile(r'<tr>\s*<td><a href="[^"]*" title="([^"]*)">([^<]*)</a></td>')
    matches = pattern.findall(text_content)

    titles = []
    for raw_title, text_title in matches:
        title = html.unescape(raw_title or text_title).strip()
        if title:
            titles.append(title)

    return titles


def resolve_steam_cover(title: str) -> tuple:
    """Queries Steam store search for a matching app ID and portrait cover image."""
    try:
        clean_title = re.sub(r"[\u2122\u00AE\u00A9]", "", title).strip()
        params = {"term": clean_title, "l": "english", "cc": "US"}
        url = f"{STEAM_SEARCH_API}?{urllib.parse.urlencode(params)}"
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})

        with urllib.request.urlopen(req, timeout=8) as resp:
            data = json.loads(resp.read().decode("utf-8"))

        items = data.get("items", [])
        if items:
            first = items[0]
            app_id = int(first.get("id", 0))
            if app_id > 0:
                return app_id, f"https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/{app_id}/library_600x900.jpg"
    except Exception:
        pass
    return 0, ""


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
    adaptive_titles = fetch_pcgw_titles(ADAPTIVE_PAGE)
    print(f"[+] Found {len(adaptive_titles)} games with Adaptive Triggers.")

    print(f"[*] Fetching Haptic Feedback list from PCGamingWiki...")
    haptic_titles = fetch_pcgw_titles(HAPTIC_PAGE)
    print(f"[+] Found {len(haptic_titles)} games with Haptic Feedback.")

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
                "iconUrl": cached.get("iconUrl", ""),
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
                "iconUrl": cached.get("iconUrl", ""),
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
            if b.get("adaptiveTriggers"):
                games_dict[norm]["adaptiveTriggers"] = True
            if b.get("hapticFeedback"):
                games_dict[norm]["hapticFeedback"] = True

    # Resolve Steam thumbnails for any games that don't have an iconUrl yet
    need_resolve = [g for g in games_dict.values() if not g.get("iconUrl")]
    if need_resolve:
        print(f"[*] Resolving Steam cover thumbnails for {len(need_resolve)} games...")
        count = 0
        for g in need_resolve:
            app_id, icon_url = resolve_steam_cover(g["title"])
            if icon_url:
                g["steamAppId"] = app_id
                g["iconUrl"] = icon_url
                count += 1
            # Slight delay to respect Steam rate limits
            time.sleep(0.05)
        print(f"[+] Successfully resolved {count} cover thumbnails.")

    output_list = sorted(games_dict.values(), key=lambda g: g["title"].lower())

    if len(output_list) < 20:
        print(f"[ERROR] Extracted list suspiciously small ({len(output_list)} games). Aborting write to prevent data loss.", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    payload = {
        "version": 1,
        "updatedAt": datetime.now(timezone.utc).isoformat(),
        "totalGames": len(output_list),
        "games": output_list,
    }

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)

    print(f"[SUCCESS] Successfully generated {output_path} with {len(output_list)} supported games.")


if __name__ == "__main__":
    main()
