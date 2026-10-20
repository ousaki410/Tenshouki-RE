/**
 * bin_parser.hpp - Generic Koei game .bin container parser
 * 
 * File format:
 * - Magic: "LINK" (4 bytes)
 * - Index Table: subfile count + one entry per file (offset, size, name hash, etc.)
 * - Data Blocks: actual file data
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
#include <stdexcept>
#include <fstream>
#include <variant>

namespace tensyoki {

// Forward declaration
class BinArchive;

/// A single file entry in a .bin container
struct BinFileEntry {
    std::string name;           // File name (if available)
    uint32_t name_hash = 0;     // File name hash
    uint32_t offset = 0;        // Offset relative to the start of the data area
    uint32_t size = 0;          // File size (bytes)
    uint32_t flags = 0;         // Flags (compression, encryption, etc.)
    uint32_t type = 0;          // Resource type
    
    /// Guess the file extension
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
    IOError,
    UnsupportedFormat
};

/// Convert error to string
constexpr std::string_view to_string(BinParseError err) noexcept {
    using E = BinParseError;
    switch (err) {
        case E::FileNotFound:    return "File not found";
        case E::FileTooSmall:    return "File too small for valid header";
        case E::InvalidMagic:    return "Invalid magic number (expected 'LINK')";
        case E::InvalidVersion:  return "Unsupported format version";
        case E::CorruptedIndex:  return "Corrupted index table";
        case E::CorruptedData:   return "Corrupted data block";
        case E::IOError:         return "I/O error";
        case E::UnsupportedFormat: return "Unsupported format variant";
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
    
    /// Load from a memory buffer
    BinResult<void> load_from_memory(std::span<const uint8_t> data);
    
    /// Get all file entries
    const std::vector<BinFileEntry>& entries() const noexcept { return entries_; }
    
    /// Find an entry by name
    const BinFileEntry* find_entry(std::string_view name) const noexcept;
    
    /// Find an entry by hash
    const BinFileEntry* find_entry_by_hash(uint32_t hash) const noexcept;
    
    /// Extract file data into a vector
    BinResult<std::vector<uint8_t>> extract(const BinFileEntry& entry) const;
    
    /// Extract file data into a preallocated buffer
    BinResult<void> extract_to(const BinFileEntry& entry, std::vector<uint8_t>& out_buffer) const;
    
    /// Get a read-only view of file data (zero-copy; archive must stay alive)
    BinResult<std::span<const uint8_t>> view(const BinFileEntry& entry) const;
    
    /// Export all files to a directory
    BinResult<void> extract_all(const std::filesystem::path& output_dir) const;
    
    /// Get a container information summary
    struct Info {
        std::string source_path;
        uint32_t version = 0;
        uint32_t file_count = 0;
        uint64_t total_size = 0;
        uint64_t index_table_offset = 0;
        uint64_t data_offset = 0;
    };
    Info info() const noexcept;
    
    /// Clear state
    void clear() noexcept;

    // Public header structures for external inspection
    #pragma pack(push, 1)
    struct Header {
        char magic[4];           // "LINK"
        uint32_t version;        // Version number
        uint32_t file_count;     // File count
        uint32_t index_offset;   // Index table offset (relative to file header)
        uint32_t index_size;     // Index table size
        uint32_t data_offset;    // Data area offset (relative to file header)
        uint32_t flags;          // Container flags
        uint32_t reserved[4];    // Reserved fields
    };
    
    struct IndexEntry {
        uint32_t name_hash;      // File name hash
        uint32_t offset;         // Offset relative to data area
        uint32_t size;           // File size
        uint32_t flags;          // Flags
        uint32_t type;           // Resource type
        uint32_t reserved;       // Reserved
    };
    #pragma pack(pop)
    
    static constexpr uint32_t MAGIC_LINK = 0x4B4E494C; // "LINK" little-endian
    static constexpr uint32_t SUPPORTED_VERSION_MIN = 1;
    static constexpr uint32_t SUPPORTED_VERSION_MAX = 3;
    
    // Internal parsing methods
    BinResult<void> parse_owned(std::vector<uint8_t>&& data);     // Parse taking ownership
    BinResult<void> parse_from_span(std::span<const uint8_t> data); // Parse from a span
    BinResult<void> parse_header(std::span<const uint8_t> data);
    BinResult<void> parse_index_table(std::span<const uint8_t> data);
    BinResult<void> validate_data_blocks() const;
    
    // Utility methods
    static uint32_t read_u32_le(std::span<const uint8_t> data, size_t offset);
    static void write_u32_le(std::vector<uint8_t>& data, size_t offset, uint32_t value);

private:
    std::filesystem::path source_path_;
    std::vector<uint8_t> file_data_;           // Complete file contents (memory-mapped or read in)
    std::vector<BinFileEntry> entries_;        // Parsed file list
    Header header_{};
    bool loaded_ = false;
};

/// Convenience function: quickly check whether the file is a valid .bin file
inline BinResult<bool> is_valid_bin_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return BinResult<bool>::failure(BinParseError::FileNotFound);
    }
    if (std::filesystem::file_size(path) < sizeof(BinArchive::Header)) {
        return BinResult<bool>::failure(BinParseError::FileTooSmall);
    }
    
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return BinResult<bool>::failure(BinParseError::IOError);
    }
    char magic[4];
    file.read(magic, 4);
    if (std::string_view(magic, 4) != "LINK") {
        return BinResult<bool>::failure(BinParseError::InvalidMagic);
    }
    return BinResult<bool>::success(true);
}

} // namespace tensyoki