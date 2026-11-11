# Turn-Based Strategy Layer

Status: implemented (Phase 6)
Last updated: 2026-11-11
Code: `src/game/strategy.hpp`, `src/game/strategy.cpp`
Integration: `src/game/game_states.cpp` (`MapViewState`, `MainMenuState`),
`src/game/map_view.cpp`, `src/game/save_system.cpp`
Test: `./build/tensyoki --test-strategy`

## 1. Overview

The campaign is turn-based. One turn is one in-game month. The abstract
strategic layer exists so the campaign can progress without forcing the player
into a tactical battle for every distant AI skirmish:

- The player resolves an entire month by pressing **End Turn (Next Month)**.
- Player castles produce gold and food, drift in loyalty, and may rebel or
  starve.
- Every living AI faction acts once: economy, recruitment, construction, and
  possibly one attack.
- AI-vs-AI and AI-vs-player attacks are resolved statistically.
- Player-initiated attacks still enter the tactical `BattleState`; the map is
  updated with the battle result afterwards.

The real-time `GameClock` is paused by default. The date only advances through
the turn system, so `GameDate` remains the single source of truth for year,
month, season, and the UI date label.

## 2. Factions and Map Ownership

`CastleData::owner_id` stores campaign ownership:

| `owner_id` | Meaning |
| ---: | --- |
| `0` | Independent / neutral castle |
| `1` | Player faction (Oda) |
| `2..8` | AI factions |

Eight factions are created at campaign start, one per abstract region. Regions
form a north-to-south chain (`0 = north`, `7 = south`) and are currently
assigned with `castle.index % 8`, because the port does not yet have a verified
per-castle region table.

| Region | Faction | Id | Stance |
| ---: | --- | ---: | --- |
| 0 | Date | 2 | Defensive |
| 1 | Mogami | 3 | Balanced |
| 2 | Hojo | 4 | Aggressive |
| 3 | Takeda | 5 | Aggressive |
| 4 | Oda (player) | 1 | Balanced |
| 5 | Mori | 6 | Balanced |
| 6 | Chosokabe | 7 | Defensive |
| 7 | Shimazu | 8 | Aggressive |

The player starts in region 4 (Kansai). `default_owner_for_region()` and
`default_region_for_owner()` in `castle_command.hpp` are the single mapping used
by campaign init, save migration, the map, and tests.

Every castle starts with 60 troops (60% of `max_garrison`), 1,000 gold, and
500 food. The starting garrison prevents the aggressive AI factions from
capturing undefended player castles on the first turn; the player still has to
recruit and build to hold the line afterwards.

Two castles are adjacent when they share a region or belong to neighbouring
regions in the chain. This is the only attack-range rule; it is independent of
screen distance.

## 3. Turn Flow

`StrategyManager::end_player_turn()` performs, in order:

1. Log the turn start.
2. Player phase:
   - rebellion checks, loyalty drift, gold income, food production and
     consumption for every player castle;
   - region-control bonuses.
3. AI phase: each living AI faction runs economy, recruitment, construction,
   and at most one abstract attack.
4. `CastleCommand::advance_turn()` and `GameDate::advance_month()`.
5. Log a season change when the new month crosses a season boundary.
6. Recompute victory/defeat and log the result.

Recruitment is not automatic for the player: it remains a deliberate action in
the castle command UI. This keeps the treasury meaningful and prevents the turn
button from silently spending gold.

## 4. Player Economy

Gold income per castle:

```
base = 100
       + 200 if market
       + 50 * (level - 1)
income = max(0, base * tax_rate / 100)
```

Food:

```
production  = base_food(castle) * season_production_multiplier
consumption = max(1, garrison / 10) * season_consumption_multiplier
```

Base food is `50 + 50 if storehouse + 25 * (level - 1)`.

Loyalty drift uses `CastleCommand::calculate_loyalty_change()`: high tax lowers
loyalty, low tax raises it, a large garrison and facilities help, and a present
lord adds `+2`. Spring adds `+1`, winter subtracts `1`.

If food would go below zero, the castle is clamped to `0` food, loses 10% of its
garrison (at least one soldier), gains `+5` unrest, and logs a starvation event.

Rebellion is checked before income. A rebelling castle becomes independent
(`owner_id = 0`), resets loyalty to `50` and unrest to `30`, and is skipped for
the rest of that month. Rebellion events also drive the red `REBEL!` marker on
the map for five turns.

### 4.1 Seasonal modifiers

| Season | Months | Food production | Food consumption |
| --- | --- | ---: | ---: |
| Spring | 3–5 | ×1.0 | ×1.0 |
| Summer | 6–8 | ×1.2 | ×1.0 |
| Autumn | 9–11 | ×1.5 | ×1.0 |
| Winter | 12–2 | ×0.5 | ×1.5 |

## 5. AI Behaviour

Each AI faction, once per month:

1. **Economy** — the same income, food, and starvation rules as the player,
   but no rebellion checks.
2. **Recruitment** — fills each owned castle up to 75% of `max_garrison` while
   gold allows.
3. **Construction** — 35% chance per faction per month; builds the first
   affordable facility while keeping a 250-gold reserve.
4. **Attack** — at most one attack per faction per month.

Attack chance and strength requirements depend on stance:

| Stance | Attack chance | Required strength ratio |
| --- | ---: | ---: |
| Aggressive | 70% | 100% |
| Balanced | 40% | 130% |
| Defensive | 20% | 170% |

The faction picks the owned castle with the best margin against the weakest
adjacent non-owned castle. A candidate must satisfy:

```
source.garrison * 100 >= target_effective_defense * strength_ratio
```

Effective defense:

```
garrison + defense * 2 + level * 5 + auto_defense_bonus + 20 if wall
```

### 5.1 Officer assignment

At the start of each AI faction's monthly turn (before economy, recruitment,
construction, and attack), `StrategyManager::process_ai_officer_assignment()`
appoints a lord to every faction-owned castle that still lacks one
(`lord_officer_id == 0`). Officers are drawn from the pool of officers not yet
assigned as lord anywhere (`CastleCommand::get_available_officers()`), so no
officer serves two castles at once. A newly appointed lord immediately grants
the `+2` lord-presence loyalty bonus during that month's economy resolution.

The current implementation picks the first available officer deterministically.
A future refinement can match officer attributes (loyalty, command, warfare)
and regional origin to castle needs.

### 5.2 Diplomacy

A symmetric diplomacy matrix tracks relations between all factions:
`Neutral` (default), `Alliance`, or `War`. The API is exposed through
`StrategyManager`:

- `get_relation(a, b)`, `is_at_war(a, b)`, `is_allied(a, b)`
- `declare_war(a, b, date)`, `form_alliance(a, b, date)`, `break_alliance(a, b, date)`

Relations are symmetric and logged as `DiplomacyDeclaredWar`,
`DiplomacyAlliance`, or `DiplomacyBrokeAlliance` events. During AI attack
selection (`weakest_adjacent_enemy`), castles owned by allied factions are
excluded from valid targets. A faction at war is not yet prioritised; that
remains a future refinement.

### 5.3 Abstract combat

At the start of each AI faction's monthly turn (before economy, recruitment,
construction, and attack), `StrategyManager::process_ai_officer_assignment()`
appoints a lord to every faction-owned castle that still lacks one
(`lord_officer_id == 0`). Officers are drawn from the pool of officers not yet
assigned as lord anywhere (`CastleCommand::get_available_officers()`), so no
officer serves two castles at once. A newly appointed lord immediately grants
the `+2` lord-presence loyalty bonus during that month's economy resolution.

The current implementation picks the first available officer deterministically.
A future refinement can match officer attributes (loyalty, command, warfare)
and regional origin to castle needs.

### 5.2 Abstract combat

```
attack_power  = source.garrison + source.level * 5 + random(0..19)
defense_power = effective_defense(target) + random(0..19)
```

If `attack_power > defense_power`, the castle is captured:

- half of the target's gold is looted;
- the attacker loses half its garrison;
- an occupying force of `clamp(source.garrison / 3, 5, max_garrison)` is
  installed;
- loyalty is reset to `40`, unrest to `20`, and the lord is cleared.

Otherwise the attack is repelled: the attacker loses a quarter of its garrison
and the defender loses a fifth. Both outcomes are logged. Eliminating a
faction's last castle marks it dead and logs a faction-elimination event.

## 6. Victory and Defeat

- **Victory** — every castle has `owner_id == 1`.
- **Defeat** — the player owns zero castles.

Conditions are re-evaluated after each turn and after every ownership change
(player battle result, load, or `StrategyManager::rebuild()`). Once either flag
is set, **End Turn** is disabled and the map info panel shows `VICTORY!` or
`DEFEAT!`.

## 7. UI Integration

- `Map Info` panel: date, season, turn, player castle count, faction status.
- **End Turn (Next Month)**: resolves one month through `StrategyManager`.
- Ownership dot under each castle: green = player, red = AI faction,
  gray = independent.
- Castle tooltip and detail panel show the owning faction name, loyalty, and
  unrest.
- `Strategy Log` panel (bottom left) shows the last eight events with colors
  for captures, losses, eliminations, season changes, starvation, victory, and
  defeat. The log is capped at 200 entries.

## 8. Save Format (Version 2)

`GameSaveData::version` is now `2`. Version 2 adds:

```
turn_count=<int>
castle.owner_id=<int>
```

Version 1 saves still load. Their castles only carry `is_player_owned`, so
`StrategyManager::rebuild(..., migrate_legacy = true)` assigns each unowned
castle to `default_owner_for_region(region_id)`. Loading also clears the old
strategy log and syncs the turn counter and date.

## 9. Testing

```bash
./build/tensyoki --test-strategy
```

The headless suite covers ownership mapping, faction creation, player economy,
calendar and turn advancement, AI recruitment, season modifiers, region
adjacency, abstract capture and repelled attacks, faction elimination,
victory/defeat, and the version 2 save/load round-trip.

All other suites remain green:

```bash
./build/tensyoki --test-battle
./build/tensyoki --test-castle
./build/tensyoki --test-save
./build/tensyoki --test-officer
./build/tensyoki --check-resources
cd build && ctest
```

## 10. Known Simplifications

- Region assignment (`castle.index % 8`) is a placeholder. The sndata castle
  record is only 33 bytes and carries no explicit region field (verified
  across sndata1..6.dat), so no verified per-castle region table could be
  recovered from the scenario data yet; replace it when map/scenario data
  provides one.
- Tactical battles are only played when the player initiates them; all other
  combat is statistical.
- AI does not coordinate between factions, form alliances, or retreat.
- There is no tactical AI or campaign balancing yet.
