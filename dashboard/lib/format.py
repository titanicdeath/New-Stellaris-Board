from __future__ import annotations

from typing import Any


def display_name(field: Any, fallback: str = "") -> str:
    if isinstance(field, dict):
        return str(field.get("key", fallback))
    if field is None:
        return fallback
    text = str(field)
    return text if text else fallback
