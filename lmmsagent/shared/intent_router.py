#!/usr/bin/env python3
import re


ALIASES = {
    "open mixer": "show mixer",
    "open the mixer": "show mixer",
    "open piano roll": "show piano roll",
    "open song editor": "show song editor",
    "open automation editor": "show automation editor",
    "open controller rack": "show controller rack",
    "launch slicer": "open slicer",
    "load slicer": "open slicer",
    "add kick": "add 808",
}


def normalize_command(text: str) -> str:
    cleaned = re.sub(r"\s+", " ", text.strip().lower())
    if not cleaned:
        return cleaned
    if cleaned in ALIASES:
        return ALIASES[cleaned]
    if cleaned.startswith("import from downloads "):
        return "import " + cleaned.removeprefix("import from downloads ").strip()
    if cleaned.startswith("open plugin "):
        plugin = cleaned.removeprefix("open plugin ").strip()
        if plugin == "slicer":
            return "open slicer"
        return f"new instrument {plugin}"
    return cleaned
