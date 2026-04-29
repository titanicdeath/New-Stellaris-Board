# Stellaris Streamlit Dashboard

## Install

```bash
pip install -r requirements.txt
```

## Run

From repo root:

```bash
streamlit run dashboard/Home.py
```

Or from the `dashboard/` directory:

```bash
cd dashboard
streamlit run Home.py
```

## Data location

The dashboard expects extractor JSON save files in `output/*.json` (with fallback to `sample_output/*.json` if `output/` is missing or empty).
