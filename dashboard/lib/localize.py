from __future__ import annotations

import re
from pathlib import Path


class Localizer:
    def __init__(self, locale_dir: Path | None):
        self.locale_dir = locale_dir

    def lookup(self, key: str) -> str | None:
        # Stub for now; YAML parsing added in a later step.
        return None

    def localize_or_fallback(self, key: str) -> str:
        found = self.lookup(key)
        if found:
            return found
        return _fallback_type_label(key)


def _fallback_type_label(key: str) -> str:
    if not key:
        return "—"
    cleaned = str(key)
    cleaned = re.sub(r"^[a-z]+_", "", cleaned)
    return cleaned.replace("_", " ").title() if cleaned else "—"


def localize_or_fallback(key: str, localizer: Localizer | None = None) -> str:
    if localizer is None:
        return _fallback_type_label(key)
    return localizer.localize_or_fallback(key)
