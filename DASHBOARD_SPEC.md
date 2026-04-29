# Stellaris Save Dashboard — Spec for Codex

This document describes a Streamlit dashboard reading JSON files produced by
an external C++ extractor. Hand this to Codex along with one example JSON
output from the extractor.

## Pipeline overview

```
.sav file (zip)
  └─ gamestate (Paradox key=value format, ~50 MiB decompressed)
       └─ stellaris_extract.exe → output/2347.11.08.json (already built, working)
            └─ localize.py → output/2347.11.08.resolved.json (TODO)
                 └─ Streamlit dashboard (this project)
```

The PowerShell script `run-extract.ps1` already handles step 1→2. Codex can
assume `output/*.json` files exist.

## Input

**Location**: `./output/*.json`. Filenames are in-game dates (`2347.11.08.json`)
and sort chronologically. Ignore `.parsed-manifest.json`.

**Top-level shape** of each JSON:

```json
{
  "meta":   { "version", "save_name", "date", "version_control_revision" },
  "player": {
    "name": "string",
    "country": { "id": 0, "name": "...", "type": "default", "government": "...",
                 "authority": "...", "ethics": [...], "civics": [...], ... }
  },
  "country": { /* FULL player country block, raw passthrough */ },
  "planet_id_sources": {
    "from_country_owned_planets": [int, ...],
    "from_owner_field":           [int, ...]
  },
  "planets": {
    "<id>": { /* full planet block, with cross-references resolved inline */ }
  },
  "galactic_context": {
    "countries": {
      "<id>": { /* light record: name, type, government, ethics, civics,
                  capital_planet, military/economy power, pops, fleet/empire size */ }
    }
  }
}
```

## Resolved fields inside each planet

These are the fields where the extractor has already replaced raw integer IDs
with inline objects so the dashboard doesn't need to do joins:

| Field              | Was   | Now is (each element)                                         |
|--------------------|-------|---------------------------------------------------------------|
| `pop_groups`       | `[int]` | `[{id, species_id, job_id, category, size, enslaved}]`        |
| `pop_jobs`         | `[int]` | same shape as pop                                             |
| `buildings_cache`  | `[int]` | `[{id, type, level, ruined, owner}]`                          |
| `districts`        | `[int]` | `[{id, type}]`                                                |
| `deposits`         | `[int]` | `[{id, type}]`                                                |
| `species_refs`     | `[int]` | `[{id, name, plural, class, portrait, traits}]`               |
| `species_information` | `{<species_id>: {num_pops}}` | same keys, values gain `name` and `class` fields |
| `owner`            | `int`   | `{id, name, type, government, authority, ethics, civics, ...}` |
| `controller`       | `int`   | same                                                          |
| `original_owner`   | `int`   | same                                                          |
| `governor`         | `int`   | `{id, name, class, level, species_id, age, paragon, traits}`  |

Fields not in the table above are passed through raw (still as integer IDs
or whatever the source format produced).

## Critical data shape gotchas

1. **Duplicate keys in source → parallel arrays in JSON.**
   `tech_status={ technology="A" level=1 technology="B" level=2 }` becomes
   `{"technology": ["A", "B"], "level": [1, 2]}`. The Nth technology pairs
   with the Nth level. Use `zip()` to recombine.

2. **Mixed blocks emit `_items`.** When a Paradox block has both named keys
   and anonymous list entries (e.g. `timed_modifier={ items={ {modifier="..."} {modifier="..."} } }`),
   the named keys appear normally and the anonymous entries appear under
   `"_items"` as an array. Same for any block where one key appears multiple
   times alongside other named keys.

3. **Localized name objects.** Name fields are commonly objects like
   `{"key": "Xeta", "literal": true}` (display the key) or
   `{"key": "%ADJECTIVE%", "variables": [...]}` (template — for now, just
   display the key). Helper:
   ```python
   def display_name(field, fallback=""):
       if isinstance(field, dict):
           return field.get("key", fallback)
       return field if field else fallback
   ```

4. **Type strings are unlocalized.** Expect `building_energy_nexus`,
   `tech_psionic_theory`, `civic_slaver_guilds`, `pc_tropical`, etc. A
   pluggable localization layer (see below) will inject English. Until that
   exists, fall back to title-casing with prefix removed:
   `building_energy_nexus` → `Energy Nexus`.

5. **Empty blocks** appear as `{}` (object, even if conceptually a list).
   Don't crash on these.

6. **Date format**: `"YYYY.MM.DD"` strings. `"0.01.01"` is the sentinel for
   "never" — display as "—" or "Never".

## Localization layer (pluggable)

Stellaris ships YAML-flavored files at:
```
<install>/localisation/english/*.yml
```

Format is roughly:
```yaml
l_english:
 building_energy_nexus:0 "Energy Nexus"
 tech_psionic_theory:0 "Psionic Theory"
```

Note: the leading space, the `:0` version suffix, and the quoted value are
specific to Paradox; standard YAML parsers won't handle this. Write a
custom regex-based reader.

Build a Python module `lib/localize.py` with:
```python
class Localizer:
    def __init__(self, locale_dir: pathlib.Path | None): ...
    def lookup(self, key: str) -> str | None: ...
    def localize_or_fallback(self, key: str) -> str:
        # if lookup returns None, strip prefix & title-case
```

The dashboard calls `localize_or_fallback()` everywhere a type string is
displayed. If `locale_dir` is None or the key isn't found, fallback gracefully.
Make it cacheable across reruns.

## Pages

Use Streamlit's multi-page layout: `Home.py` + `pages/`. Cache JSON loads
with `@st.cache_data`.

### Home / Overview
Empire snapshot for the most recent save. Top section: name, in-game date,
ethics, civics, government, ascension perks, traditions count, victory rank.
Big-number cards: military power, economy power, tech power, fleet size,
sapient pops, owned planets, controlled planets, starbases.

A save selector in the sidebar lets the user pick which JSON to view (default:
most recent). All other pages read from this selection.

### Economy
Reproduce the in-game economy panel. Source: `country.budget.current_month.balance`
is a dict of category → `{resource: signed_amount}`. Build a DataFrame with
categories as rows and resources as columns. Render with a category total
row and a resource total row matching the in-game "Net Monthly Gain". Also
show stockpiles from `country.modules.standard_economy_module.resources`.

### Planets
Sortable table: id, name, class, size, num_sapient_pops, stability, crime,
designation, free_housing, total_housing, top-3 net production resources.
Click row → drill-down with:
- districts (table: type, count if duplicates)
- buildings (table: type, level, ruined)
- pop composition by species (`species_information`)
- governor (name, traits, level)
- timed modifiers
- production / upkeep / profits tables

### Diplomacy
List from `country.relations_manager.relation[]`. Each entry has a `country`
ID and fields like `relation_current`, `trust`, `subject`, `commercial_pact`,
`migration_access`, `truce`, `hostile`, `borders`, `closed_borders`. Cross-
reference `galactic_context.countries[<id>]` for country names. Subject
relationships and federations highlighted.

### Tech
From `country.tech_status`:
- `technology` and `level` parallel arrays → completed techs (count by area)
- `physics_queue`, `society_queue`, `engineering_queue` → currently researching
- `alternatives.{physics,society,engineering}` → next pool to roll from

### Timeline
From `country.timeline_events` — array of `{date, definition, data}` objects.
Render as a chronological list, grouped by year. Filter by event type.

## Multi-save mode

When 2+ JSONs in `output/`, expose a "Timeline mode" toggle. Plot:
- Military / economy / tech power across saves (line chart, x = in-game date)
- Sapient pops over time
- Owned planets over time
- Stockpiles (energy, minerals, alloys, food) over time

Use Plotly for plots; pandas for shaping.

## Stack

- Python 3.11+
- streamlit, pandas, plotly
- No database; all state is JSON files in `output/`
- One pip-installable: `pip install streamlit pandas plotly`

## Project layout

```
dashboard/
├── Home.py
├── pages/
│   ├── 1_Economy.py
│   ├── 2_Planets.py
│   ├── 3_Diplomacy.py
│   ├── 4_Tech.py
│   └── 5_Timeline.py
├── lib/
│   ├── __init__.py
│   ├── load.py        # JSON loader, save selector helpers
│   ├── localize.py    # Stellaris YAML loader + lookup
│   └── format.py      # display_name(), date formatter, etc.
├── requirements.txt
└── README.md
```

Run with `streamlit run Home.py`.
