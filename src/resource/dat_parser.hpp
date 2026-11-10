/**
 * dat_parser.hpp - Koei game .DAT / scenario data parsers
 *
 * Provides:
 *   - DatArchive<T>: a generic fixed-size struct-array parser (for any DAT
 *     file whose records are a single repeated POD struct).
 *   - SndataArchive: a specialised parser for scenario/sndata1..6.dat, which
 *     lays out officer attributes (47 bytes/slot) and officer names
 *     (24 bytes/slot) in two separate blocks, plus a castle block
 *     (33 bytes/castle).
 *
 * All multi-byte integer fields are little-endian. Text fields are encoded
 * in CP932 (Shift-JIS variant); decode_cp932() converts them to UTF-8 via
 * iconv when available.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <optional>
#include <filesystem>
#include <system_error>
#include <type_traits>
#include <algorithm>

#include "bin_parser.hpp"   // for Result<>, BinParseError (reused pattern)
#include "logging.hpp"      // for LOG_ERROR / LOG_WARN / LOG_INFO in templates

namespace tensyoki {

/// Parse errors for the generic DAT archive
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

/// Example officer record struct (64-byte aligned, for generic DAT files).
/// Kept for reference; sndata.dat uses the 47-byte SndataAttrRecord below.
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
    template<typename U = RecordType>
    auto find_by_id(uint16_t id) const -> std::optional<U> requires requires(U u) { u.id; } {
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

// ---------------------------------------------------------------------------
// Sndata (scenario/sndata1..6.dat) specialised parser
// ---------------------------------------------------------------------------

/// Parse errors for sndata files
enum class SndataParseError {
    FileNotFound,
    FileTooSmall,
    CorruptedData,
    IOError
};

constexpr std::string_view to_string(SndataParseError err) noexcept {
    using E = SndataParseError;
    switch (err) {
        case E::FileNotFound:    return "Sndata file not found";
        case E::FileTooSmall:    return "Sndata file too small for expected layout";
        case E::CorruptedData:   return "Corrupted sndata data";
        case E::IOError:         return "Sndata I/O error";
    }
    return "Unknown sndata error";
}

template<typename T>
using SndataResult = Result<T, SndataParseError>;

#pragma pack(push, 1)
/// Officer attribute block record: 47 bytes per slot.
/// Layout (observed in sndata1..6.dat, base 0x0AE8, stride 47):
///   +0x00  u8  attr[18]   various officer attributes (leadership, warfare, …)
///   +0x12  u16 id         officer id; 0xFFFF marks an empty slot
///   +0x14  u8  pad[2]     unused / reserved
///   +0x16  u8  status     officer status
///   +0x17  u8  gender     officer gender
///   +0x18  u8  attr2[23]  remaining attribute bytes
struct SndataAttrRecord {
    uint8_t attr[18];
    uint16_t id;
    uint8_t pad[2];
    uint8_t status;
    uint8_t gender;
    uint8_t attr2[23];
};
static_assert(sizeof(SndataAttrRecord) == 47, "SndataAttrRecord must be 47 bytes");

/// Officer name block record: 24 bytes per slot.
///   +0x00  char[18] name  CP932-encoded name, null-padded
///   +0x12  u8  tail[6]    auxiliary bytes
struct SndataNameRecord {
    char name[18];
    uint8_t tail[6];
};
static_assert(sizeof(SndataNameRecord) == 24, "SndataNameRecord must be 24 bytes");

/// Castle record: 33 bytes per castle.
///   +0x00  u16 header[4]  four u16 fields (observed values vary per castle)
///   +0x08  char[9] name   CP932-encoded castle name, null-padded
///   +0x11  u8  tail[16]    auxiliary bytes
struct SndataCastleRecord {
    uint16_t header[4];
    char name[9];
    uint8_t tail[16];
};
static_assert(sizeof(SndataCastleRecord) == 33, "SndataCastleRecord must be 33 bytes");
#pragma pack(pop)

/// Decoded officer entry
struct Officer {
    uint32_t slot = 0;          // slot index (0-based)
    uint16_t id = 0;            // internal officer id (0xFFFF = empty)
    uint8_t status = 0;
    uint8_t gender = 0;
    std::string name_raw;       // CP932 bytes as hex (for inspection)
    std::string name_utf8;      // CP932 -> UTF-8 (best effort, may be empty)
    std::vector<uint8_t> attr;  // full 47-byte attribute record
};

/// Decoded castle entry
struct Castle {
    uint32_t index = 0;         // castle index (0-based)
    uint16_t header[4] = {0, 0, 0, 0};
    std::string name_raw;       // CP932 bytes as hex
    std::string name_utf8;      // CP932 -> UTF-8 (best effort, may be empty)
    std::vector<uint8_t> tail;  // 16-byte tail
    std::vector<uint8_t> raw;   // full 33-byte record
};

/// Decoded sndata scenario file
struct SndataArchive {
    uint32_t scenario = 0;      // scenario number (1..6)
    std::vector<Officer> officers;
    std::vector<Castle> castles;

    bool empty() const noexcept { return officers.empty() && castles.empty(); }
    size_t officer_count() const noexcept { return officers.size(); }
    size_t castle_count() const noexcept { return castles.size(); }
};

/**
 * Decode a CP932 (Shift-JIS) byte span to UTF-8.
 * Uses iconv when available; on failure returns a best-effort ASCII-only
 * rendering (printable ASCII passed through, everything else '?').
 */
std::string decode_cp932(std::span<const uint8_t> data);

/// Parse an sndata file from disk
SndataResult<SndataArchive> parse_sndata_file(const std::filesystem::path& path, uint32_t scenario);

/// Parse an sndata buffer in memory
SndataResult<SndataArchive> parse_sndata_memory(std::span<const uint8_t> data, uint32_t scenario);

// Sndata layout constants (observed in sndata1..6.dat)
inline constexpr uint32_t SNDATA_NSLOT = 532;
inline constexpr uint32_t SNDATA_ATTR_BASE = 0x0AE8;
inline constexpr uint32_t SNDATA_ATTR_STRIDE = 47;
inline constexpr uint32_t SNDATA_NAME_BASE = 0x6C94;
inline constexpr uint32_t SNDATA_NAME_STRIDE = 24;
inline constexpr uint32_t SNDATA_CASTLE_BASE = 0x9E74;
inline constexpr uint32_t SNDATA_CASTLE_STRIDE = 33;
inline constexpr uint32_t SNDATA_NCASTLE = 214;

} // namespace tensyoki
