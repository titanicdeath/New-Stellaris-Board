// resolver.h
// Cross-reference resolution: build per-block in-memory tables that map
// integer IDs to lightweight record structs, then expose helpers that the
// JSON emitter calls to expand ID-lists into inline objects.
//
// Why this lives in its own translation unit:
//   - extractor.cpp is already large and growing
//   - the resolution logic is shaped by Stellaris's data model, not by
//     either the parser or the JSON writer; isolating it keeps the
//     responsibilities clean
//   - if we ever want resolution maps for non-planet purposes (e.g. fleet
//     dashboard) the API is reusable

#pragma once

#include "json_writer.h"
#include "pdx_node.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace resolve {

// ---------- record types ----------

struct SpeciesRec {
    std::string name;       // "Xellia" — usually a localization key, not English
    std::string plural;     // "Xellians"
    std::string class_;     // "LITHOID" / "MAMMALIAN" / "ROBOT" / etc.
    std::string portrait;
    std::vector<std::string> traits;
    long long home_planet = -1;
    long long base_ref = -1; // for sub-species
};

struct LeaderRec {
    std::string first_name;
    std::string last_name;
    std::string class_;     // "scientist" / "official" / "commander" / etc.
    int skill = 0;
    std::string gender;
    long long species_id = -1;
    long long age = 0;
    bool paragon = false;
    std::vector<std::string> traits;
};

struct PopRec {
    long long species_id = -1;
    long long job_id = -1;          // index into pop_jobs? Often a job-instance id
    std::string category;            // "ruler"/"specialist"/"worker"/etc.
    std::string ethos;
    long long planet_id = -1;
    int size = 0;
    bool enslaved = false;
};

struct BuildingRec {
    std::string type;            // "building_energy_nexus"
    long long planet_id = -1;
    int level = 1;
    bool ruined = false;
    long long owner = -1;
};

struct DistrictRec {
    std::string type;            // "district_city"
    long long planet_id = -1;
};

struct DepositRec {
    std::string type;            // "d_minerals_3"
    long long planet_id = -1;
};

// Country records are special: every country in the galaxy gets a *light*
// record (id, name, type, government), with the player's full block emitted
// elsewhere via the generic emitter.
struct CountryRec {
    std::string name;            // resolved from name block (key field)
    std::string type;            // "default" / "fallen_empire" / "primitive" / ...
    std::string government_type;
    std::string authority;
    std::vector<std::string> ethics;
    std::vector<std::string> civics;
    long long capital_planet = -1;
    long long founder_species_ref = -1;
    double military_power = 0;
    double economy_power = 0;
    long long num_sapient_pops = 0;
    long long fleet_size = 0;
    long long empire_size = 0;
};

// ---------- the master maps ----------

struct Tables {
    std::unordered_map<long long, SpeciesRec>  species;
    std::unordered_map<long long, LeaderRec>   leaders;
    std::unordered_map<long long, PopRec>      pops;
    std::unordered_map<long long, BuildingRec> buildings;
    std::unordered_map<long long, DistrictRec> districts;
    std::unordered_map<long long, DepositRec>  deposits;
    std::unordered_map<long long, CountryRec>  countries;
};

// Build all tables from the parsed root.
Tables build(const pdx::Node& root);

// ---------- emission helpers ----------
//
// Each helper expands an ID or list-of-IDs into JSON inline. They take the
// raw ID-list source node from the planet and write enriched objects.

// Emit a single ID as either {id, ...resolved_fields} or {id} if not found.
void emit_species_ref (jw::Writer& w, long long id, const Tables& t);
void emit_leader_ref  (jw::Writer& w, long long id, const Tables& t);
void emit_pop_ref     (jw::Writer& w, long long id, const Tables& t);
void emit_building_ref(jw::Writer& w, long long id, const Tables& t);
void emit_district_ref(jw::Writer& w, long long id, const Tables& t);
void emit_deposit_ref (jw::Writer& w, long long id, const Tables& t);
void emit_country_ref (jw::Writer& w, long long id, const Tables& t);

// Walk a primitive-int-list block (`{ 2 252 362 ... }`) and emit each via
// the supplied per-id callback. Convenience for planet ID lists.
template <typename Fn>
void emit_id_list(jw::Writer& w, const pdx::Node* list_block, Fn&& per_id) {
    w.begin_array();
    if (list_block) {
        for (const auto& e : list_block->entries) {
            if (!e.key.empty() || !e.value || e.value->kind != pdx::NodeKind::Scalar) continue;
            try {
                long long id = std::stoll(e.value->scalar);
                per_id(w, id);
            } catch (...) { /* skip malformed */ }
        }
    }
    w.end_array();
}

} // namespace resolve
