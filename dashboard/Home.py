from __future__ import annotations

import streamlit as st

from lib.load import load_save, render_save_selector

st.set_page_config(page_title="Stellaris Save Dashboard", layout="wide")
st.title("Stellaris Save Dashboard")

selected_path = render_save_selector()
if selected_path is None:
    st.stop()

save_data = load_save(selected_path)

st.subheader("Selected save")
st.write({
    "file": selected_path.name,
    "meta": save_data.get("meta", {}),
})

st.info("Overview page implementation is next.")
