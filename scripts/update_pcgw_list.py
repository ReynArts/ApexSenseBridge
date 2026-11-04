#!/usr/bin/env python3
"""
update_pcgw_list.py
Fetches PC games with DualSense Adaptive Triggers and Haptic Feedback
from PCGamingWiki API, resolves Steam cover thumbnails, and generates data/supported_games.json.
"""

import hashlib
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
    "deathstranding2": "death-stranding-2",
    "deathstranding2onthebeach": "death-stranding-2",
}

BUILTIN_GAMES = [
    {
        "title": "Call of Duty",
        "normalized": "callofduty",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "standard",
        "steamAppId": 1938090,
        "steamAppIdVerified": True,
        "iconUrl": "",
        # Current releases enter through the shared Call of Duty HQ process.
        "executables": ["cod.exe"],
    },
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
        "iconUrl": "",
    },
    {
        "title": "Ghost of Tsushima DIRECTOR'S CUT",
        "normalized": "ghostoftsushimadirectorscut",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "ghost-of-tsushima",
        "steamAppId": 2215430,
        "steamAppIdVerified": True,
        "iconUrl": "",
    },
    {
        "title": "Warframe",
        "normalized": "warframe",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "warframe",
        "steamAppId": 230410,
        "steamAppIdVerified": True,
        "iconUrl": "",
    },
    {
        "title": "Call of Duty: Modern Warfare 4 Beta",
        "normalized": "callofdutymodernwarfare4beta",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "standard",
        "steamAppId": 1938090,
        "steamAppIdVerified": False,
        "iconUrl": "",
    },
    {
        "title": "Grand Theft Auto V",
        "normalized": "grandtheftautov",
        "adaptiveTriggers": True,
        "hapticFeedback": True,
        "profile": "standard",
        "steamAppId": 271590,
        "steamAppIdVerified": True,
        "iconUrl": "",
    },
]

# Discord may classify a shared game hub as a launcher and omit it. These
# basenames are runtime game processes that must keep a single canonical owner.
PINNED_EXECUTABLE_OWNERS = {
    "cod.exe": "callofduty",
}


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

    # Apply curated shared-launcher ownership after Discord collision cleanup.
    # This both preserves the alias when Discord omits it and prevents a future
    # feed change from assigning the same basename to an individual COD title.
    games_by_normalized = {game.get("normalized", ""): game for game in games}
    for executable_name, owner_normalized in PINNED_EXECUTABLE_OWNERS.items():
        executable_key = executable_name.casefold()
        for game in games:
            names = [
                name
                for name in cached_executables(game)
                if name.casefold() != executable_key
            ]
            if names:
                game["executables"] = names
            else:
                game.pop("executables", None)

        owner = games_by_normalized.get(owner_normalized)
        if owner and owner.get("steamAppIdVerified", False):
            owner["executables"] = sorted(
                cached_executables(owner) + [executable_name], key=str.casefold
            )

    return {
        "matchedGames": matched_games,
        "importedNames": imported_names,
        "ambiguousNames": len(ambiguous),
    }


def is_valid_added_date(value) -> bool:
    """Accepts only plain ISO calendar dates (YYYY-MM-DD)."""
    if not isinstance(value, str) or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", value):
        return False
    try:
        datetime.strptime(value, "%Y-%m-%d")
        return True
    except ValueError:
        return False


def assign_added_dates(games: list, existing_cache: dict, today: str) -> int:
    """Records when each game first entered the catalogue (issue #29).

    Known games keep their stored date. Only games absent from a non-empty
    previous catalogue are dated today, so a missing or unreadable cache never
    relabels the whole list as new. The field is always written last to keep
    regenerated files stable.
    """
    new_games = 0
    for game in games:
        cached = existing_cache.get(game.get("normalized", ""))
        game.pop("addedAt", None)
        if isinstance(cached, dict):
            added = cached.get("addedAt")
            if is_valid_added_date(added):
                game["addedAt"] = added
        elif existing_cache:
            game["addedAt"] = today
            new_games += 1
    return new_games


def parse_pcgw_support_rows(text_content: str) -> dict:
    """Extracts game titles, wiki links, and support states from a PCGW list."""
    games = {}
    row_pattern = re.compile(r"<tr[^>]*>(.*?)</tr>", re.IGNORECASE | re.DOTALL)
    title_pattern = re.compile(
        r'<td[^>]*>\s*<a href="([^"]*)" title="([^"]*)">',
        re.IGNORECASE | re.DOTALL,
    )
    support_pattern = re.compile(
        r'<div title="([^"]*)" class="[^"]*\btickcross-([a-z-]+)\b[^"]*"',
        re.IGNORECASE,
    )

    for row in row_pattern.findall(text_content or ""):
        title_match = title_pattern.search(row)
        support_match = support_pattern.search(row)
        if not title_match or not support_match:
            continue

        title = html.unescape(title_match.group(2)).strip()
        if not title:
            continue

        href = html.unescape(title_match.group(1)).strip()
        support_class = support_match.group(2).casefold()
        games[title] = {
            "requiresManualFix": support_class == "hackable",
            "pcgwUrl": urllib.parse.urljoin("https://www.pcgamingwiki.com", href),
        }

    return games


def fetch_pcgw_support(page_titles: list) -> dict:
    """Queries PCGW for game titles and their native/manual-fix state."""
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

        games = parse_pcgw_support_rows(text_content)
        if games:
            manual_fix_count = sum(
                1 for details in games.values() if details["requiresManualFix"]
            )
            print(
                f"[+] Successfully fetched {len(games)} titles from PCGW page: "
                f"'{page_title}' ({manual_fix_count} require a manual fix)"
            )
            return games

    print(f"[ERROR] Could not fetch any titles from candidate pages: {page_titles}", file=sys.stderr)
    return {}


def fetch_pcgw_titles(page_titles: list) -> list:
    """Compatibility wrapper returning only titles."""
    return list(fetch_pcgw_support(page_titles))


MANUAL_FIX_FEATURES = {
    "adaptiveTriggers": "dualsense adaptive trigger support notes",
    "hapticFeedback": "dualsense haptics support notes",
}
MANUAL_FIX_TRANSLATION_LANGUAGES = ("es", "fr", "ja", "ko", "pt", "ru", "vi", "zh")
MANUAL_FIX_NOTE_LIMIT = 600
MANUAL_FIX_LINK_LIMIT = 6
MANUAL_FIX_TOOLS = (
    ("DSX", re.compile(r"\bDSX\b")),
    ("DSY", re.compile(r"\bDSY\b")),
    ("RacingDSX", re.compile(r"RacingDSX", re.IGNORECASE)),
    ("DualSenseAT", re.compile(r"DualSense\s?AT\b")),
    ("Special K", re.compile(r"Special\s?K\b", re.IGNORECASE)),
    ("SimHub", re.compile(r"Sim\s?Hub", re.IGNORECASE)),
)

_REF_PATTERN = re.compile(r"<ref\b[^>]*/>|<ref\b[^>]*>.*?</ref>", re.IGNORECASE | re.DOTALL)
_BREAK_PATTERN = re.compile(r"<br\s*/?>", re.IGNORECASE)
_TAG_PATTERN = re.compile(r"<[^>]+>")
_CODE_TEMPLATE_PATTERN = re.compile(r"\{\{\s*(?:code|c)\s*\|([^{}]*)\}\}", re.IGNORECASE)
_TEMPLATE_PATTERN = re.compile(r"\{\{[^{}]*\}\}")
_LINK_PATTERN = re.compile(
    r"\[\[([^\[\]|]+)(?:\|([^\[\]]*))?\]\]|\[(https?://[^\s\]]+)(?:[ \t]+([^\]]*))?\]"
)
_EMPHASIS_PATTERN = re.compile(r"'{2,}")


def extract_wikitext_field(wikitext: str, field: str) -> str:
    wanted = field.casefold()
    collected = None
    for line in (wikitext or "").splitlines():
        if collected is None:
            match = re.match(r"^\|\s*([^=|]+?)\s*=(.*)$", line)
            if match and match.group(1).casefold() == wanted:
                collected = [match.group(2)]
            continue
        if line.startswith("|") or line.startswith("}}"):
            break
        collected.append(line)
    return "\n".join(collected).strip() if collected is not None else ""


def clean_wikitext(raw: str) -> tuple:
    links = []

    def replace_link(match):
        if match.group(1) is not None:
            target = html.unescape(match.group(1)).strip()
            label = html.unescape(match.group(2) or target).strip()
            if ":" in target and not target.casefold().startswith(("file:", "image:", "category:")):
                url = "https://www.pcgamingwiki.com/wiki/" + urllib.parse.quote(
                    target.replace(" ", "_"), safe=":#_(),'!-."
                )
                links.append({"label": label, "url": url})
            return label
        url = html.unescape(match.group(3)).strip()
        label = html.unescape(match.group(4) or "").strip()
        if not label:
            label = urllib.parse.urlparse(url).netloc
        links.append({"label": label, "url": url})
        return label

    text = _REF_PATTERN.sub("", raw or "")
    text = _BREAK_PATTERN.sub(" ", text)
    text = _CODE_TEMPLATE_PATTERN.sub(lambda m: m.group(1), text)
    text = _LINK_PATTERN.sub(replace_link, text)
    previous = None
    while previous != text:
        previous = text
        text = _TEMPLATE_PATTERN.sub("", text)
    text = _TAG_PATTERN.sub("", text)
    text = _EMPHASIS_PATTERN.sub("", html.unescape(text))
    text = re.sub(r"\s+", " ", text).strip()

    if len(text) > MANUAL_FIX_NOTE_LIMIT:
        text = text[:MANUAL_FIX_NOTE_LIMIT].rsplit(" ", 1)[0].rstrip(" ,;:.") + "…"

    unique_links = []
    seen_urls = set()
    for link in links:
        if link["url"] in seen_urls:
            continue
        seen_urls.add(link["url"])
        unique_links.append(link)
    return text, unique_links[:MANUAL_FIX_LINK_LIMIT]


def detect_manual_fix_tools(note: str, links: list) -> list:
    haystack = " ".join([note] + [f"{l['label']} {l['url']}" for l in links])
    return [name for name, pattern in MANUAL_FIX_TOOLS if pattern.search(haystack)]


def parse_manual_fix_details(wikitext: str, features: list) -> dict:
    details = {}
    for feature in features:
        field = MANUAL_FIX_FEATURES.get(feature)
        if not field:
            continue
        note, links = clean_wikitext(extract_wikitext_field(wikitext, field))
        if not note and not links:
            continue
        details[feature] = {
            "note": note,
            "links": links,
            "tools": detect_manual_fix_tools(note, links),
        }
    return details


def pcgw_page_title(url: str) -> str:
    path = urllib.parse.urlparse(url or "").path
    prefix = "/wiki/"
    if not path.startswith(prefix):
        return ""
    return urllib.parse.unquote(path[len(prefix):]).replace("_", " ")


def fetch_pcgw_wikitext(page_title: str):
    params = {
        "action": "parse",
        "page": page_title,
        "prop": "wikitext",
        "redirects": 1,
        "format": "json",
    }
    url = f"{PCGW_API}?{urllib.parse.urlencode(params)}"
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except Exception as err:
        print(f"[WARN] Failed to fetch wikitext of {page_title}: {err}", file=sys.stderr)
        return None
    if data.get("error"):
        print(f"[INFO] PCGW page '{page_title}' not found: {data['error'].get('info', '')}", file=sys.stderr)
        return None
    return data.get("parse", {}).get("wikitext", {}).get("*") or None


def manual_fix_note_key(note: str) -> str:
    return hashlib.sha256((note or "").encode("utf-8")).hexdigest()[:16]


def load_manual_fix_translations(path: str) -> dict:
    empty = {"notes": {}, "labels": {}}
    try:
        with open(path, "r", encoding="utf-8") as stream:
            data = json.load(stream)
    except (OSError, ValueError):
        return empty
    if not isinstance(data, dict):
        return empty
    notes = data.get("notes")
    labels = data.get("labels")
    return {
        "notes": notes if isinstance(notes, dict) else {},
        "labels": labels if isinstance(labels, dict) else {},
    }


def pick_translations(entry) -> dict:
    if not isinstance(entry, dict):
        return {}
    return {
        language: entry[language].strip()
        for language in MANUAL_FIX_TRANSLATION_LANGUAGES
        if isinstance(entry.get(language), str) and entry[language].strip()
    }


def attach_manual_fix_translations(games: list, translations: dict) -> dict:
    stats = {"translated": 0, "missing": 0}
    notes = translations.get("notes", {})
    labels = translations.get("labels", {})
    for game in games:
        for entry in (game.get("manualFixDetails") or {}).values():
            entry.pop("translations", None)
            found = pick_translations(notes.get(manual_fix_note_key(entry.get("note", ""))))
            if found:
                entry["translations"] = found
                stats["translated"] += 1
            elif entry.get("note"):
                stats["missing"] += 1
            for link in entry.get("links", []):
                link.pop("translations", None)
                label_found = pick_translations(labels.get(link.get("label")))
                if label_found:
                    link["translations"] = label_found
    return stats


MYMEMORY_API = "https://api.mymemory.translated.net/get"
MYMEMORY_LANGUAGES = {
    "es": "es", "fr": "fr", "ja": "ja", "ko": "ko",
    "pt": "pt", "ru": "ru", "vi": "vi", "zh": "zh-CN",
}
MYMEMORY_QUERY_LIMIT = 450
MANUAL_FIX_TRANSLATION_BATCH = 10
MANUAL_FIX_TRANSLATION_PAUSE = 0.4


class TranslationQuotaExceeded(RuntimeError):
    pass


def manual_fix_language_problem(source: str, text) -> str:
    if not isinstance(text, str) or not text.strip():
        return "is missing"
    if len(text) > len(source) * 3 + 60:
        return "is far longer than the source"
    if "\n" in text or "\r" in text:
        return "spans several lines"
    if "http" in text.casefold() and "http" not in source.casefold():
        return "adds a web address"
    for _, pattern in MANUAL_FIX_TOOLS:
        ascii_pattern = re.compile(
            pattern.pattern, (pattern.flags & ~re.UNICODE) | re.ASCII
        )
        if ascii_pattern.search(source) and not ascii_pattern.search(text):
            return "lost a tool name"
    return ""


def manual_fix_translation_problem(source: str, candidate) -> str:
    if not isinstance(candidate, dict):
        return "no translations returned"
    for language in MANUAL_FIX_TRANSLATION_LANGUAGES:
        problem = manual_fix_language_problem(source, candidate.get(language))
        if problem:
            return f"{language} {problem}"
    return ""


def pending_manual_fix_notes(games: list, translations: dict) -> dict:
    pending = {}
    known = translations.get("notes", {})
    for game in games:
        for entry in (game.get("manualFixDetails") or {}).values():
            note = entry.get("note", "")
            if not note:
                continue
            key = manual_fix_note_key(note)
            if key not in pending and manual_fix_translation_problem(note, known.get(key)):
                pending[key] = note
    return pending


def protect_manual_fix_names(text: str) -> tuple:
    names = {}

    def hide(match):
        token = f"Qzx{len(names) + 1}"
        names[token] = match.group(0)
        return token

    for _, pattern in MANUAL_FIX_TOOLS:
        text = pattern.sub(hide, text)
    return text, names


def restore_manual_fix_names(text: str, names: dict) -> str:
    for token, name in names.items():
        text = re.sub(rf"Qzx\s*{token[3:]}(?!\d)", lambda _: name, text)
    return text


def mymemory_translate(text: str, language: str) -> str:
    if len(text.encode("utf-8")) > MYMEMORY_QUERY_LIMIT:
        raise ValueError("note is too long for the free translation service")
    params = {"q": text, "langpair": f"en|{MYMEMORY_LANGUAGES[language]}"}
    email = os.environ.get("MANUAL_FIX_TRANSLATION_EMAIL")
    if email:
        params["de"] = email
    request = urllib.request.Request(
        f"{MYMEMORY_API}?{urllib.parse.urlencode(params)}",
        headers={"User-Agent": USER_AGENT},
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        data = json.loads(response.read().decode("utf-8"))
    status = str(data.get("responseStatus"))
    translated = (data.get("responseData") or {}).get("translatedText")
    if status == "429" or (isinstance(translated, str) and "MYMEMORY WARNING" in translated.upper()):
        raise TranslationQuotaExceeded("the free translation quota is used up for today")
    if status != "200" or not isinstance(translated, str):
        raise RuntimeError(f"the translation service answered {status}")
    return html.unescape(translated).strip()


def mymemory_translate_batch(
    items: list, translate=mymemory_translate, pause: float = MANUAL_FIX_TRANSLATION_PAUSE
) -> dict:
    result = {}
    for item in items:
        protected, names = protect_manual_fix_names(item["text"])
        translated = {}
        for language in MANUAL_FIX_TRANSLATION_LANGUAGES:
            try:
                translated[language] = restore_manual_fix_names(
                    translate(protected, language), names
                )
            except TranslationQuotaExceeded:
                raise
            except Exception as err:
                print(
                    f"[WARN] No {language} translation for note {item['id']}: {err}",
                    file=sys.stderr,
                )
            if pause:
                time.sleep(pause)
        if translated:
            result[item["id"]] = translated
    return result


def translate_missing_manual_fix_notes(
    games: list,
    translations: dict,
    translator=mymemory_translate_batch,
    batch_size: int = MANUAL_FIX_TRANSLATION_BATCH,
) -> dict:
    stats = {"pending": 0, "added": 0, "rejected": 0, "failed": 0}
    pending = list(pending_manual_fix_notes(games, translations).items())
    stats["pending"] = len(pending)
    for start in range(0, len(pending), batch_size):
        chunk = pending[start:start + batch_size]
        try:
            returned = translator([{"id": key, "text": source} for key, source in chunk])
        except Exception as err:
            print(f"[WARN] Manual-fix translation stopped: {err}", file=sys.stderr)
            stats["failed"] += len(pending) - start
            break
        for key, source in chunk:
            candidate = returned.get(key) if isinstance(returned, dict) else None
            kept = translations.setdefault("notes", {}).get(key, {})
            entry = {"source": source}
            for language in MANUAL_FIX_TRANSLATION_LANGUAGES:
                if not manual_fix_language_problem(source, kept.get(language)):
                    entry[language] = kept[language]
                    continue
                text = candidate.get(language) if isinstance(candidate, dict) else None
                problem = manual_fix_language_problem(source, text)
                if problem:
                    if isinstance(candidate, dict) and language in candidate:
                        print(
                            f"[WARN] Rejected {language} translation of note {key}: {problem}",
                            file=sys.stderr,
                        )
                        stats["rejected"] += 1
                    continue
                entry[language] = text.strip()
                stats["added"] += 1
            if len(entry) > 1:
                translations["notes"][key] = entry
    return stats


def save_manual_fix_translations(path: str, translations: dict) -> None:
    newline = "\n"
    try:
        with open(path, "rb") as stream:
            if b"\r\n" in stream.read():
                newline = "\r\n"
    except OSError:
        pass
    payload = {
        "version": 1,
        "notes": translations.get("notes", {}),
        "labels": translations.get("labels", {}),
    }
    text = json.dumps(payload, indent=2, ensure_ascii=False).replace("\n", newline)
    with open(path, "wb") as stream:
        stream.write(text.encode("utf-8"))


def manual_fix_features(game: dict) -> list:
    return [feature for feature in MANUAL_FIX_FEATURES if game.get(f"{feature}ManualFix")]


def enrich_with_manual_fix_details(games: list, cache: dict, fetch=fetch_pcgw_wikitext, pause: float = 0.4) -> dict:
    stats = {"fetched": 0, "cached": 0, "empty": 0}
    for game in games:
        features = manual_fix_features(game)
        if not features:
            game.pop("manualFixDetails", None)
            continue

        page_title = pcgw_page_title(game.get("manualFixUrl", ""))
        wikitext = fetch(page_title) if page_title else None
        if wikitext is None:
            previous = cache.get(game.get("normalized"), {}).get("manualFixDetails", {})
            details = {f: previous[f] for f in features if f in previous}
            if details:
                stats["cached"] += 1
        else:
            details = parse_manual_fix_details(wikitext, features)
            stats["fetched"] += 1
            if pause:
                time.sleep(pause)

        if details:
            game["manualFixDetails"] = details
        else:
            game.pop("manualFixDetails", None)
            stats["empty"] += 1
    return stats


def fetch_steam_icon_url(app_id: int) -> str:
    """Fetches the correct Steam library_capsule_2x icon URL via IStoreBrowseService API."""
    try:
        input_json = json.dumps({
            "ids": [{"appid": app_id}],
            "context": {"language": "english", "country_code": "US"},
            "data_request": {"include_assets": True}
        })
        api_url = f"https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json={urllib.parse.quote(input_json)}"
        req = urllib.request.Request(api_url, headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        store_items = data.get("response", {}).get("store_items", [])
        if not store_items:
            return None
        item = store_items[0]
        assets = item.get("assets", {})
        asset_url_format = assets.get("asset_url_format", "")
        asset_name = None
        if assets.get("library_capsule_2x"):
            asset_name = assets["library_capsule_2x"]
        elif assets.get("library_capsule"):
            asset_name = assets["library_capsule"]
        elif assets.get("header"):
            asset_name = assets["header"]
        if asset_name and asset_url_format:
            return "https://shared.akamai.steamstatic.com/store_item_assets/" + asset_url_format.replace("${FILENAME}", asset_name)
        return None
    except Exception:
        return None


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
            icon = fetch_steam_icon_url(app_id)
            if icon:
                return "verified", app_id, icon
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
    adaptive_games = fetch_pcgw_support(ADAPTIVE_PAGES)
    print(f"[+] Found {len(adaptive_games)} games with Adaptive Triggers.")

    print(f"[*] Fetching Haptic Feedback list from PCGamingWiki...")
    haptic_games = fetch_pcgw_support(HAPTIC_PAGES)
    print(f"[+] Found {len(haptic_games)} games with Haptic Feedback.")

    if not adaptive_games or not haptic_games:
        print(f"[ERROR] Incomplete fetch (Adaptive: {len(adaptive_games)}, Haptic: {len(haptic_games)}). Aborting to prevent data loss.", file=sys.stderr)
        sys.exit(1)

    games_dict = {}

    for title, support in adaptive_games.items():
        norm = normalize_title(title)
        if not norm:
            continue
        cached = existing_cache.get(norm, {})
        if norm not in games_dict:
            games_dict[norm] = {
                "title": title,
                "normalized": norm,
                "adaptiveTriggers": True,
                "adaptiveTriggersManualFix": support["requiresManualFix"],
                "hapticFeedback": False,
                "hapticFeedbackManualFix": False,
                "manualFixUrl": support["pcgwUrl"] if support["requiresManualFix"] else "",
                "profile": get_special_profile(norm),
                "steamAppId": cached.get("steamAppId", 0),
                "steamAppIdVerified": bool(cached.get("steamAppIdVerified", False)),
                "iconUrl": cached.get("iconUrl", ""),
                "executables": cached_executables(cached),
            }
        else:
            games_dict[norm]["adaptiveTriggers"] = True
            games_dict[norm]["adaptiveTriggersManualFix"] = support["requiresManualFix"]
            if support["requiresManualFix"]:
                games_dict[norm]["manualFixUrl"] = support["pcgwUrl"]

    for title, support in haptic_games.items():
        norm = normalize_title(title)
        if not norm:
            continue
        cached = existing_cache.get(norm, {})
        if norm not in games_dict:
            games_dict[norm] = {
                "title": title,
                "normalized": norm,
                "adaptiveTriggers": False,
                "adaptiveTriggersManualFix": False,
                "hapticFeedback": True,
                "hapticFeedbackManualFix": support["requiresManualFix"],
                "manualFixUrl": support["pcgwUrl"] if support["requiresManualFix"] else "",
                "profile": get_special_profile(norm),
                "steamAppId": cached.get("steamAppId", 0),
                "steamAppIdVerified": bool(cached.get("steamAppIdVerified", False)),
                "iconUrl": cached.get("iconUrl", ""),
                "executables": cached_executables(cached),
            }
        else:
            games_dict[norm]["hapticFeedback"] = True
            games_dict[norm]["hapticFeedbackManualFix"] = support["requiresManualFix"]
            if support["requiresManualFix"]:
                games_dict[norm]["manualFixUrl"] = support["pcgwUrl"]

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

    for game in games_dict.values():
        adaptive_manual_fix = bool(
            game.get("adaptiveTriggersManualFix", False)
        )
        haptic_manual_fix = bool(
            game.get("hapticFeedbackManualFix", False)
        )
        if adaptive_manual_fix:
            game["adaptiveTriggersManualFix"] = True
        else:
            game.pop("adaptiveTriggersManualFix", None)
        if haptic_manual_fix:
            game["hapticFeedbackManualFix"] = True
        else:
            game.pop("hapticFeedbackManualFix", None)

        if adaptive_manual_fix or haptic_manual_fix:
            game["requiresManualFix"] = True
        else:
            game.pop("requiresManualFix", None)
            game.pop("manualFixUrl", None)

    print("[*] Fetching manual-fix notes from PCGamingWiki game pages...")
    fix_stats = enrich_with_manual_fix_details(list(games_dict.values()), existing_cache)
    print(
        "[+] Manual-fix notes: "
        f"{fix_stats['fetched']} pages read, {fix_stats['cached']} kept from cache, "
        f"{fix_stats['empty']} without usable note."
    )
    translations_path = os.path.join(root_dir, "data", "manual_fix_translations.json")
    translations = load_manual_fix_translations(translations_path)
    pending_notes = pending_manual_fix_notes(list(games_dict.values()), translations)
    if pending_notes:
        print(f"[*] Translating {len(pending_notes)} new or changed manual-fix notes...")
        try:
            auto_stats = translate_missing_manual_fix_notes(
                list(games_dict.values()), translations
            )
            if auto_stats["added"]:
                save_manual_fix_translations(translations_path, translations)
            print(
                "[+] Automatic translation: "
                f"{auto_stats['added']} languages added, {auto_stats['rejected']} rejected, "
                f"{auto_stats['failed']} notes postponed (shown in English meanwhile)."
            )
        except Exception as err:
            print(f"[WARN] Automatic translation skipped: {err}", file=sys.stderr)
    translation_stats = attach_manual_fix_translations(
        list(games_dict.values()), translations
    )
    print(
        "[+] Manual-fix translations: "
        f"{translation_stats['translated']} notes translated, "
        f"{translation_stats['missing']} shown in English."
    )

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

    # Resolve missing icons and repair broken URLs (issue #37)
    need_icon_resolve = [
        g for g in games_dict.values()
        if g.get("steamAppIdVerified", False) and g.get("steamAppId", 0) > 0
        and (not g.get("iconUrl") or "library_600x900.jpg" in g.get("iconUrl", ""))
    ]
    if need_icon_resolve:
        print(f"[*] Resolving/repairing {len(need_icon_resolve)} icon URLs...")
        fixed_count = 0
        for g in need_icon_resolve:
            icon_url = fetch_steam_icon_url(g["steamAppId"])
            if icon_url:
                g["iconUrl"] = icon_url
                fixed_count += 1
            # Slight delay to respect Steam rate limits
            time.sleep(0.05)
        print(f"[+] Icon resolution: {fixed_count} URLs resolved/repaired.")

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

    new_games = assign_added_dates(
        output_list, existing_cache, datetime.now(timezone.utc).date().isoformat()
    )
    print(f"[+] {new_games} newly added games dated today.")

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
