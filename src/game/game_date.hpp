/**
 * game_date.hpp - In-game date and season system
 *
 * Tracks year/month/day for the strategic layer. KOEI games typically use
 * a lunar-ish calendar with 12 months and 4 seasons.
 *
 * Provides automatic date advancement with adjustable speed (pause, 1x,
 * 2x, 4x, 8x) and callbacks on month/year change.
 */

#pragma once

#include <cstdint>
#include <string>
#include <functional>

namespace tensyoki {

enum class Season {
    Spring,
    Summer,
    Autumn,
    Winter,
};

/// Game date: year, month (1-12), day (1-30 approx)
struct GameDate {
    int16_t year = 1;       // Game year (e.g. Tensho era)
    uint8_t month = 1;      // 1..12
    uint8_t day = 1;        // 1..30

    Season season() const;
    std::string season_name() const;
    std::string month_name() const;
    std::string to_string() const;

    /// Advance by one day
    void advance_day();

    /// Advance by one month
    void advance_month();

    /// Callback invoked when month changes
    std::function<void(int16_t year, uint8_t month)> on_month_change;
    /// Callback invoked when year changes
    std::function<void(int16_t year)> on_year_change;
};

/// Game clock: controls date advancement speed and accumulated time
struct GameClock {
    // The campaign is turn-based: the date advances one month per turn, not
    // in real time. Set paused = false to enable the optional real-time clock.
    bool paused = true;
    float speed = 1.0f;           // Days per second (1.0 = 1 day/sec)
    float accumulator = 0.0f;  // Seconds accumulated since last day
    float seconds_per_day = 2.0f;  // Real seconds per game day

    /// Update clock, advancing date as needed. Returns number of days advanced.
    int update(float dt, GameDate& date);
};

} // namespace tensyoki
