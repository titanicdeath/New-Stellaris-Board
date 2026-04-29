// resolver.cpp
// See resolver.h for design notes.
//
// The build() function walks each top-level block once and indexes by ID.
// We lean on the fact that these blocks are all keyed by integer IDs at
// their top level — pop={ 1054={...} 1055={...} }, buildings={ ... },
// district={ ... } — same pattern as planets.

#include "resolver.h"

#include <stdexcept>

namespace resolve {

namespace {

// Helper: read a name-block (e.g. {key="Foo" literal=yes}) into a string.
// We prefer the key field; if absent, fall back to whatever scalar is there.
std::string read_name(const pdx::Node* n) {
    if (!n) return {};
    if (n->kind == pdx::NodeKind::Scalar) return n->scalar;
    return n->child_string("key");
}

// Helper: walk a block keyed by integer IDs and call `per_entry(id, child)`
// for each. Skips malformed keys.
template <typename Fn>
void for_each_id_keyed(const pdx::Node& parent, Fn&& fn) {
    for (const auto& e : parent.entries) {
        if (e.key.empty() || !e.value) continue;
        if (e.value->kind != pdx::NodeKind::Block) continue;
        try {
            long long id = std::stoll(e.key);
            fn(id, *e.value);
        } catch (...) { /* non-numeric key, skip */ }
    }
}

// Read a primitive-string list block into a vector.
std::vector<std::string> read_string_list(const pdx::Node* n) {
    std::vector<std::string> out;
    if (!n || n->kind != pdx::NodeKind::Block) return out;
    for (const auto& e : n->entries) {
        if (!e.key.empty() || !e.value) continue;
        if (e.value->kind == pdx::NodeKind::Scalar) {
            out.push_back(e.value->scalar);
        }
    }
    return out;
}

// ---------- per-block builders ----------

void build_species(const pdx::Node& root, Tables& t) {
    auto* sdb = root.find("species_db");
    if (!sdb) return;
    // species_db is keyed by integer indices; same pattern as country/planet.
    for_each_id_keyed(*sdb, [&](long long id, const pdx::Node& sp) {
        SpeciesRec r;
        r.name        = sp.child_string("name");
        r.plural      = sp.child_string("plural");
        r.class_      = sp.child_string("class");
        r.portrait    = sp.child_string("portrait");
        r.home_planet = sp.child_int("home_planet", -1);
        r.base_ref    = sp.child_int("base_ref", -1);
        if (auto* tr = sp.find("traits")) {
            r.traits = read_string_list(tr);
        }
        t.species[id] = std::move(r);
    });
}

void build_leaders(const pdx::Node& root, Tables& t) {
    auto* leaders = root.find("leaders");
    if (!leaders) return;
    for_each_id_keyed(*leaders, [&](long long id, const pdx::Node& l) {
        LeaderRec r;
        // Leader names are often nested {first_name={key=...} last_name={key=...}}
        // but sometimes flat strings. Handle both.
        if (auto* fn = l.find("first_name")) r.first_name = read_name(fn);
        if (auto* ln = l.find("last_name"))  r.last_name  = read_name(ln);
        r.class_   = l.child_string("class");
        r.skill    = (int)l.child_int("level", l.child_int("skill", 0));
        r.gender   = l.child_string("gender");
        r.species_id = l.child_int("species", -1);
        r.age      = l.child_int("age", 0);
        r.paragon  = l.child_yesno("is_paragon", false);
        if (auto* tr = l.find("traits")) {
            r.traits = read_string_list(tr);
        }
        t.leaders[id] = std::move(r);
    });
}

void build_pops(const pdx::Node& root, Tables& t) {
    // The single-pop block is variously called "pop" (older) or part of
    // pop_groups. We index whatever `pop` block exists at top level, which
    // is the per-pop record source as of recent Stellaris versions.
    auto* pop = root.find("pop");
    if (!pop) return;
    for_each_id_keyed(*pop, [&](long long id, const pdx::Node& p) {
        PopRec r;
        r.species_id = p.child_int("species_index", -1);
        r.job_id     = p.child_int("job", -1);
        r.category   = p.child_string("category");
        r.ethos      = p.child_string("ethos");
        r.planet_id  = p.child_int("planet", -1);
        r.size       = (int)p.child_int("size", 1);
        r.enslaved   = p.child_yesno("enslaved", false);
        t.pops[id] = std::move(r);
    });
}

void build_buildings(const pdx::Node& root, Tables& t) {
    auto* b = root.find("buildings");
    if (!b) return;
    for_each_id_keyed(*b, [&](long long id, const pdx::Node& bd) {
        BuildingRec r;
        r.type      = bd.child_string("type");
        r.planet_id = bd.child_int("planet", -1);
        r.level     = (int)bd.child_int("level", 1);
        r.ruined    = bd.child_yesno("ruined", false);
        r.owner     = bd.child_int("owner", -1);
        t.buildings[id] = std::move(r);
    });
}

void build_districts(const pdx::Node& root, Tables& t) {
    // Stellaris uses both "district" (singular) and sometimes other names
    // depending on version. We only know the top-level block from inspection.
    auto* d = root.find("district");
    if (!d) return;
    for_each_id_keyed(*d, [&](long long id, const pdx::Node& dd) {
        DistrictRec r;
        r.type      = dd.child_string("type");
        r.planet_id = dd.child_int("planet", -1);
        t.districts[id] = std::move(r);
    });
}

void build_deposits(const pdx::Node& root, Tables& t) {
    auto* d = root.find("deposit");
    if (!d) return;
    for_each_id_keyed(*d, [&](long long id, const pdx::Node& dd) {
        DepositRec r;
        r.type      = dd.child_string("type");
        r.planet_id = dd.child_int("planet", -1);
        t.deposits[id] = std::move(r);
    });
}

void build_countries(const pdx::Node& root, Tables& t) {
    auto* c = root.find("country");
    if (!c) return;
    for_each_id_keyed(*c, [&](long long id, const pdx::Node& cc) {
        CountryRec r;
        r.name = read_name(cc.find("name"));
        r.type = cc.child_string("type");
        if (auto* g = cc.find("government")) {
            r.government_type = g->child_string("type");
            r.authority       = g->child_string("authority");
            if (auto* civics = g->find("civics")) {
                r.civics = read_string_list(civics);
            }
        }
        if (auto* e = cc.find("ethos")) {
            // ethos block uses repeated `ethic="..."` entries
            for (auto* en : e->find_all("ethic")) {
                if (en && en->kind == pdx::NodeKind::Scalar) {
                    r.ethics.push_back(en->scalar);
                }
            }
        }
        r.capital_planet = cc.child_int("capital", -1);
        r.founder_species_ref = cc.child_int("founder_species_ref", -1);
        r.military_power = cc.child_double("military_power", 0);
        r.economy_power  = cc.child_double("economy_power", 0);
        r.num_sapient_pops = cc.child_int("num_sapient_pops", 0);
        r.fleet_size     = cc.child_int("fleet_size", 0);
        r.empire_size    = cc.child_int("empire_size", 0);
        t.countries[id] = std::move(r);
    });
}

} // namespace

Tables build(const pdx::Node& root) {
    Tables t;
    build_species(root, t);
    build_leaders(root, t);
    build_pops(root, t);
    build_buildings(root, t);
    build_districts(root, t);
    build_deposits(root, t);
    build_countries(root, t);
    return t;
}

// ---------- emitters ----------
//
// Pattern for each: write `{id: N}` always; if found in the table, also
// emit the resolved fields. This keeps the JSON shape predictable for the
// dashboard (every reference is always an object, never bare).

void emit_species_ref(jw::Writer& w, long long id, const Tables& t) {
    w.begin_object();
    w.kv("id", id);
    auto it = t.species.find(id);
    if (it != t.species.end()) {
        const auto& r = it->second;
        if (!r.name.empty())     w.kv("name", r.name);
        if (!r.plural.empty())   w.kv("plural", r.plural);
        if (!r.class_.empty())   w.kv("class", r.class_);
        if (!r.portrait.empty()) w.kv("portrait", r.portrait);
        if (!r.traits.empty()) {
            w.key("traits");
            w.begin_array();
            for (const auto& s : r.traits) w.string(s);
            w.end_array();
        }
    }
    w.end_object();
}

void emit_leader_ref(jw::Writer& w, long long id, const Tables& t) {
    w.begin_object();
    w.kv("id", id);
    auto it = t.leaders.find(id);
    if (it != t.leaders.end()) {
        const auto& r = it->second;
        std::string full = r.first_name;
        if (!r.last_name.empty()) {
            if (!full.empty()) full += " ";
            full += r.last_name;
        }
        if (!full.empty()) w.kv("name", full);
        if (!r.class_.empty()) w.kv("class", r.class_);
        if (r.skill > 0)       w.kv("level", r.skill);
        if (r.species_id >= 0) w.kv("species_id", r.species_id);
        if (r.age > 0)         w.kv("age", r.age);
        if (r.paragon)         w.kv("paragon", true);
        if (!r.traits.empty()) {
            w.key("traits");
            w.begin_array();
            for (const auto& s : r.traits) w.string(s);
            w.end_array();
        }
    }
    w.end_object();
}

void emit_pop_ref(jw::Writer& w, long long id, const Tables& t) {
    w.begin_object();
    w.kv("id", id);
    auto it = t.pops.find(id);
    if (it != t.pops.end()) {
        const auto& r = it->second;
        if (r.species_id >= 0) w.kv("species_id", r.species_id);
        if (r.job_id >= 0)     w.kv("job_id", r.job_id);
        if (!r.category.empty()) w.kv("category", r.category);
        if (!r.ethos.empty())    w.kv("ethos", r.ethos);
        if (r.size > 0)        w.kv("size", r.size);
        if (r.enslaved)        w.kv("enslaved", true);
    }
    w.end_object();
}

void emit_building_ref(jw::Writer& w, long long id, const Tables& t) {
    w.begin_object();
    w.kv("id", id);
    auto it = t.buildings.find(id);
    if (it != t.buildings.end()) {
        const auto& r = it->second;
        if (!r.type.empty()) w.kv("type", r.type);
        if (r.level > 1)     w.kv("level", r.level);
        if (r.ruined)        w.kv("ruined", true);
        if (r.owner >= 0)    w.kv("owner", r.owner);
    }
    w.end_object();
}

void emit_district_ref(jw::Writer& w, long long id, const Tables& t) {
    w.begin_object();
    w.kv("id", id);
    auto it = t.districts.find(id);
    if (it != t.districts.end()) {
        const auto& r = it->second;
        if (!r.type.empty()) w.kv("type", r.type);
    }
    w.end_object();
}

void emit_deposit_ref(jw::Writer& w, long long id, const Tables& t) {
    w.begin_object();
    w.kv("id", id);
    auto it = t.deposits.find(id);
    if (it != t.deposits.end()) {
        const auto& r = it->second;
        if (!r.type.empty()) w.kv("type", r.type);
    }
    w.end_object();
}

void emit_country_ref(jw::Writer& w, long long id, const Tables& t) {
    w.begin_object();
    w.kv("id", id);
    auto it = t.countries.find(id);
    if (it != t.countries.end()) {
        const auto& r = it->second;
        if (!r.name.empty()) w.kv("name", r.name);
        if (!r.type.empty()) w.kv("type", r.type);
        if (!r.government_type.empty()) w.kv("government", r.government_type);
        if (!r.authority.empty())       w.kv("authority", r.authority);
        if (!r.ethics.empty()) {
            w.key("ethics");
            w.begin_array();
            for (const auto& s : r.ethics) w.string(s);
            w.end_array();
        }
        if (!r.civics.empty()) {
            w.key("civics");
            w.begin_array();
            for (const auto& s : r.civics) w.string(s);
            w.end_array();
        }
        if (r.capital_planet >= 0)  w.kv("capital_planet", r.capital_planet);
        if (r.military_power > 0)   w.kv("military_power", r.military_power);
        if (r.economy_power > 0)    w.kv("economy_power", r.economy_power);
        if (r.num_sapient_pops > 0) w.kv("num_sapient_pops", r.num_sapient_pops);
        if (r.fleet_size > 0)       w.kv("fleet_size", r.fleet_size);
        if (r.empire_size > 0)      w.kv("empire_size", r.empire_size);
    }
    w.end_object();
}

} // namespace resolve
