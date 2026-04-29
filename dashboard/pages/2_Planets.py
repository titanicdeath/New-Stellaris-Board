from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path
from typing import Any

import pandas as pd
import streamlit as st

_DASHBOARD_DIR = Path(__file__).resolve().parents[1]
if str(_DASHBOARD_DIR) not in sys.path:
    sys.path.insert(0, str(_DASHBOARD_DIR))

from lib.format import display_name
from lib.load import load_save, render_save_selector
from lib.localize import localize_or_fallback


def _as_list(value: Any) -> list[Any]:
    if isinstance(value, list):
        return value
    if value in (None, {}, ""):
        return []
    return [value]


def _safe_num(value: Any) -> float | int | None:
    return value if isinstance(value, (int, float)) else None


def _planet_rows(planets: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for planet_id, planet in planets.items():
        name = display_name(planet.get("name"), fallback=f"Planet {planet_id}")
        pclass = localize_or_fallback(planet.get("planet_class", ""))
        designation = localize_or_fallback(planet.get("designation", "")) if planet.get("designation") else "—"
        rows.append(
            {
                "id": str(planet_id),
                "name": name,
                "class": pclass,
                "size": planet.get("planet_size", "—"),
                "pops": planet.get("num_sapient_pops", "—"),
                "stability": planet.get("stability", "—"),
                "crime": planet.get("crime", "—"),
                "designation": designation,
            }
        )
    return rows


def _count_types(items: list[Any], key: str) -> pd.DataFrame:
    counts = Counter()
    for item in items:
        if isinstance(item, dict):
            t = localize_or_fallback(item.get(key, "")) if item.get(key) else "—"
        else:
            t = "—"
        counts[t] += 1
    df = pd.DataFrame(sorted(counts.items()), columns=["type", "count"])
    return df


def _render_buildings(planet: dict[str, Any]) -> None:
    buildings = _as_list(planet.get("buildings_cache"))
    if not buildings:
        st.write("—")
        return
    rows = []
    for b in buildings:
        rows.append(
            {
                "id": b.get("id", "—") if isinstance(b, dict) else "—",
                "type": localize_or_fallback(b.get("type", "")) if isinstance(b, dict) else "—",
                "level": b.get("level", "—") if isinstance(b, dict) else "—",
                "ruined": b.get("ruined", "—") if isinstance(b, dict) else "—",
            }
        )
    st.dataframe(pd.DataFrame(rows), use_container_width=True, hide_index=True)


def _render_species_info(planet: dict[str, Any]) -> None:
    species_info = planet.get("species_information", {})
    if not isinstance(species_info, dict) or not species_info:
        st.write("—")
        return
    rows = []
    for species_id, data in species_info.items():
        if not isinstance(data, dict):
            continue
        rows.append(
            {
                "species_id": species_id,
                "name": display_name(data.get("name"), fallback="—"),
                "class": localize_or_fallback(data.get("class", "")) if data.get("class") else "—",
                "num_pops": data.get("num_pops", "—"),
            }
        )
    st.dataframe(pd.DataFrame(rows), use_container_width=True, hide_index=True)


def _render_governor(planet: dict[str, Any]) -> None:
    gov = planet.get("governor", {})
    if not isinstance(gov, dict) or not gov:
        st.write("—")
        return
    st.write(
        {
            "name": display_name(gov.get("name"), fallback="—"),
            "class": localize_or_fallback(gov.get("class", "")) if gov.get("class") else "—",
            "level": gov.get("level", "—"),
            "traits": gov.get("traits", "—"),
        }
    )


def _render_simple_table(title: str, value: Any, type_key: str) -> None:
    st.markdown(f"#### {title}")
    items = _as_list(value)
    if not items:
        st.write("—")
        return
    df = _count_types(items, type_key)
    st.dataframe(df, use_container_width=True, hide_index=True)


def _render_resource_table(title: str, value: Any) -> None:
    st.markdown(f"#### {title}")
    if not isinstance(value, dict) or not value:
        st.write("—")
        return
    rows = [{"resource": localize_or_fallback(k), "amount": v} for k, v in value.items()]
    st.dataframe(pd.DataFrame(rows), use_container_width=True, hide_index=True)


st.set_page_config(page_title="Planets", layout="wide")
st.title("Planets")

selected_path = render_save_selector()
if selected_path is None:
    st.stop()

data = load_save(selected_path)
planets = data.get("planets", {})
if not isinstance(planets, dict) or not planets:
    st.info("No planets found in this save.")
    st.stop()

summary_rows = _planet_rows(planets)
summary_df = pd.DataFrame(summary_rows)

st.subheader("Planet Summary")
st.dataframe(summary_df, use_container_width=True, hide_index=True)

options = [f"{row['name']} (ID {row['id']})" for row in summary_rows]
selected_label = st.selectbox("Planet", options)
selected_id = selected_label.split("ID ")[-1].rstrip(")")
planet = planets.get(selected_id, {})

st.markdown("---")
st.subheader(display_name(planet.get("name"), fallback=f"Planet {selected_id}"))

col1, col2 = st.columns(2)
with col1:
    st.markdown("#### Basics")
    st.write(
        {
            "class": localize_or_fallback(planet.get("planet_class", "")) if planet.get("planet_class") else "—",
            "size": planet.get("planet_size", "—"),
            "designation": localize_or_fallback(planet.get("designation", "")) if planet.get("designation") else "—",
            "owner": display_name((planet.get("owner") or {}).get("name"), fallback="—") if isinstance(planet.get("owner"), dict) else "—",
            "controller": display_name((planet.get("controller") or {}).get("name"), fallback="—") if isinstance(planet.get("controller"), dict) else "—",
            "original_owner": display_name((planet.get("original_owner") or {}).get("name"), fallback="—") if isinstance(planet.get("original_owner"), dict) else "—",
        }
    )
with col2:
    _render_governor(planet)

_render_simple_table("Districts", planet.get("districts"), "type")
_render_buildings(planet)
st.markdown("#### Deposits")
_render_simple_table("", planet.get("deposits"), "type")

st.markdown("#### Pops by Species")
_render_species_info(planet)

st.markdown("#### Pop Groups")
pop_groups = _as_list(planet.get("pop_groups"))
if pop_groups:
    st.caption("Pop-group objects are present but may have unresolved/empty fields from extractor output.")
    st.dataframe(pd.DataFrame(pop_groups), use_container_width=True, hide_index=True)
else:
    st.write("—")

st.markdown("#### Timed Modifiers")
modifiers = planet.get("timed_modifiers")
if isinstance(modifiers, dict) and modifiers.get("_items"):
    st.dataframe(pd.DataFrame(_as_list(modifiers.get("_items"))), use_container_width=True, hide_index=True)
elif isinstance(modifiers, list):
    st.dataframe(pd.DataFrame(modifiers), use_container_width=True, hide_index=True)
else:
    st.write("—")

_resource_table_inputs = [
    ("Production", planet.get("production")),
    ("Upkeep", planet.get("upkeep")),
    ("Profits", planet.get("profits")),
]
for title, block in _resource_table_inputs:
    _render_resource_table(title, block)
