/**
 * bin_parser.hpp - Parser for Koei "LINK" .bin containers
 *
 * Layout as validated in docs/formats/link.md:
 *
 *   0x00  char[4]  magic       "LINK"
 *   0x04  u32      entry_count number of table entries
 *   0x08  u32      version     1 in all six known containers under res/
 *   0x0C  u32      reserved    0 in all known files
 *   0x10  entries  entry_count * { u32 absolute_offset, u32 size }
 *
 * Payloads follow the table. Each non-empty payload is followed by a 4-byte
 * sentinel (little-endian 0x89ABCDEF). Offsets are absolute from file start.
 *
 * A payload may itself be a nested LINK container:
 *
 *   +0x00  char[4]  "LINK"
 *   +0x04  u32      version            1
 *   +0x08  u32      data_table_offset  0x20
 *   +0x0C  u32      record_count       number of 12-byte descriptor records
 *   +0x10  u32      descriptor_offset  0x40
 *   +0x20  u32 x2   data_offset, data_size (one table entry)
 *
 * Descriptor records are { u16 x, y, w, h; u32 texture_index }, all offsets
 * relative to the nested container start. The data section is usually a GT1G
 * texture block (see docs/formats/gt1g.md).
 */

#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <cstdint>
#include <span>
#include <optional>
#include <string_view>
#include <memory>
#include <utility>
#include <type_traits>
#include <stdexcept>
#include <fstream>
#include <variant>

namespace tensyoki {

// Forward declaration
class BinArchive;

/// A rectangle descriptor inside a nested LINK container (12 bytes on disk)
struct LinkRecord {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    uint32_t texture_index = 0;
};

/// One texture inside a GT1G block
struct Gt1gTexture {
    uint32_t index = 0;
    uint32_t header_offset = 0;  // relative to GT1G block start
    uint32_t data_offset = 0;    // relative to GT1G block start
    uint32_t data_size = 0;      // bytes
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t mip_info = 0;
    uint8_t format_id = 0;
    uint8_t size_log2 = 0;
};

/// Decoded GT1G ("GT1G0600") texture block
struct Gt1gInfo {
    uint32_t total_size = 0;
    uint32_t header_size = 0;
    uint32_t texture_count = 0;
    std::vector<Gt1gTexture> textures;
};

/// Decoded nested LINK container
struct NestedLinkInfo {
    uint32_t version = 0;
    uint32_t data_table_offset = 0;
    uint32_t record_count = 0;
    uint32_t descriptor_offset = 0;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
    std::vector<LinkRecord> records;
    std::optional<Gt1gInfo> textures;  // present when the data section is GT1G
};

/// A single payload entry in a .bin container
struct BinFileEntry {
    std::string name;           // Generated name, e.g. "entry_0000"
    uint32_t index = 0;         // Position in the table
    uint32_t offset = 0;        // Absolute offset from file start
    uint32_t size = 0;          // Payload size in bytes (0 = empty slot)
    bool has_sentinel = false;  // Trailing 0x89ABCDEF marker present
    bool is_nested_link = false;  // Payload begins with a nested "LINK"
    std::string signature;      // Sniffed magic, e.g. "LINK", "LS11", "DBUG"

    bool empty() const noexcept { return size == 0; }

    /// Guess the file extension from the sniffed signature
    std::string guess_extension() const;
};

/// Parse error type
enum class BinParseError {
    FileNotFound,
    FileTooSmall,
    InvalidMagic,
    InvalidVersion,
    CorruptedIndex,
    CorruptedData,
    NotANestedLink,
    CorruptedNestedTable,
    CorruptedTextureBlock,
    IOError,
    UnsupportedFormat
};

/// Convert error to string
constexpr std::string_view to_string(BinParseError err) noexcept {
    using E = BinParseError;
    switch (err) {
        case E::FileNotFound:        return "File not found";
        case E::FileTooSmall:        return "File too small for valid header";
        case E::InvalidMagic:        return "Invalid magic number (expected 'LINK')";
        case E::InvalidVersion:      return "Unsupported format version";
        case E::CorruptedIndex:      return "Corrupted entry table";
        case E::CorruptedData:       return "Corrupted payload";
        case E::NotANestedLink:      return "Payload is not a nested LINK container";
        case E::CorruptedNestedTable:return "Corrupted nested LINK table";
        case E::CorruptedTextureBlock:return "Corrupted GT1G texture block";
        case E::IOError:             return "I/O error";
        case E::UnsupportedFormat:   return "Unsupported format variant";
    }
    return "Unknown error";
}

/// C++20-compatible Result type (similar to std::expected)
/// Uses std::variant to handle the void case (std::monostate in place of void)
template<typename T, typename E>
class Result {
public:
    // Disable default construction
    Result() = delete;

    // Success construction
    template<typename U = T>
    constexpr Result(U&& value) requires (!std::is_void_v<U>)
        : storage_(std::in_place_index<0>, std::forward<U>(value)) {}

    // Error construction
    constexpr Result(E&& error) : storage_(std::in_place_index<1>, std::move(error)) {}
    constexpr Result(const E& error) : storage_(std::in_place_index<1>, error) {}

    // Specialized construction for the void type
    template<typename U = T>
    constexpr Result(std::nullptr_t) requires (std::is_void_v<U>)
        : storage_(std::in_place_index<0>, std::monostate{}) {}

    // Move construction/assignment
    constexpr Result(Result&& other) noexcept = default;
    constexpr Result& operator=(Result&& other) noexcept = default;

    // Disable copy
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    // State queries
    constexpr explicit operator bool() const noexcept { return storage_.index() == 0; }
    constexpr bool has_value() const noexcept { return storage_.index() == 0; }
    constexpr bool has_error() const noexcept { return storage_.index() == 1; }

    // Value access
    template<typename U = T>
    constexpr U& value() & requires (!std::is_void_v<U>) {
        if (!has_value()) throw std::runtime_error("Result has no value");
        return std::get<0>(storage_);
    }
    template<typename U = T>
    constexpr const U& value() const& requires (!std::is_void_v<U>) {
        if (!has_value()) throw std::runtime_error("Result has no value");
        return std::get<0>(storage_);
    }
    template<typename U = T>
    constexpr U&& value() && requires (!std::is_void_v<U>) {
        if (!has_value()) throw std::runtime_error("Result has no value");
        return std::move(std::get<0>(storage_));
    }

    // value() for the void type
    template<typename U = T>
    constexpr void value() requires (std::is_void_v<U>) {
        if (!has_value()) throw std::runtime_error("Result has no value");
    }
    template<typename U = T>
    constexpr void value() const requires (std::is_void_v<U>) {
        if (!has_value()) throw std::runtime_error("Result has no value");
    }

    // Error access
    constexpr E& error() & {
        if (has_value()) throw std::runtime_error("Result has no error");
        return std::get<1>(storage_);
    }
    constexpr const E& error() const& {
        if (has_value()) throw std::runtime_error("Result has no error");
        return std::get<1>(storage_);
    }

    // Operator overloads (non-void only)
    template<typename U = T>
    constexpr U* operator->() & requires (!std::is_void_v<U>) { return has_value() ? &std::get<0>(storage_) : nullptr; }
    template<typename U = T>
    constexpr const U* operator->() const requires (!std::is_void_v<U>) { return has_value() ? &std::get<0>(storage_) : nullptr; }

    template<typename U = T>
    constexpr U& operator*() & requires (!std::is_void_v<U>) { return value(); }
    template<typename U = T>
    constexpr const U& operator*() const& requires (!std::is_void_v<U>) { return value(); }

    // Error construction helpers
    static constexpr Result success() requires (std::is_void_v<T>) { return Result(nullptr); }
    template<typename U = T>
    static constexpr Result success(U&& val) requires (!std::is_void_v<U>) { return Result(std::forward<U>(val)); }
    static constexpr Result failure(E&& err) { return Result(std::move(err)); }
    static constexpr Result failure(const E& err) { return Result(err); }

private:
    // variant index 0 = value (T or monostate for void), index 1 = error
    std::variant<std::conditional_t<std::is_void_v<T>, std::monostate, T>, E> storage_;
};

/// Result type alias
template<typename T>
using BinResult = Result<T, BinParseError>;

/// Main .bin container class
class BinArchive {
public:
    BinArchive() = default;
    ~BinArchive() = default;

    // Disable copy (large data), allow move
    BinArchive(const BinArchive&) = delete;
    BinArchive& operator=(const BinArchive&) = delete;
    BinArchive(BinArchive&&) = default;
    BinArchive& operator=(BinArchive&&) = default;

    /// Load and parse from a file
    BinResult<void> load_from_file(const std::filesystem::path& path);

    /// Load from a memory buffer (copied into the archive)
    BinResult<void> load_from_memory(std::span<const uint8_t> data);

    /// Get all file entries
    const std::vector<BinFileEntry>& entries() const noexcept { return entries_; }

    /// Find an entry by generated name
    const BinFileEntry* find_entry(std::string_view name) const noexcept;

    /// Find an entry by index
    const BinFileEntry* find_by_index(uint32_t index) const noexcept;

    /// Extract file data into a vector
    BinResult<std::vector<uint8_t>> extract(const BinFileEntry& entry) const;

    /// Extract file data into a preallocated buffer
    BinResult<void> extract_to(const BinFileEntry& entry, std::vector<uint8_t>& out_buffer) const;

    /// Get a read-only view of file data (zero-copy; archive must stay alive)
    BinResult<std::span<const uint8_t>> view(const BinFileEntry& entry) const;

    /// Decode a nested LINK container and, if present, its GT1G texture block
    BinResult<NestedLinkInfo> parse_nested(const BinFileEntry& entry) const;

    /// Export all files to a directory
    BinResult<void> extract_all(const std::filesystem::path& output_dir) const;

    /// Get a container information summary
    struct Info {
        std::string source_path;
        uint32_t version = 0;
        uint32_t file_count = 0;
        uint64_t total_size = 0;
        uint64_t table_offset = 0;
        uint64_t data_offset = 0;
        uint64_t sentinel_count = 0;
    };
    Info info() const noexcept;

    /// Clear state
    void clear() noexcept;

    /// On-disk structures
    #pragma pack(push, 1)
    struct Header {
        char magic[4];        // "LINK"
        uint32_t entry_count; // number of table entries
        uint32_t version;     // 1 in all known files
        uint32_t reserved;    // 0 in all known files
    };

    struct TableEntry {
        uint32_t offset;      // absolute offset from file start
        uint32_t size;        // payload size in bytes
    };
    #pragma pack(pop)

    static_assert(sizeof(Header) == 16, "LINK header must be 16 bytes");
    static_assert(sizeof(TableEntry) == 8, "LINK table entry must be 8 bytes");

    static constexpr uint32_t MAGIC_LINK = 0x4B4E494C;   // "LINK" little-endian
    static constexpr uint32_t SENTINEL = 0x89ABCDEF;     // payload terminator
    static constexpr uint32_t SUPPORTED_VERSION = 1;
    static constexpr uint32_t MAX_ENTRIES = 100000;
    static constexpr size_t HEADER_SIZE = sizeof(Header);
    static constexpr size_t TABLE_ENTRY_SIZE = sizeof(TableEntry);
    static constexpr size_t SENTINEL_SIZE = 4;

    // Nested LINK layout
    static constexpr size_t NESTED_DATA_TABLE_OFFSET = 0x20;
    static constexpr size_t NESTED_DESCRIPTOR_OFFSET = 0x40;
    static constexpr size_t NESTED_RECORD_SIZE = 12;

    // GT1G layout
    static constexpr size_t GT1G_FIXED_HEADER_SIZE = 0x1C;
    static constexpr size_t GT1G_TEXTURE_HEADER_SIZE = 20;

    /// Sniff a short signature from payload bytes
    static std::string sniff_signature(std::span<const uint8_t> data);

    // Internal parsing methods
    BinResult<void> parse_owned(std::vector<uint8_t>&& data);
    BinResult<void> parse_from_span(std::span<const uint8_t> data);
    BinResult<void> parse_header(std::span<const uint8_t> data);
    BinResult<void> parse_table(std::span<const uint8_t> data);
    BinResult<void> validate_entries();
    BinResult<Gt1gInfo> parse_gt1g(std::span<const uint8_t> block) const;

    // Utility methods
    static uint32_t read_u32_le(std::span<const uint8_t> data, size_t offset);
    static uint16_t read_u16_le(std::span<const uint8_t> data, size_t offset);

private:
    std::filesystem::path source_path_;
    std::vector<uint8_t> file_data_;           // Complete file contents
    std::vector<BinFileEntry> entries_;        // Parsed entry list
    Header header_{};
    bool loaded_ = false;
};

/// Convenience function: quickly check whether the file starts with "LINK"
inline BinResult<bool> is_valid_bin_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return BinResult<bool>::failure(BinParseError::FileNotFound);
    }
    if (std::filesystem::file_size(path) < BinArchive::HEADER_SIZE) {
        return BinResult<bool>::failure(BinParseError::FileTooSmall);
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return BinResult<bool>::failure(BinParseError::IOError);
    }
    char magic[4]{};
    file.read(magic, 4);
    if (std::string_view(magic, 4) != "LINK") {
        return BinResult<bool>::failure(BinParseError::InvalidMagic);
    }
    return BinResult<bool>::success(true);
}

} // namespace tensyoki
