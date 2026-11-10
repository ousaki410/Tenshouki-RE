/**
 * game_date.cpp - Game date and season implementation
 */

#include "game_date.hpp"
#include <string>

namespace tensyoki {

Season GameDate::season() const {
    // KOEI-style seasons: Spring(3-5), Summer(6-8), Autumn(9-11), Winter(12-2)
    if (month >= 3 && month <= 5) return Season::Spring;
    if (month >= 6 && month <= 8) return Season::Summer;
    if (month >= 9 && month <= 11) return Season::Autumn;
    return Season::Winter;
}

std::string GameDate::season_name() const {
    switch (season()) {
        case Season::Spring:  return "Spring";
        case Season::Summer:  return "Summer";
        case Season::Autumn:  return "Autumn";
        case Season::Winter:  return "Winter";
    }
    return "Unknown";
}

std::string GameDate::month_name() const {
    static const char* names[] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    if (month >= 1 && month <= 12) {
        return names[month];
    }
    return "?";
}

std::string GameDate::to_string() const {
    return std::to_string(year) + " " + month_name() + " " +
           std::to_string(day) + " (" + season_name() + ")";
}

void GameDate::advance_day() {
    ++day;
    // Simplified: each month has 30 days
    if (day > 30) {
        day = 1;
        advance_month();
    }
}

void GameDate::advance_month() {
    ++month;
    if (month > 12) {
        month = 1;
        ++year;
    }
}

} // namespace tensyoki
