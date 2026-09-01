#!/usr/bin/env python3
"""
update_pcgw_list.py
Fetches PC games with DualSense Adaptive Triggers and Haptic Feedback
from PCGamingWiki API and generates data/supported_games.json.
"""

import html
import json
import os
import re
import sys
import urllib.parse
import urllib.request
from datetime import datetime, timezone

PCGW_API = "https://www.pcgamingwiki.com/w/api.php"
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
    },
    {
        "title": "Marvel's Spider-Man: Miles Morales",
        "normalized": "marvelsspidermanmilesmorales",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "miles-morales",
    },
    {
        "title": "Ghost of Tsushima DIRECTOR'S CUT",
        "normalized": "ghostoftsushimadirectorscut",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "ghost-of-tsushima",
    },
    {
        "title": "Warframe",
        "normalized": "warframe",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "warframe",
    },
    {
        "title": "Call of Duty: Modern Warfare 4 Beta",
        "normalized": "callofduty",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "standard",
    },
    {
        "title": "Grand Theft Auto V",
        "normalized": "grandtheftautov",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "standard",
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

    # Match table rows containing game titles: <tr><td><a href="..." title="...">Game Title</a></td>
    pattern = re.compile(r'<tr>\s*<td><a href="[^"]*" title="([^"]*)">([^<]*)</a></td>')
    matches = pattern.findall(text_content)

    titles = []
    for raw_title, text_title in matches:
        # Prefer the raw title or text title, decoded
        title = html.unescape(raw_title or text_title).strip()
        if title:
            titles.append(title)

    return titles


def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output_path = os.path.join(root_dir, "data", "supported_games.json")

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
        if norm not in games_dict:
            games_dict[norm] = {
                "title": title,
                "normalized": norm,
                "adaptiveTriggers": True,
                "hapticFeedback": False,
                "profile": get_special_profile(norm),
            }
        else:
            games_dict[norm]["adaptiveTriggers"] = True

    for title in haptic_titles:
        norm = normalize_title(title)
        if not norm:
            continue
        if norm not in games_dict:
            games_dict[norm] = {
                "title": title,
                "normalized": norm,
                "adaptiveTriggers": False,
                "hapticFeedback": True,
                "profile": get_special_profile(norm),
            }
        else:
            games_dict[norm]["hapticFeedback"] = True

    # Merge built-in verified entries
    for b in BUILTIN_GAMES:
        norm = b["normalized"]
        if norm not in games_dict:
            games_dict[norm] = dict(b)
        else:
            if b["profile"] != "standard":
                games_dict[norm]["profile"] = b["profile"]
            if b.get("adaptiveTriggers"):
                games_dict[norm]["adaptiveTriggers"] = True
            if b.get("hapticFeedback"):
                games_dict[norm]["hapticFeedback"] = True

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
