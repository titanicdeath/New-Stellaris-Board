from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import pandas as pd
import streamlit as st

_DASHBOARD_DIR = Path(__file__).resolve().parents[1]
if str(_DASHBOARD_DIR) not in sys.path:
    sys.path.insert(0, str(_DASHBOARD_DIR))

from lib.load import load_save, render_save_selector
from lib.localize import localize_or_fallback


def _as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _balance_to_dataframe(balance: dict[str, Any]) -> pd.DataFrame:
    all_resources: set[str] = set()
    normalized: dict[str, dict[str, float]] = {}

    for category, resource_map in balance.items():
        if not isinstance(resource_map, dict):
            continue
        normalized[category] = {}
        for resource, amount in resource_map.items():
            if isinstance(amount, (int, float)):
                normalized[category][resource] = float(amount)
                all_resources.add(resource)

    if not normalized:
        return pd.DataFrame()

    resources = sorted(all_resources)
    rows = []
    for category, resource_map in normalized.items():
        row = {"Category": localize_or_fallback(category)}
        for resource in resources:
            row[localize_or_fallback(resource)] = resource_map.get(resource, 0.0)
        rows.append(row)

    df = pd.DataFrame(rows).set_index("Category")
    df.loc["Category Total"] = df.sum(axis=0)
    df["Row Total"] = df.sum(axis=1)
    return df


def _stockpiles_dataframe(resources_block: dict[str, Any]) -> pd.DataFrame:
    rows = []
    for resource, amount in resources_block.items():
        rows.append({"Resource": localize_or_fallback(resource), "Stockpile": amount})
    return pd.DataFrame(rows)


st.set_page_config(page_title="Economy", layout="wide")
st.title("Economy")

selected_path = render_save_selector()
if selected_path is None:
    st.stop()

data = load_save(selected_path)
country = _as_dict(data.get("country"))
budget = _as_dict(country.get("budget"))
current_month = _as_dict(budget.get("current_month"))
balance = _as_dict(current_month.get("balance"))

st.subheader("Monthly Balance")
if not balance:
    st.info("No monthly balance data found at country.budget.current_month.balance")
else:
    econ_df = _balance_to_dataframe(balance)
    if econ_df.empty:
        st.info("Monthly balance block is present but contained no numeric resource values.")
    else:
        st.dataframe(econ_df, use_container_width=True)

st.subheader("Stockpiles")
modules = _as_dict(country.get("modules"))
standard_economy_module = _as_dict(modules.get("standard_economy_module"))
resources_block = _as_dict(standard_economy_module.get("resources"))
if not resources_block:
    st.info("No stockpile data found at country.modules.standard_economy_module.resources")
else:
    st.dataframe(_stockpiles_dataframe(resources_block), use_container_width=True, hide_index=True)
