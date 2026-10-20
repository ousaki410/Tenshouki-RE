/**
 * dat_parser.hpp - Generic Koei game .DAT struct-array data parser
 * 
 * Suitable for DAT files containing fixed-size struct arrays such as officers, castles, and items.
 */

#pragma once

#include "bin_parser.hpp"
#include "logging.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <span>
#include <optional>
#include <algorithm>
#include <type_traits>

namespace tensyoki {

/// DAT parse error type (reuses or extends BinParseError)
enum class DatParseError {
    FileNotFound,
    FileTooSmall,
    InvalidAlignment,
    OutOfRange,
    IOError,
    CorruptedData
};

constexpr std::string_view to_string(DatParseError err) noexcept {
    using E = DatParseError;
    switch (err) {
        case E::FileNotFound:     return "DAT file not found";
        case E::FileTooSmall:     return "DAT file too small for record structure";
        case E::InvalidAlignment: return "DAT file size does not align with record size";
        case E::OutOfRange:       return "Record index out of range";
        case E::IOError:          return "DAT I/O error";
        case E::CorruptedData:    return "Corrupted DAT data";
    }
    return "Unknown DAT error";
}

template<typename T>
using DatResult = Result<T, DatParseError>;

/// Example officer record struct (64-byte aligned)
#pragma pack(push, 1)
struct OfficerRecord {
    uint16_t id;              // Internal officer ID
    uint16_t portrait_id;     // Portrait ID
    uint8_t  status;          // Status (ronin, in service, dead, etc.)
    uint8_t  loyalty;         // Loyalty
    uint8_t  leadership;      // Leadership
    uint8_t  warfare;         // Warfare
    uint8_t  wisdom;          // Wisdom
    uint8_t  politics;        // Politics
    uint8_t  ambition;        // Ambition
    uint8_t  charm;           // Charm
    char     name[20];        // Name (fixed length, possibly Shift-JIS encoded)
    char     kana[20];        // Kana / courtesy name
    uint8_t  reserved[12];    // Reserved fields to pad to 64 bytes
};
#pragma pack(pop)

static_assert(sizeof(OfficerRecord) == 64, "OfficerRecord size must be exactly 64 bytes");

/// Generic DAT struct-array container parser
template<typename RecordType>
class DatArchive {
    static_assert(std::is_trivially_copyable_v<RecordType>, "RecordType must be trivially copyable");
    static_assert(std::is_standard_layout_v<RecordType>, "RecordType must be standard layout");

public:
    DatArchive() = default;
    ~DatArchive() = default;

    DatArchive(const DatArchive&) = delete;
    DatArchive& operator=(const DatArchive&) = delete;
    DatArchive(DatArchive&&) = default;
    DatArchive& operator=(DatArchive&&) = default;

    /// Load and parse from a memory span
    DatResult<void> load_from_memory(std::span<const uint8_t> data) {
        clear();

        if (data.empty()) {
            LOG_ERROR("DAT data span is empty");
            return DatResult<void>::failure(DatParseError::FileTooSmall);
        }

        constexpr size_t record_size = sizeof(RecordType);
        if (data.size() < record_size) {
            LOG_ERROR("DAT data too small: {} bytes (need at least {})", data.size(), record_size);
            return DatResult<void>::failure(DatParseError::FileTooSmall);
        }

        if (data.size() % record_size != 0) {
            LOG_WARN("DAT file size ({} bytes) is not a multiple of record size ({} bytes). Extra trailing bytes will be ignored.",
                     data.size(), record_size);
        }

        size_t count = data.size() / record_size;
        raw_data_.assign(data.begin(), data.end());
        
        // Build the record view
        records_ = std::span<const RecordType>(
            reinterpret_cast<const RecordType*>(raw_data_.data()), 
            count
        );

        LOG_INFO("Successfully loaded {} records of size {} bytes (total {} bytes)",
                 count, record_size, raw_data_.size());
        return DatResult<void>::success();
    }

    /// Load from a specific entry of a BinArchive
    DatResult<void> load_from_archive(const BinArchive& archive, const BinFileEntry& entry) {
        auto view_result = archive.view(entry);
        if (!view_result) {
            LOG_ERROR("Failed to view entry '{}' from archive for DAT loading", entry.name);
            return DatResult<void>::failure(DatParseError::CorruptedData);
        }
        return load_from_memory(*view_result);
    }

    /// Get a read-only view of all records
    std::span<const RecordType> records() const noexcept { return records_; }

    /// Get the record count
    size_t count() const noexcept { return records_.size(); }

    /// Whether data is loaded
    bool is_loaded() const noexcept { return !records_.empty(); }

    /// Get a record by index
    DatResult<const RecordType*> get(size_t index) const noexcept {
        if (index >= records_.size()) {
            LOG_ERROR("DAT record index out of range: {} >= {}", index, records_.size());
            return DatResult<const RecordType*>::failure(DatParseError::OutOfRange);
        }
        return DatResult<const RecordType*>::success(&records_[index]);
    }

    /// Look up a record by ID (assumes RecordType has an id field)
    template<typename T = RecordType>
    auto find_by_id(uint16_t id) const -> std::optional<RecordType> requires requires(T t) { t.id; } {
        auto it = std::find_if(records_.begin(), records_.end(), [id](const RecordType& r) {
            return r.id == id;
        });
        if (it != records_.end()) {
            return *it;
        }
        return std::nullopt;
    }

    /// Clear data
    void clear() noexcept {
        raw_data_.clear();
        records_ = {};
    }

private:
    std::vector<uint8_t> raw_data_;
    std::span<const RecordType> records_;
};

} // namespace tensyoki
