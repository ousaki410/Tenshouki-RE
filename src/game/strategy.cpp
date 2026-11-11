/**
 * strategy.cpp - Turn-based strategic layer implementation
 */

#include "strategy.hpp"
#include "core/logging.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>

namespace tensyoki {

namespace {

// One faction per region, ordered north (Hokkaido) to south (Kyushu).
// The player controls region 4 (Kansai / Oda).
const char* kFactionNames[StrategyManager::kRegionCount] = {
    "Date",       // region 0 - Hokkaido
    "Mogami",     // region 1 - Tohoku
    "Hojo",       // region 2 - Kanto
    "Takeda",     // region 3 - Chubu
    "Oda",        // region 4 - Kansai (player)
    "Mori",       // region 5 - Chugoku
    "Chosokabe",  // region 6 - Shikoku
    "Shimazu",    // region 7 - Kyushu
};

const FactionStance kFactionStances[StrategyManager::kRegionCount] = {
    FactionStance::Defensive,   // Date
    FactionStance::Balanced,    // Mogami
    FactionStance::Aggressive,  // Hojo
    FactionStance::Aggressive,  // Takeda
    FactionStance::Balanced,    // Oda (player; unused)
    FactionStance::Balanced,    // Mori
    FactionStance::Defensive,   // Chosokabe
    FactionStance::Aggressive,  // Shimazu
};

const char* facility_name(FacilityType type) {
    switch (type) {
        case FacilityType::Wall:       return "wall";
        case FacilityType::Barracks:   return "barracks";
        case FacilityType::Storehouse: return "storehouse";
        case FacilityType::Market:     return "market";
        case FacilityType::None:       return "none";
    }
    return "facility";
}

int32_t effective_defense(const CastleData& castle) {
    int32_t defense = castle.garrison + castle.defense * 2 +
                      castle.level * 5 + castle.auto_defense_bonus;
    if (castle.has_wall) defense += 20;
    return defense;
}

const std::string& castle_display_name(const CastleData& castle) {
    return castle.name_utf8.empty() ? castle.name : castle.name_utf8;
}

} // namespace

StrategyManager::StrategyManager() : rng_(seed_) {}

void StrategyManager::set_seed(uint32_t seed) {
    seed_ = seed;
    rng_.seed(seed_);
}

float StrategyManager::season_food_production_multiplier(Season season) {
    switch (season) {
        case Season::Spring: return 1.0f;
        case Season::Summer: return 1.2f;
        case Season::Autumn: return 1.5f;
        case Season::Winter: return 0.5f;
    }
    return 1.0f;
}

float StrategyManager::season_food_consumption_multiplier(Season season) {
    // Troops consume more food in winter.
    return season == Season::Winter ? 1.5f : 1.0f;
}

bool StrategyManager::are_adjacent(const CastleData& a, const CastleData& b) {
    uint32_t ra = a.region_id % kRegionCount;
    uint32_t rb = b.region_id % kRegionCount;
    if (ra == rb) return true;
    // Regions form a north-to-south chain.
    uint32_t distance = ra > rb ? ra - rb : rb - ra;
    return distance == 1;
}

void StrategyManager::init(CastleCommand& cmd) {
    rebuild(cmd, false);
    initialized_ = true;
    turn_ = cmd.get_turn_count();
    events_.clear();
    evaluate_end_conditions(cmd);
    LOG_INFO("StrategyManager: campaign initialized, {} factions, turn {}",
             factions_.size(), turn_);
}

void StrategyManager::reset(CastleCommand& cmd) {
    victory_ = false;
    defeat_ = false;
    events_.clear();
    rng_.seed(seed_);
    init(cmd);
}

void StrategyManager::rebuild(CastleCommand& cmd, bool migrate_legacy) {
    // Normalise ownership flags. Save files produced before the ownership
    // field existed only carry is_player_owned; assign those castles to the
    // default faction of their region when migrating.
    for (auto& castle : cmd.castles()) {
        if (castle.owner_id == 0) {
            if (castle.is_player_owned) {
                castle.owner_id = kPlayerFactionId;
            } else if (migrate_legacy) {
                castle.owner_id = default_owner_for_region(castle.region_id);
            }
        }
        castle.is_player_owned = (castle.owner_id == kPlayerFactionId);
    }

    rebuild_factions(cmd);
    turn_ = cmd.get_turn_count();
    evaluate_end_conditions(cmd);
}

void StrategyManager::rebuild_factions(const CastleCommand& cmd) {
    factions_.clear();
    factions_.reserve(kRegionCount);

    for (uint32_t region = 0; region < kRegionCount; ++region) {
        Faction faction;
        faction.id = default_owner_for_region(region);
        faction.name = kFactionNames[region];
        faction.is_player = (faction.id == kPlayerFactionId);
        faction.stance = kFactionStances[region];
        faction.home_region = region;
        faction.alive = false;
        faction.capital_castle = 0;
        factions_.push_back(faction);
    }

    for (auto& faction : factions_) {
        for (const auto& castle : cmd.castles()) {
            if (castle.owner_id != faction.id) continue;
            if (!faction.alive) {
                faction.alive = true;
                faction.capital_castle = castle.index;
            }
        }
    }
}

const Faction* StrategyManager::faction(uint32_t id) const {
    for (const auto& f : factions_) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

std::string StrategyManager::faction_name(uint32_t id) const {
    if (id == 0) return "Independent";
    if (const Faction* f = faction(id)) return f->name;
    return "Faction " + std::to_string(id);
}

std::string StrategyManager::latest_message() const {
    if (events_.empty()) return "";
    return events_.back().message;
}

void StrategyManager::clear_events() {
    events_.clear();
}

void StrategyManager::log_event(StrategyEventType type, const GameDate& date,
                                std::string message,
                                std::vector<StrategyEvent>* out) {
    StrategyEvent event;
    event.type = type;
    event.turn = turn_;
    event.year = date.year;
    event.month = date.month;
    event.message = std::move(message);

    if (out) {
        out->push_back(std::move(event));
    } else {
        events_.push_back(std::move(event));
    }
}

bool StrategyManager::faction_owns_any_castle(const CastleCommand& cmd,
                                              uint32_t faction_id) const {
    for (const auto& castle : cmd.castles()) {
        if (castle.owner_id == faction_id) return true;
    }
    return false;
}

void StrategyManager::evaluate_end_conditions(const CastleCommand& cmd) {
    const int32_t total = static_cast<int32_t>(cmd.castles().size());
    if (total == 0) {
        victory_ = false;
        defeat_ = false;
        return;
    }

    const int32_t player_count = cmd.player_castle_count();
    victory_ = (player_count == total);
    defeat_ = (player_count == 0) && !victory_;
}

void StrategyManager::end_player_turn(CastleCommand& cmd, GameDate& date) {
    if (!initialized_) init(cmd);
    if (game_over()) return;

    std::vector<StrategyEvent> month_events;
    const Season season_before = date.season();

    log_event(StrategyEventType::TurnStarted, date,
              "Turn " + std::to_string(turn_ + 1) + " begins (" +
                  date.season_name() + ").",
              &month_events);

    // ---- Player phase: economy and region bonuses ----
    // Recruitment is a deliberate player action through the castle command
    // UI, so do not auto-spend the treasury here.
    process_player_economy(cmd, date, month_events);
    cmd.process_region_control();

    // ---- AI phase ----
    if (ai_enabled_) {
        process_ai_turn(cmd, date, month_events);
    }

    // ---- Advance the campaign calendar ----
    cmd.advance_turn();
    turn_ = cmd.get_turn_count();
    date.advance_month();

    if (date.season() != season_before) {
        log_event(StrategyEventType::SeasonChanged, date,
                  "The season changed to " + date.season_name() + ".",
                  &month_events);
    }

    // ---- Victory / defeat ----
    evaluate_end_conditions(cmd);
    if (victory_) {
        log_event(StrategyEventType::Victory, date,
                  "Victory! Every castle is under player control.",
                  &month_events);
    } else if (defeat_) {
        log_event(StrategyEventType::Defeat, date,
                  "Defeat! The player no longer controls any castle.",
                  &month_events);
    }

    // Keep the strategy log bounded.
    for (auto& event : month_events) {
        events_.push_back(std::move(event));
    }
    constexpr size_t kMaxEvents = 200;
    if (events_.size() > kMaxEvents) {
        events_.erase(events_.begin(),
                      events_.begin() +
                          static_cast<std::ptrdiff_t>(events_.size() - kMaxEvents));
    }
}

void StrategyManager::process_player_economy(
    CastleCommand& cmd, const GameDate& date,
    std::vector<StrategyEvent>& events) {
    // Collect indices first: rebellion can change ownership mid-loop.
    std::vector<uint32_t> indices;
    for (const auto& castle : cmd.castles()) {
        if (castle.is_player_owned) indices.push_back(castle.index);
    }

    for (uint32_t index : indices) {
        CastleData* castle = cmd.get_castle(index);
        if (!castle || !castle->is_player_owned) continue;
        apply_castle_economy(cmd, *castle, date, true, events);
    }
}

void StrategyManager::apply_castle_economy(
    CastleCommand& cmd, CastleData& castle, const GameDate& date, bool is_player,
    std::vector<StrategyEvent>& events) {
    // ---- Rebellion check (player castles only) ----
    if (is_player && cmd.check_rebellion(castle.index)) {
        log_event(StrategyEventType::CastleLost, date,
                  "Castle " + castle_display_name(castle) +
                      " rebelled and became independent!",
                  &events);
        return;
    }

    // ---- Loyalty drift ----
    if (is_player) {
        int32_t change = cmd.calculate_loyalty_change(castle);
        if (date.season() == Season::Spring) change += 1;
        if (date.season() == Season::Winter) change -= 1;

        castle.loyalty = std::clamp(castle.loyalty + change, 0, 100);
        if (change < 0) {
            castle.unrest = std::clamp(castle.unrest - change, 0, 100);
        } else if (change > 0) {
            castle.unrest =
                std::clamp(castle.unrest - std::max(1, change / 2), 0, 100);
        }
    }

    // ---- Gold income ----
    const int32_t rate = is_player ? cmd.tax_rate()
                                   : std::clamp(castle.tax_rate, 0, 100);
    int32_t base_income = 100;
    if (castle.has_market) base_income += 200;
    if (castle.level > 1) base_income += 50 * (castle.level - 1);
    const int32_t income = std::max(0, base_income * rate / 100);
    castle.gold += income;

    // ---- Food production / consumption ----
    const int32_t production = static_cast<int32_t>(std::lround(
        static_cast<float>(cmd.calculate_food_production(castle)) *
        season_food_production_multiplier(date.season())));
    const int32_t consumption = static_cast<int32_t>(std::lround(
        static_cast<float>(cmd.calculate_food_consumption(castle)) *
        season_food_consumption_multiplier(date.season())));

    castle.food += production - consumption;
    bool starved = false;
    if (castle.food < 0) {
        castle.food = 0;
        starved = true;
    }

    if (starved) {
        const int32_t deserters = std::max(1, castle.garrison / 10);
        castle.garrison = std::max(0, castle.garrison - deserters);
        castle.unrest = std::clamp(castle.unrest + 5, 0, 100);
        log_event(StrategyEventType::Starvation, date,
                  "Castle " + castle_display_name(castle) +
                      " ran out of food; " + std::to_string(deserters) +
                      " troops deserted.",
                  &events);
    }
}

void StrategyManager::process_ai_turn(CastleCommand& cmd, const GameDate& date,
                                      std::vector<StrategyEvent>& events) {
    for (auto& faction : factions_) {
        if (faction.is_player || !faction.alive) continue;
        if (!faction_owns_any_castle(cmd, faction.id)) {
            faction.alive = false;
            continue;
        }

        process_ai_faction(cmd, date, faction, events);

        if (cmd.player_castle_count() == 0) break;
    }
}

void StrategyManager::process_ai_faction(
    CastleCommand& cmd, const GameDate& date, Faction& faction,
    std::vector<StrategyEvent>& events) {
    process_ai_economy(cmd, date, faction, events);

    const int32_t recruited = process_ai_recruitment(cmd, faction);
    if (recruited > 0) {
        log_event(StrategyEventType::AiRecruited, date,
                  faction.name + " recruited " + std::to_string(recruited) +
                      " troops.",
                  &events);
    }

    process_ai_build(cmd, date, faction, events);
    process_ai_attack(cmd, date, faction, events);

    if (!faction_owns_any_castle(cmd, faction.id)) {
        faction.alive = false;
    }
}

void StrategyManager::process_ai_economy(
    CastleCommand& cmd, const GameDate& date, Faction& faction,
    std::vector<StrategyEvent>& events) {
    std::vector<uint32_t> indices;
    for (const auto& castle : cmd.castles()) {
        if (castle.owner_id == faction.id) indices.push_back(castle.index);
    }

    for (uint32_t index : indices) {
        CastleData* castle = cmd.get_castle(index);
        if (!castle || castle->owner_id != faction.id) continue;
        apply_castle_economy(cmd, *castle, date, false, events);
    }
}

int32_t StrategyManager::process_ai_recruitment(CastleCommand& cmd,
                                                Faction& faction) {
    int32_t total = 0;

    for (auto& castle : cmd.castles()) {
        if (castle.owner_id != faction.id) continue;

        // AI keeps a reserve and fills up to 3/4 of the garrison capacity.
        const int32_t target = castle.max_garrison * 3 / 4;
        if (castle.garrison >= target) continue;

        const int32_t needed = target - castle.garrison;
        const int32_t affordable =
            castle.gold / cmd.recruitment_cost_per_troop();
        const int32_t amount = std::min(needed, affordable);
        if (amount <= 0) continue;

        if (cmd.recruit_troops(castle.index, amount) ==
            CommandResult::Success) {
            total += amount;
        }
    }

    return total;
}

bool StrategyManager::process_ai_build(CastleCommand& cmd, const GameDate& date,
                                       Faction& faction,
                                       std::vector<StrategyEvent>& events) {
    // Build occasionally, not every month, and keep a gold reserve.
    if (rng_() % 100 >= 35) return false;

    const FacilityType options[4] = {
        FacilityType::Wall,
        FacilityType::Barracks,
        FacilityType::Storehouse,
        FacilityType::Market,
    };
    const uint32_t start = rng_() % 4;

    for (auto& castle : cmd.castles()) {
        if (castle.owner_id != faction.id) continue;

        for (uint32_t i = 0; i < 4; ++i) {
            const FacilityType type = options[(start + i) % 4];
            const int32_t cost = cmd.facility_cost(type);
            if (castle.gold < cost + 250) continue;

            if (cmd.build_facility(castle.index, type) ==
                CommandResult::Success) {
                log_event(StrategyEventType::AiBuilt, date,
                          faction.name + " built a " +
                              std::string(facility_name(type)) + " at " +
                              castle_display_name(castle) + ".",
                          &events);
                return true;
            }
        }
    }

    return false;
}

std::optional<uint32_t> StrategyManager::weakest_adjacent_enemy(
    const CastleCommand& cmd, const CastleData& source,
    uint32_t faction_id) const {
    std::optional<uint32_t> best;
    int32_t best_defense = INT32_MAX;

    for (const auto& castle : cmd.castles()) {
        if (castle.index == source.index) continue;
        if (castle.owner_id == faction_id) continue;
        if (!are_adjacent(source, castle)) continue;

        const int32_t defense = effective_defense(castle);
        if (defense < best_defense) {
            best_defense = defense;
            best = castle.index;
        }
    }

    return best;
}

bool StrategyManager::process_ai_attack(CastleCommand& cmd,
                                        const GameDate& date,
                                        Faction& faction,
                                        std::vector<StrategyEvent>& events) {
    int32_t attack_chance = 40;
    int32_t strength_ratio = 130;
    switch (faction.stance) {
        case FactionStance::Aggressive:
            attack_chance = 70;
            strength_ratio = 100;
            break;
        case FactionStance::Balanced:
            attack_chance = 40;
            strength_ratio = 130;
            break;
        case FactionStance::Defensive:
            attack_chance = 20;
            strength_ratio = 170;
            break;
    }

    if (static_cast<int32_t>(rng_() % 100) >= attack_chance) return false;

    CastleData* best_source = nullptr;
    CastleData* best_target = nullptr;
    int32_t best_margin = INT32_MIN;

    for (auto& source : cmd.castles()) {
        if (source.owner_id != faction.id) continue;

        const auto target_index =
            weakest_adjacent_enemy(cmd, source, faction.id);
        if (!target_index) continue;

        CastleData* target = cmd.get_castle(*target_index);
        if (!target) continue;

        const int32_t defense = effective_defense(*target);
        if (source.garrison * 100 < defense * strength_ratio) continue;

        const int32_t margin = source.garrison * 100 - defense * strength_ratio;
        if (margin > best_margin) {
            best_margin = margin;
            best_source = &source;
            best_target = target;
        }
    }

    if (!best_source || !best_target) return false;

    return resolve_attack(cmd, *best_source, *best_target, faction.id, date,
                          events);
}

bool StrategyManager::resolve_attack(CastleCommand& cmd, CastleData& source,
                                     CastleData& target,
                                     uint32_t attacker_faction,
                                     const GameDate& date,
                                     std::vector<StrategyEvent>& events) {
    if (source.index == target.index) return false;
    if (source.owner_id != attacker_faction) return false;
    if (target.owner_id == attacker_faction) return false;

    const uint32_t old_owner = target.owner_id;
    const std::string attacker_name = faction_name(attacker_faction);
    const std::string defender_name = faction_name(old_owner);

    const int32_t attack_power = source.garrison + source.level * 5 +
                                 static_cast<int32_t>(rng_() % 20);
    const int32_t defense_power = effective_defense(target) +
                                  static_cast<int32_t>(rng_() % 20);

    if (attack_power > defense_power) {
        // ---- Capture ----
        const int32_t loot = std::max(0, target.gold / 2);
        source.gold += loot;
        target.gold = std::max(0, target.gold - loot);

        const int32_t occupy_cap = std::max(5, target.max_garrison);
        const int32_t occupying_troops =
            std::min(std::max(5, source.garrison / 3), occupy_cap);
        source.garrison = std::max(0, source.garrison - source.garrison / 2);

        target.owner_id = attacker_faction;
        target.is_player_owned = (attacker_faction == kPlayerFactionId);
        target.garrison = occupying_troops;
        target.loyalty = 40;
        target.unrest = 20;
        target.lord_officer_id = 0;
        target.lord_name.clear();
        target.lord_name_utf8.clear();

        if (old_owner == kPlayerFactionId) {
            log_event(StrategyEventType::CastleLost, date,
                      "Castle " + castle_display_name(target) +
                          " was captured by " + attacker_name + "!",
                      &events);
        } else if (attacker_faction == kPlayerFactionId) {
            log_event(StrategyEventType::CastleCaptured, date,
                      "The player captured " + castle_display_name(target) +
                          " from " + defender_name + ".",
                      &events);
        } else {
            log_event(StrategyEventType::CastleCaptured, date,
                      attacker_name + " captured " +
                          castle_display_name(target) + " from " +
                          defender_name + ".",
                      &events);
        }

        // ---- Elimination check ----
        if (old_owner != 0 && !faction_owns_any_castle(cmd, old_owner)) {
            for (auto& faction : factions_) {
                if (faction.id != old_owner) continue;
                faction.alive = false;
                log_event(StrategyEventType::FactionEliminated,
                          date,
                          faction.name + " has been eliminated.",
                          &events);
                break;
            }
        }

        LOG_INFO("Strategy: {} captured castle {} from {}",
                 attacker_name, target.index, defender_name);
        return true;
    }

    // ---- Attack repelled ----
    const int32_t attacker_loss = std::max(1, source.garrison / 4);
    source.garrison = std::max(0, source.garrison - attacker_loss);
    target.garrison = std::max(0, target.garrison - std::max(1, target.garrison / 5));

    log_event(StrategyEventType::AiAttack, date,
              attacker_name + " attacked " + castle_display_name(target) +
                  " but was repelled.",
              &events);
    return false;
}

} // namespace tensyoki
