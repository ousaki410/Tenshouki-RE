/**
 * game_date.hpp - In-game date and season system
 *
 * Tracks year/month/day for the strategic layer. KOEI games typically use
 * a lunar-ish calendar with 12 months and 4 seasons.
 */

#pragma once

#include <cstdint>
#include <string>

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
};

} // namespace tensyoki
