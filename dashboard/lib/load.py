from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import streamlit as st

OUTPUT_DIR = Path("output")
SAMPLE_OUTPUT_DIR = Path("sample_output")


def _json_files_in(path: Path) -> list[Path]:
    if not path.exists() or not path.is_dir():
        return []
    return sorted(
        [p for p in path.glob("*.json") if p.name != ".parsed-manifest.json"],
        key=lambda p: p.name,
    )


def list_save_files() -> list[Path]:
    output_files = _json_files_in(OUTPUT_DIR)
    if output_files:
        return output_files
    return _json_files_in(SAMPLE_OUTPUT_DIR)


@st.cache_data(show_spinner=False)
def load_save(path: str | Path) -> dict[str, Any]:
    file_path = Path(path)
    with file_path.open("r", encoding="utf-8") as f:
        return json.load(f)


def render_save_selector() -> Path | None:
    save_files = list_save_files()
    if not save_files:
        st.sidebar.warning("No save JSON files found in output/ or sample_output/.")
        return None

    labels = [p.name for p in save_files]
    default_index = len(save_files) - 1
    selected_name = st.sidebar.selectbox("Save file", labels, index=default_index)
    return next(p for p in save_files if p.name == selected_name)
