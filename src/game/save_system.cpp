/**
 * save_system.cpp - Save/load system implementation
 *
 * Simple text-based save/load format (key=value pairs with sections).
 * No external dependencies required.
 */

#include "save_system.hpp"
#include "core/logging.hpp"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace tensyoki {

namespace {

// Older saves (produced before the decode_cp932 NUL-compaction fix) may
// contain embedded NUL bytes inside name_utf8 / lord_name_utf8. Strip them
// on load so UI rendering never truncates a name at an embedded NUL.
std::string strip_embedded_nulls(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    return s;
}

}  // namespace

void SaveSystem::init(const std::string& save_dir) {
    save_dir_ = save_dir;
    ensure_dir();
    LOG_INFO("SaveSystem: initialized with directory {}", save_dir);
}

bool SaveSystem::ensure_dir() const {
    std::error_code ec;
    std::filesystem::create_directories(save_dir_, ec);
    return !ec;
}

std::string SaveSystem::get_save_path(int slot) const {
    return save_dir_ + "/save_" + std::to_string(slot) + ".txt";
}

bool SaveSystem::slot_exists(int slot) const {
    return std::filesystem::exists(get_save_path(slot));
}

std::vector<SaveSlotInfo> SaveSystem::list_saves() const {
    std::vector<SaveSlotInfo> slots;

    for (int slot = 1; slot <= 10; ++slot) {
        if (!slot_exists(slot)) continue;

        SaveSlotInfo info;
        info.slot = slot;
        info.is_valid = true;

        // Read save file to extract metadata
        std::ifstream file(get_save_path(slot));
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.find("save_name=") == 0) {
                    info.save_name = line.substr(10);
                } else if (line.find("date_year=") == 0) {
                    info.date_year = std::stoi(line.substr(10));
                } else if (line.find("date_month=") == 0) {
                    info.date_month = std::stoi(line.substr(11));
                } else if (line.find("date_day=") == 0) {
                    info.date_day = std::stoi(line.substr(9));
                } else if (line.find("player_castle_count=") == 0) {
                    info.player_castle_count = std::stoi(line.substr(20));
                } else if (line.find("total_castles=") == 0) {
                    info.total_castles = std::stoi(line.substr(14));
                }
            }
            file.close();
        }

        // Build date string
        info.date_string = std::to_string(info.date_year) + "年" +
                          std::to_string(info.date_month) + "月" +
                          std::to_string(info.date_day) + "日";

        slots.push_back(info);
    }

    // Sort by slot number (already sorted since we iterate 1-10)
    return slots;
}

bool SaveSystem::save(int slot, const GameSaveData& data) {
    if (slot < 1 || slot > 10) {
        LOG_ERROR("SaveSystem: invalid slot {}", slot);
        return false;
    }

    ensure_dir();

    std::string content = serialize(data);
    std::ofstream file(get_save_path(slot));
    if (!file.is_open()) {
        LOG_ERROR("SaveSystem: failed to open save file for slot {}", slot);
        return false;
    }

    file << content;
    file.close();

    LOG_INFO("SaveSystem: saved game to slot {}", slot);
    return true;
}

bool SaveSystem::load(int slot, GameSaveData& data) {
    if (slot < 1 || slot > 10) {
        LOG_ERROR("SaveSystem: invalid slot {}", slot);
        return false;
    }

    if (!slot_exists(slot)) {
        LOG_ERROR("SaveSystem: slot {} does not exist", slot);
        return false;
    }

    std::ifstream file(get_save_path(slot));
    if (!file.is_open()) {
        LOG_ERROR("SaveSystem: failed to open save file for slot {}", slot);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    if (!deserialize(buffer.str(), data)) {
        LOG_ERROR("SaveSystem: failed to deserialize save slot {}", slot);
        return false;
    }

    LOG_INFO("SaveSystem: loaded game from slot {}", slot);
    return true;
}

bool SaveSystem::delete_save(int slot) {
    if (slot < 1 || slot > 10) {
        LOG_ERROR("SaveSystem: invalid slot {}", slot);
        return false;
    }

    if (!slot_exists(slot)) {
        LOG_WARN("SaveSystem: slot {} does not exist", slot);
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(get_save_path(slot), ec);
    if (ec) {
        LOG_ERROR("SaveSystem: failed to delete save slot {}: {}", slot, ec.message());
        return false;
    }

    LOG_INFO("SaveSystem: deleted save slot {}", slot);
    return true;
}

std::string SaveSystem::serialize(const GameSaveData& data) const {
    std::ostringstream ss;

    ss << "version=" << data.version << "\n";
    ss << "date_year=" << data.date_year << "\n";
    ss << "date_month=" << data.date_month << "\n";
    ss << "date_day=" << data.date_day << "\n";
    ss << "save_name=" << data.save_name << "\n";
    ss << "turn_count=" << data.turn_count << "\n";
    ss << "player_castle_count=" << data.player_castle_count << "\n";
    ss << "total_castles=" << data.total_castles << "\n";

    // Castle data
    ss << "castle_count=" << data.castles.size() << "\n";
    for (const auto& castle : data.castles) {
        ss << "castle.index=" << castle.index << "\n";
        ss << "castle.name=" << castle.name << "\n";
        ss << "castle.name_utf8=" << castle.name_utf8 << "\n";
        ss << "castle.level=" << castle.level << "\n";
        ss << "castle.defense=" << castle.defense << "\n";
        ss << "castle.max_defense=" << castle.max_defense << "\n";
        ss << "castle.garrison=" << castle.garrison << "\n";
        ss << "castle.max_garrison=" << castle.max_garrison << "\n";
        ss << "castle.gold=" << castle.gold << "\n";
        ss << "castle.food=" << castle.food << "\n";
        ss << "castle.has_wall=" << (castle.has_wall ? 1 : 0) << "\n";
        ss << "castle.has_barracks=" << (castle.has_barracks ? 1 : 0) << "\n";
        ss << "castle.has_storehouse=" << (castle.has_storehouse ? 1 : 0) << "\n";
        ss << "castle.has_market=" << (castle.has_market ? 1 : 0) << "\n";
        ss << "castle.lord_officer_id=" << castle.lord_officer_id << "\n";
        ss << "castle.lord_name=" << castle.lord_name << "\n";
        ss << "castle.lord_name_utf8=" << castle.lord_name_utf8 << "\n";
        ss << "castle.region_id=" << castle.region_id << "\n";
        ss << "castle.is_player_owned=" << (castle.is_player_owned ? 1 : 0) << "\n";
        ss << "castle.owner_id=" << castle.owner_id << "\n";
        ss << "castle.is_capital=" << (castle.is_capital ? 1 : 0) << "\n";
        ss << "castle.auto_defense_bonus=" << castle.auto_defense_bonus << "\n";
        ss << "castle.loyalty=" << castle.loyalty << "\n";
        ss << "castle.tax_rate=" << castle.tax_rate << "\n";
        ss << "castle.unrest=" << castle.unrest << "\n";
        ss << "castle.end\n";
    }

    ss << "end\n";
    return ss.str();
}

bool SaveSystem::deserialize(const std::string& json, GameSaveData& data) const {
    std::istringstream ss(json);
    std::string line;

    while (std::getline(ss, line)) {
        // Skip empty lines
        if (line.empty()) continue;

        if (line == "end") break;

        try {
            if (line.find("version=") == 0) {
                data.version = std::stoi(line.substr(8));
            } else if (line.find("date_year=") == 0) {
                data.date_year = std::stoi(line.substr(10));
            } else if (line.find("date_month=") == 0) {
                data.date_month = std::stoi(line.substr(11));
            } else if (line.find("date_day=") == 0) {
                data.date_day = std::stoi(line.substr(9));
            } else if (line.find("save_name=") == 0) {
                data.save_name = line.substr(10);
            } else if (line.find("turn_count=") == 0) {
                data.turn_count = std::stoi(line.substr(11));
            } else if (line.find("player_castle_count=") == 0) {
                data.player_castle_count = std::stoi(line.substr(20));
            } else if (line.find("total_castles=") == 0) {
                data.total_castles = std::stoi(line.substr(14));
            } else if (line.find("castle_count=") == 0) {
                int32_t count = std::stoi(line.substr(13));
                data.castles.clear();
                data.castles.reserve(count);
            } else if (line.find("castle.index=") == 0) {
                CastleData cd;
                cd.index = std::stoi(line.substr(13));
                data.castles.push_back(cd);
            } else if (line.find("castle.name=") == 0) {
                if (!data.castles.empty()) {
                    if (line.size() > 12 && line[12] != '_') {
                        data.castles.back().name = line.substr(12);
                    }
                }
            } else if (line.find("castle.name_utf8=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().name_utf8 =
                        strip_embedded_nulls(line.substr(17));
                }
            } else if (line.find("castle.level=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().level = std::stoi(line.substr(13));
                }
            } else if (line.find("castle.defense=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().defense = std::stoi(line.substr(15));
                }
            } else if (line.find("castle.max_defense=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().max_defense = std::stoi(line.substr(19));
                }
            } else if (line.find("castle.garrison=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().garrison = std::stoi(line.substr(16));
                }
            } else if (line.find("castle.max_garrison=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().max_garrison = std::stoi(line.substr(20));
                }
            } else if (line.find("castle.gold=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().gold = std::stoi(line.substr(12));
                }
            } else if (line.find("castle.food=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().food = std::stoi(line.substr(12));
                }
            } else if (line.find("castle.has_wall=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().has_wall = (line.substr(16) == "1");
                }
            } else if (line.find("castle.has_barracks=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().has_barracks = (line.substr(20) == "1");
                }
            } else if (line.find("castle.has_storehouse=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().has_storehouse = (line.substr(22) == "1");
                }
            } else if (line.find("castle.has_market=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().has_market = (line.substr(18) == "1");
                }
            } else if (line.find("castle.lord_officer_id=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().lord_officer_id = std::stoi(line.substr(23));
                }
            } else if (line.find("castle.lord_name=") == 0) {
                if (!data.castles.empty()) {
                    if (line.size() > 17 && line[17] != '_') {
                        data.castles.back().lord_name = line.substr(17);
                    }
                }
            } else if (line.find("castle.lord_name_utf8=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().lord_name_utf8 =
                        strip_embedded_nulls(line.substr(22));
                }
            } else if (line.find("castle.region_id=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().region_id = std::stoi(line.substr(17));
                }
            } else if (line.find("castle.is_player_owned=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().is_player_owned = (line.substr(23) == "1");
                }
            } else if (line.find("castle.owner_id=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().owner_id =
                        static_cast<uint32_t>(std::stoi(line.substr(16)));
                }
            } else if (line.find("castle.is_capital=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().is_capital = (line.substr(18) == "1");
                }
            } else if (line.find("castle.auto_defense_bonus=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().auto_defense_bonus = std::stoi(line.substr(26));
                }
            } else if (line.find("castle.loyalty=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().loyalty = std::stoi(line.substr(15));
                }
            } else if (line.find("castle.tax_rate=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().tax_rate = std::stoi(line.substr(16));
                }
            } else if (line.find("castle.unrest=") == 0) {
                if (!data.castles.empty()) {
                    data.castles.back().unrest = std::stoi(line.substr(14));
                }
            } else if (line.find("castle.end") == 0) {
                // End of castle entry
            }
        } catch (const std::exception& e) {
            LOG_ERROR("SaveSystem: failed to parse line '{}': {}", line, e.what());
            return false;
        }
    }

    return true;
}

} // namespace tensyoki