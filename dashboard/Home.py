from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import streamlit as st

# Support both:
#   1) streamlit run dashboard/Home.py  (repo root cwd)
#   2) cd dashboard && streamlit run Home.py
_DASHBOARD_DIR = Path(__file__).resolve().parent
if str(_DASHBOARD_DIR) not in sys.path:
    sys.path.insert(0, str(_DASHBOARD_DIR))

from lib.format import display_name
from lib.load import load_save, render_save_selector
from lib.localize import localize_or_fallback


def _as_list(value: Any) -> list[Any]:
    if isinstance(value, list):
        return value
    if value in (None, "", {}):
        return []
    return [value]


def _render_labeled_values(title: str, values: list[Any], empty_label: str = "—") -> None:
    st.markdown(f"**{title}**")
    if not values:
        st.write(empty_label)
        return

    labels: list[str] = []
    for value in values:
        if isinstance(value, dict):
            maybe_key = value.get("key") or value.get("name") or ""
            labels.append(localize_or_fallback(maybe_key) if maybe_key else empty_label)
        else:
            labels.append(localize_or_fallback(str(value)))

    rendered = [label for label in labels if label and label != empty_label]
    st.write(", ".join(rendered) if rendered else empty_label)


def _render_metrics_grid(stats: list[tuple[str, Any]], per_row: int = 4) -> None:
    for i in range(0, len(stats), per_row):
        row = stats[i : i + per_row]
        cols = st.columns(per_row)
        for j, (label, value) in enumerate(row):
            display_value = "—" if value in (None, "") else value
            cols[j].metric(label, display_value)


st.set_page_config(page_title="Stellaris Save Dashboard", layout="wide")
st.title("Stellaris Save Dashboard")

selected_path = render_save_selector()
if selected_path is None:
    st.stop()

save_data = load_save(selected_path)
meta = save_data.get("meta", {})
player = save_data.get("player", {})
country = save_data.get("country", {}) or {}
player_country = player.get("country", {}) if isinstance(player, dict) else {}

st.caption(f"Viewing: {selected_path.name}")

# Prefer the full country block; fallback to player.country.
source_country = country if country else player_country

empire_name = display_name(source_country.get("name") or player.get("name") or "Unknown Empire")
in_game_date = meta.get("date", "—")

st.header("Overview")
header_left, header_right = st.columns([2, 1])
with header_left:
    st.subheader(empire_name)
    st.write(f"In-game date: `{in_game_date}`")
with header_right:
    st.metric("Save name", meta.get("save_name", "—"))

government = source_country.get("government", {}) if isinstance(source_country, dict) else {}

gov_type = localize_or_fallback(government.get("type", "")) if isinstance(government, dict) else "—"
authority = localize_or_fallback(government.get("authority", "")) if isinstance(government, dict) else "—"

info_col1, info_col2 = st.columns(2)
with info_col1:
    st.markdown("**Government**")
    st.write(gov_type if gov_type else "—")

    st.markdown("**Authority**")
    st.write(authority if authority else "—")

    _render_labeled_values("Ethics", _as_list(source_country.get("ethics")))
    _render_labeled_values("Civics", _as_list(government.get("civics", []) if isinstance(government, dict) else []))

with info_col2:
    perks = _as_list(source_country.get("ascension_perks"))
    traditions = _as_list(source_country.get("traditions"))

    _render_labeled_values("Ascension Perks", perks)
    st.markdown("**Traditions Unlocked**")
    st.write(len(traditions))

    victory_rank = source_country.get("victory_rank", "—")
    st.markdown("**Victory Rank**")
    st.write(victory_rank if victory_rank not in (None, "") else "—")

stats = [
    ("Military Power", source_country.get("military_power")),
    ("Economy Power", source_country.get("economy_power")),
    ("Tech Power", source_country.get("tech_power")),
    ("Fleet Size", source_country.get("fleet_size")),
    ("Sapient Pops", source_country.get("num_sapient_pops")),
    ("Owned Planets", len(_as_list(source_country.get("owned_planets")))),
    ("Controlled Planets", len(_as_list(source_country.get("controlled_planets")))),
    ("Starbases", source_country.get("num_upgraded_starbase")),
    ("Starbase Capacity", source_country.get("starbase_capacity")),
]

st.markdown("### Key Metrics")
_render_metrics_grid(stats, per_row=4)
