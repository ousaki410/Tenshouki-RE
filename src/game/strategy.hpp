/**
 * strategy.hpp - Turn-based strategic layer
 *
 * Drives the campaign at the scale of one month per turn:
 *   - Player economy (tax income, food, loyalty, rebellion)
 *   - AI faction economy (income, food, recruitment, construction)
 *   - AI military actions (adjacent castle attacks resolved abstractly)
 *   - Seasonal modifiers (spring growth, summer heat, autumn harvest,
 *     winter attrition)
 *   - Calendar and turn advancement, victory/defeat evaluation
 *
 * The tactical battle layer remains player-driven: AI-vs-AI and AI-vs-player
 * attacks are resolved statistically so the campaign can progress without
 * interrupting the player for every distant skirmish.
 *
 * Faction ownership model:
 *   owner_id 0      = independent / neutral castle
 *   owner_id 1      = player faction (Oda, starts in Kansai)
 *   owner_id 2..8   = AI factions, each starting in one region
 */

#pragma once

#include "castle_command.hpp"
#include "game_date.hpp"
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace tensyoki {

/// How aggressively an AI faction pursues military expansion.
enum class FactionStance {
    Defensive,
    Balanced,
    Aggressive,
};

/// A campaign faction.
struct Faction {
    uint32_t id = 0;                 // Faction id stored in CastleData::owner_id
    std::string name;                // Display name
    bool is_player = false;          // Player-controlled faction
    bool alive = true;               // Still owns at least one castle
    FactionStance stance = FactionStance::Balanced;
    uint32_t home_region = 0;        // Region the faction starts in
    uint32_t capital_castle = 0;     // First castle owned (display / future rules)
};

/// Event categories used for UI colouring and test assertions.
enum class StrategyEventType {
    Info,
    TurnStarted,
    SeasonChanged,
    AiRecruited,
    AiBuilt,
    AiAttack,
    OfficerAssigned,
    CastleCaptured,
    CastleLost,
    Starvation,
    FactionEliminated,
    Victory,
    Defeat,
};

/// One entry in the strategy log.
struct StrategyEvent {
    StrategyEventType type = StrategyEventType::Info;
    int32_t turn = 0;
    int16_t year = 0;
    uint8_t month = 1;
    std::string message;
};

/// Turn-based campaign manager.
class StrategyManager {
public:
    static constexpr uint32_t kPlayerFactionId = 1;
    static constexpr uint32_t kRegionCount = 8;

    StrategyManager();

    // ---- Lifecycle ----

    /// Build factions from the current castle ownership and start the campaign.
    void init(CastleCommand& cmd);

    /// Rebuild faction bookkeeping after ownership changes (battle, load).
    /// When migrate_legacy is true, castles without an owner id are assigned
    /// to their region's default faction (used for version 1 save files).
    void rebuild(CastleCommand& cmd, bool migrate_legacy = false);

    /// Full reset: clear events and flags, then init.
    void reset(CastleCommand& cmd);

    bool initialized() const { return initialized_; }

    // ---- Turn processing ----

    /// End the player's turn: player economy -> AI turn -> season change ->
    /// calendar/turn advance -> victory/defeat evaluation.
    void end_player_turn(CastleCommand& cmd, GameDate& date);

    /// Apply one month of economy to player-owned castles.
    void process_player_economy(CastleCommand& cmd, const GameDate& date,
                                std::vector<StrategyEvent>& events);

    /// Run every living AI faction for one month.
    void process_ai_turn(CastleCommand& cmd, const GameDate& date,
                         std::vector<StrategyEvent>& events);

    /// Resolve an abstract attack. Returns true when the target is captured.
    bool resolve_attack(CastleCommand& cmd, CastleData& source,
                        CastleData& target, uint32_t attacker_faction,
                        const GameDate& date,
                        std::vector<StrategyEvent>& events);

    /// Recompute victory/defeat flags from current ownership.
    void evaluate_end_conditions(const CastleCommand& cmd);

    // ---- Queries ----

    const std::vector<Faction>& factions() const { return factions_; }
    const Faction* faction(uint32_t id) const;
    std::string faction_name(uint32_t id) const;

    const std::vector<StrategyEvent>& events() const { return events_; }
    std::string latest_message() const;
    void clear_events();

    int32_t turn() const { return turn_; }
    bool victory() const { return victory_; }
    bool defeat() const { return defeat_; }
    bool game_over() const { return victory_ || defeat_; }

    void set_seed(uint32_t seed);
    void set_ai_enabled(bool enabled) { ai_enabled_ = enabled; }
    bool ai_enabled() const { return ai_enabled_; }

    // ---- Static helpers (exposed for tests and UI) ----

    /// Region that the player faction starts in (Kansai).
    static constexpr uint32_t kPlayerStartRegion = 4;

    /// Food production multiplier for a season.
    static float season_food_production_multiplier(Season season);
    /// Food consumption multiplier for a season.
    static float season_food_consumption_multiplier(Season season);

    /// Two castles are considered adjacent when they share a region or belong
    /// to neighbouring regions in the north-to-south region chain.
    static bool are_adjacent(const CastleData& a, const CastleData& b);

private:
    void rebuild_factions(const CastleCommand& cmd);
    void log_event(StrategyEventType type, const GameDate& date,
                   std::string message,
                   std::vector<StrategyEvent>* out = nullptr);

    void process_ai_faction(CastleCommand& cmd, const GameDate& date,
                            Faction& faction,
                            std::vector<StrategyEvent>& events);
    void process_ai_economy(CastleCommand& cmd, const GameDate& date,
                            Faction& faction,
                            std::vector<StrategyEvent>& events);
    int32_t process_ai_recruitment(CastleCommand& cmd, Faction& faction);
    void process_ai_officer_assignment(CastleCommand& cmd, Faction& faction,
                                       const GameDate& date,
                                       std::vector<StrategyEvent>& events);
    bool process_ai_build(CastleCommand& cmd, const GameDate& date,
                          Faction& faction,
                          std::vector<StrategyEvent>& events);
    bool process_ai_attack(CastleCommand& cmd, const GameDate& date,
                           Faction& faction,
                           std::vector<StrategyEvent>& events);

    void apply_castle_economy(CastleCommand& cmd, CastleData& castle,
                              const GameDate& date, bool is_player,
                              std::vector<StrategyEvent>& events);

    bool faction_owns_any_castle(const CastleCommand& cmd,
                                 uint32_t faction_id) const;
    std::optional<uint32_t> weakest_adjacent_enemy(
        const CastleCommand& cmd, const CastleData& source,
        uint32_t faction_id) const;

    std::vector<Faction> factions_;
    std::vector<StrategyEvent> events_;
    bool initialized_ = false;
    bool ai_enabled_ = true;
    int32_t turn_ = 0;
    bool victory_ = false;
    bool defeat_ = false;
    uint32_t seed_ = 0x7E45A123u;
    std::mt19937 rng_;
};

} // namespace tensyoki
