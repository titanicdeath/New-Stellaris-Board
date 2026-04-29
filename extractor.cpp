// extractor.cpp (v2)
// Walks a parsed Stellaris gamestate and emits JSON containing:
//   - meta:    save metadata
//   - player:  who's playing, which country
//   - country: FULL block via the generic emitter (raw passthrough)
//   - planets: every owned planet with cross-references resolved inline
//
// Cross-reference resolution: see resolver.{h,cpp}. Pop, building, district,
// deposit, leader, species, and country IDs that appear inside a planet's
// data are expanded into objects carrying the relevant fields, so the
// dashboard can render planet data without doing joins itself.

#include "json_writer.h"
#include "pdx_node.h"
#include "resolver.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// ---------------------------------------------------------------- file I/O

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open: " + path);
    auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::string buf(static_cast<size_t>(size), '\0');
    if (!in.read(buf.data(), size)) throw std::runtime_error("read failed: " + path);
    return buf;
}

// ---------------------------------------------------------------- generic JSON converter
//
// Same approach as v1: scalars sniff to int/float/bool; blocks become objects
// (or arrays for anonymous-only entries, or _items-style mixed objects).

void emit_node(jw::Writer& w, const pdx::Node& n);

void emit_scalar(jw::Writer& w, const pdx::Node& n) {
    if (n.quoted) { w.string(n.scalar); return; }
    const auto& s = n.scalar;
    if (s.empty())            { w.string(s); return; }
    if (s == "yes")           { w.boolean(true);  return; }
    if (s == "no")            { w.boolean(false); return; }
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    bool all_digits = (i < s.size());
    for (; i < s.size() && all_digits; ++i) {
        if (s[i] < '0' || s[i] > '9') all_digits = false;
    }
    if (all_digits) {
        try {
            size_t pos = 0;
            long long v = std::stoll(s, &pos);
            if (pos == s.size()) { w.integer(v); return; }
        } catch (...) {}
        w.string(s);
        return;
    }
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos == s.size()) { w.number(v); return; }
    } catch (...) {}
    w.string(s);
}

void emit_block(jw::Writer& w, const pdx::Node& n) {
    bool any_named = false, any_anon = false;
    std::unordered_set<std::string> seen_keys;
    bool any_dup = false;
    for (const auto& e : n.entries) {
        if (e.key.empty()) any_anon = true;
        else {
            any_named = true;
            if (!seen_keys.insert(e.key).second) any_dup = true;
        }
    }

    if (any_anon && !any_named) {
        w.begin_array();
        for (const auto& e : n.entries) emit_node(w, *e.value);
        w.end_array();
        return;
    }
    if (any_named && !any_anon && !any_dup) {
        w.begin_object();
        for (const auto& e : n.entries) {
            w.key(e.key);
            emit_node(w, *e.value);
        }
        w.end_object();
        return;
    }
    if (!any_named && !any_anon) {
        w.begin_object();
        w.end_object();
        return;
    }

    // Mixed / duplicates: collapse duplicate keys into arrays, anonymous → _items.
    w.begin_object();
    std::vector<std::string> order;
    std::unordered_set<std::string> emitted;
    for (const auto& e : n.entries) {
        if (e.key.empty()) continue;
        if (emitted.count(e.key)) continue;
        emitted.insert(e.key);
        order.push_back(e.key);
    }
    for (const auto& k : order) {
        int count = 0;
        for (const auto& e : n.entries) if (e.key == k) ++count;
        w.key(k);
        if (count == 1) {
            for (const auto& e : n.entries) {
                if (e.key == k) { emit_node(w, *e.value); break; }
            }
        } else {
            w.begin_array();
            for (const auto& e : n.entries) {
                if (e.key == k) emit_node(w, *e.value);
            }
            w.end_array();
        }
    }
    if (any_anon) {
        w.key("_items");
        w.begin_array();
        for (const auto& e : n.entries) {
            if (e.key.empty()) emit_node(w, *e.value);
        }
        w.end_array();
    }
    w.end_object();
}

void emit_node(jw::Writer& w, const pdx::Node& n) {
    if (n.kind == pdx::NodeKind::Scalar) emit_scalar(w, n);
    else                                  emit_block(w, n);
}

// ---------------------------------------------------------------- discovery

struct PlayerInfo {
    std::string player_name;
    long long country_id = -1;
};

std::optional<PlayerInfo> find_player(const pdx::Node& root) {
    auto* player = root.find("player");
    if (!player) return std::nullopt;
    for (const auto& e : player->entries) {
        if (!e.value || e.value->kind != pdx::NodeKind::Block) continue;
        const auto& obj = *e.value;
        if (auto* country = obj.find("country")) {
            PlayerInfo info;
            info.player_name = obj.child_string("name");
            info.country_id  = country->as_int(-1);
            return info;
        }
    }
    return std::nullopt;
}

const pdx::Node* find_by_id(const pdx::Node& parent, long long id) {
    std::string s = std::to_string(id);
    for (const auto& e : parent.entries) {
        if (e.key == s) return e.value.get();
    }
    return nullptr;
}

std::vector<long long> read_id_list(const pdx::Node* block) {
    std::vector<long long> out;
    if (!block) return out;
    for (const auto& e : block->entries) {
        if (!e.key.empty() || !e.value) continue;
        if (e.value->kind != pdx::NodeKind::Scalar) continue;
        try { out.push_back(std::stoll(e.value->scalar)); }
        catch (...) {}
    }
    return out;
}

// ---------------------------------------------------------------- planet emission

// Emit one owned planet with cross-references resolved.
//
// We do a hybrid pass: for fields known to be ID-lists we want resolved
// (pop_groups, pop_jobs, buildings_cache, districts, deposits, army),
// we replace them with resolved arrays. For ID-scalars (owner, controller,
// governor, original_owner) we replace with resolved objects. For
// everything else, we use the generic emitter so we don't drop unknown
// fields.
//
// The `resolved_keys` set tracks which keys we've custom-handled so the
// generic pass at the end doesn't re-emit them.
void emit_planet(jw::Writer& w, const pdx::Node& planet,
                 const resolve::Tables& t)
{
    using namespace resolve;
    w.begin_object();

    std::unordered_set<std::string> resolved_keys;

    // ---- ID-list fields with type-specific resolvers ----
    auto emit_resolved_list = [&](const char* key, auto resolver) {
        if (auto* node = planet.find(key)) {
            w.key(key);
            w.begin_array();
            for (const auto& e : node->entries) {
                if (!e.key.empty() || !e.value || e.value->kind != pdx::NodeKind::Scalar) continue;
                try { resolver(w, std::stoll(e.value->scalar), t); }
                catch (...) {}
            }
            w.end_array();
            resolved_keys.insert(key);
        }
    };
    emit_resolved_list("pop_groups",      emit_pop_ref);
    emit_resolved_list("pop_jobs",        emit_pop_ref);
    emit_resolved_list("buildings_cache", emit_building_ref);
    emit_resolved_list("districts",       emit_district_ref);
    emit_resolved_list("deposits",        emit_deposit_ref);
    emit_resolved_list("army",            [](jw::Writer& jw_, long long id, const Tables&) {
        jw_.begin_object(); jw_.kv("id", id); jw_.end_object();
    });

    // ---- ID-scalar fields ----
    auto emit_resolved_scalar = [&](const char* key, auto resolver) {
        if (auto* node = planet.find(key)) {
            if (node->kind == pdx::NodeKind::Scalar) {
                long long id = node->as_int(-1);
                if (id >= 0) {
                    w.key(key);
                    resolver(w, id, t);
                    resolved_keys.insert(key);
                }
            }
        }
    };
    emit_resolved_scalar("owner",          emit_country_ref);
    emit_resolved_scalar("controller",     emit_country_ref);
    emit_resolved_scalar("original_owner", emit_country_ref);
    emit_resolved_scalar("governor",       emit_leader_ref);

    // species_information uses species IDs as KEYS (not list of IDs), so we
    // handle it specially: keep the structure but inject species names.
    if (auto* si = planet.find("species_information")) {
        w.key("species_information");
        w.begin_object();
        for (const auto& e : si->entries) {
            if (e.key.empty() || !e.value) continue;
            w.key(e.key);
            w.begin_object();
            try {
                long long sid = std::stoll(e.key);
                auto it = t.species.find(sid);
                if (it != t.species.end()) {
                    if (!it->second.name.empty()) w.kv("name", it->second.name);
                    if (!it->second.class_.empty()) w.kv("class", it->second.class_);
                }
            } catch (...) {}
            if (e.value->kind == pdx::NodeKind::Block) {
                for (const auto& sub : e.value->entries) {
                    if (sub.key.empty() || !sub.value) continue;
                    w.key(sub.key);
                    emit_node(w, *sub.value);
                }
            }
            w.end_object();
        }
        w.end_object();
        resolved_keys.insert("species_information");
    }

    // species_refs: list of species IDs — resolve to species objects
    emit_resolved_list("species_refs", emit_species_ref);

    // ---- everything else, raw passthrough via generic emitter ----
    for (const auto& e : planet.entries) {
        if (e.key.empty()) continue;
        if (resolved_keys.count(e.key)) continue;
        w.key(e.key);
        emit_node(w, *e.value);
    }

    w.end_object();
}

} // namespace

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <gamestate-file> <output.json>\n";
        return 1;
    }
    const std::string in_path = argv[1];
    const std::string out_path = argv[2];

    auto t0 = std::chrono::steady_clock::now();
    std::cerr << "[1/5] Reading " << in_path << " ... " << std::flush;
    std::string source = slurp(in_path);
    std::cerr << source.size() << " bytes\n";

    auto t1 = std::chrono::steady_clock::now();
    std::cerr << "[2/5] Parsing ... " << std::flush;
    auto root = pdx::parse(source);
    auto t2 = std::chrono::steady_clock::now();
    std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()
              << " ms\n";

    std::cerr << "[3/5] Building resolution tables ... " << std::flush;
    auto tables = resolve::build(*root);
    auto t3 = std::chrono::steady_clock::now();
    std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count()
              << " ms"
              << "  (species=" << tables.species.size()
              << " leaders=" << tables.leaders.size()
              << " pops=" << tables.pops.size()
              << " buildings=" << tables.buildings.size()
              << " districts=" << tables.districts.size()
              << " deposits=" << tables.deposits.size()
              << " countries=" << tables.countries.size()
              << ")\n";

    std::cerr << "[4/5] Discovering player and planets ...\n";
    auto player_opt = find_player(*root);
    if (!player_opt) {
        std::cerr << "ERROR: no `player` block found in save\n";
        return 2;
    }
    auto& player = *player_opt;
    std::cerr << "       player: \"" << player.player_name
              << "\"  country_id=" << player.country_id << "\n";

    auto* country_root = root->find("country");
    if (!country_root) { std::cerr << "ERROR: no `country` block\n"; return 2; }
    auto* my_country = find_by_id(*country_root, player.country_id);
    if (!my_country) {
        std::cerr << "ERROR: country id " << player.country_id
                  << " not found in country block\n";
        return 2;
    }
    auto myc_it = tables.countries.find(player.country_id);
    std::cerr << "       country: \""
              << (myc_it != tables.countries.end() ? myc_it->second.name : std::string("(unresolved)"))
              << "\"\n";

    auto* planets_block = root->find("planets");
    const pdx::Node* planet_table = planets_block ? planets_block->find("planet") : nullptr;
    if (!planet_table) { std::cerr << "ERROR: no `planets.planet` block\n"; return 2; }

    auto owned_ids = read_id_list(my_country->find("owned_planets"));
    std::cerr << "       owned planets (from country.owned_planets): "
              << owned_ids.size() << "\n";

    std::vector<long long> by_owner;
    for (const auto& e : planet_table->entries) {
        if (!e.value || e.value->kind != pdx::NodeKind::Block) continue;
        if (auto* o = e.value->find("owner")) {
            if (o->as_int(-1) == player.country_id) {
                try { by_owner.push_back(std::stoll(e.key)); } catch (...) {}
            }
        }
    }
    std::cerr << "       planets with owner=" << player.country_id
              << ": " << by_owner.size() << "\n";

    // ---- emit JSON ----
    std::cerr << "[5/5] Writing " << out_path << " ...\n";
    std::ofstream out(out_path, std::ios::binary);
    if (!out) { std::cerr << "ERROR: cannot open output\n"; return 2; }

    jw::Writer w(out, /*pretty=*/true);
    w.begin_object();

    w.key("meta");
    w.begin_object();
    w.kv("version", root->child_string("version"));
    w.kv("version_control_revision", root->child_int("version_control_revision"));
    w.kv("save_name", root->child_string("name"));
    w.kv("date", root->child_string("date"));
    w.end_object();

    w.key("player");
    w.begin_object();
    w.kv("name", player.player_name);
    w.key("country");
    resolve::emit_country_ref(w, player.country_id, tables);
    w.end_object();

    // country: full passthrough via generic emitter
    w.key("country");
    emit_node(w, *my_country);

    w.key("planet_id_sources");
    w.begin_object();
    {
        w.key("from_country_owned_planets");
        w.begin_array();
        for (auto id : owned_ids) w.integer(id);
        w.end_array();
        w.key("from_owner_field");
        w.begin_array();
        for (auto id : by_owner) w.integer(id);
        w.end_array();
    }
    w.end_object();

    std::unordered_set<long long> all_ids;
    for (auto id : owned_ids) all_ids.insert(id);
    for (auto id : by_owner) all_ids.insert(id);

    w.key("planets");
    w.begin_object();
    for (long long id : all_ids) {
        auto* planet = find_by_id(*planet_table, id);
        if (!planet) continue;
        w.key(std::to_string(id));
        emit_planet(w, *planet, tables);
    }
    w.end_object();

    // Galactic context: every country we have a relation with, plus subjects.
    w.key("galactic_context");
    w.begin_object();
    {
        std::unordered_set<long long> referenced;
        if (auto* rm = my_country->find("relations_manager")) {
            for (auto* rel : rm->find_all("relation")) {
                if (!rel) continue;
                long long cid = rel->child_int("country", -1);
                if (cid >= 0) referenced.insert(cid);
            }
        }
        for (auto sid : read_id_list(my_country->find("subjects"))) referenced.insert(sid);

        w.key("countries");
        w.begin_object();
        for (long long cid : referenced) {
            w.key(std::to_string(cid));
            resolve::emit_country_ref(w, cid, tables);
        }
        w.end_object();
    }
    w.end_object();

    w.end_object();
    out.put('\n');

    auto t5 = std::chrono::steady_clock::now();
    std::cerr << "Done in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t5 - t0).count()
              << " ms total\n";
    return 0;
}
