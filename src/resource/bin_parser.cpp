/**
 * bin_parser.cpp - Implementation of the generic Koei game .bin container parser
 */

#include "bin_parser.hpp"
#include "logging.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <system_error>

namespace tensyoki {

// Little-endian read helpers
uint32_t BinArchive::read_u32_le(std::span<const uint8_t> data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void BinArchive::write_u32_le(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    if (offset + 4 > data.size()) return;
    data[offset]     = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

// Parse helper: take ownership and parse. Note that clear() resets source_path_, so the caller must set it afterwards on success.
BinResult<void> BinArchive::parse_owned(std::vector<uint8_t>&& data) {
    // Clear first
    clear();
    
    // Take ownership
    file_data_ = std::move(data);
    
    // Parse
    auto result = parse_from_span(file_data_);
    if (!result) {
        clear();
    }
    return result;
}

// Parse helper: parse from a span without taking ownership
BinResult<void> BinArchive::parse_from_span(std::span<const uint8_t> data) {
    // 1. Parse header
    auto header_result = parse_header(data);
    if (!header_result) return header_result;
    
    // 2. Parse index table
    auto index_span = data.subspan(header_.index_offset, header_.index_size);
    auto index_result = parse_index_table(index_span);
    if (!index_result) return index_result;
    
    // 3. Validate data blocks
    auto validate_result = validate_data_blocks();
    if (!validate_result) return validate_result;
    
    loaded_ = true;
    LOG_INFO("Successfully loaded BIN archive: {} files, version {}", entries_.size(), header_.version);
    return BinResult<void>::success();
}

BinResult<void> BinArchive::load_from_file(const std::filesystem::path& path) {
    LOG_INFO("Loading BIN archive: {}", path.string());
    
    // Check that the file exists
    if (!std::filesystem::exists(path)) {
        LOG_ERROR("File not found: {}", path.string());
        return BinResult<void>::failure(BinParseError::FileNotFound);
    }
    
    // Get file size
    auto file_size = std::filesystem::file_size(path);
    if (file_size < sizeof(Header)) {
        LOG_ERROR("File too small ({} bytes, min {}): {}", file_size, sizeof(Header), path.string());
        return BinResult<void>::failure(BinParseError::FileTooSmall);
    }
    
    // Read the file into a local buffer
    std::vector<uint8_t> file_data;
    file_data.resize(file_size);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open file: {}", path.string());
        return BinResult<void>::failure(BinParseError::IOError);
    }
    
    file.read(reinterpret_cast<char*>(file_data.data()), static_cast<std::streamsize>(file_size));
    if (!file) {
        LOG_ERROR("Failed to read file: {}", path.string());
        return BinResult<void>::failure(BinParseError::IOError);
    }
    
    // Parse and take ownership; source_path_ must be set after parse_owned()
    auto result = parse_owned(std::move(file_data));
    if (result) {
        source_path_ = path;
    }
    return result;
}

BinResult<void> BinArchive::load_from_memory(std::span<const uint8_t> data) {
    // Parse external data without taking ownership (copied into the internal buffer to support view())
    clear();
    file_data_.assign(data.begin(), data.end());
    auto result = parse_from_span(file_data_);
    if (!result) {
        clear();
    }
    return result;
}

BinResult<void> BinArchive::parse_header(std::span<const uint8_t> data) {
    if (data.size() < sizeof(Header)) {
        LOG_ERROR("Data too small for header: {} < {}", data.size(), sizeof(Header));
        return BinResult<void>::failure(BinParseError::FileTooSmall);
    }
    
    // Read magic number
    std::string_view magic(reinterpret_cast<const char*>(data.data()), 4);
    if (magic != "LINK") {
        // Try reading a little-endian uint32
        uint32_t magic_val = read_u32_le(data, 0);
        if (magic_val != MAGIC_LINK) {
            LOG_ERROR("Invalid magic: 0x{:08X} (expected 0x{:08X} 'LINK')", magic_val, MAGIC_LINK);
            return BinResult<void>::failure(BinParseError::InvalidMagic);
        }
    }
    
    // Parse header fields (little-endian)
    header_.version     = read_u32_le(data, 4);
    header_.file_count  = read_u32_le(data, 8);
    header_.index_offset = read_u32_le(data, 12);
    header_.index_size   = read_u32_le(data, 16);
    header_.data_offset  = read_u32_le(data, 20);
    header_.flags        = read_u32_le(data, 24);
    // reserved field ignored
    
    LOG_DEBUG("Header: version={}, files={}, index_off=0x{:X}, index_sz={}, data_off=0x{:X}, flags=0x{:X}",
              header_.version, header_.file_count, header_.index_offset, 
              header_.index_size, header_.data_offset, header_.flags);
    
    // Version check
    if (header_.version < SUPPORTED_VERSION_MIN || header_.version > SUPPORTED_VERSION_MAX) {
        LOG_WARN("Unsupported version {}, attempting anyway", header_.version);
        // Do not return an error; attempt compatibility
    }
    
    // Basic sanity check
    if (header_.file_count > 100000) {
        LOG_ERROR("Suspicious file count: {}", header_.file_count);
        return BinResult<void>::failure(BinParseError::CorruptedIndex);
    }
    
    // Use 64-bit arithmetic to avoid overflow
    uint64_t index_end = static_cast<uint64_t>(header_.index_offset) + static_cast<uint64_t>(header_.index_size);
    if (index_end > data.size()) {
        LOG_ERROR("Index table extends beyond file: off=0x{:X}+0x{:X} > size=0x{:X}",
                  header_.index_offset, header_.index_size, data.size());
        return BinResult<void>::failure(BinParseError::CorruptedIndex);
    }
    
    if (static_cast<uint64_t>(header_.data_offset) > data.size()) {
        LOG_ERROR("Data offset beyond file: 0x{:X} > 0x{:X}", header_.data_offset, data.size());
        return BinResult<void>::failure(BinParseError::CorruptedIndex);
    }
    
    return BinResult<void>::success();
}

BinResult<void> BinArchive::parse_index_table(std::span<const uint8_t> data) {
    constexpr size_t ENTRY_SIZE = sizeof(IndexEntry);
    
    // Use 64-bit arithmetic to avoid overflow
    uint64_t required_size = static_cast<uint64_t>(header_.file_count) * ENTRY_SIZE;
    if (data.size() < required_size) {
        LOG_ERROR("Index table too small: {} bytes for {} entries (need {})",
                  data.size(), header_.file_count, required_size);
        return BinResult<void>::failure(BinParseError::CorruptedIndex);
    }
    
    entries_.clear();
    entries_.reserve(header_.file_count);
    
    for (uint32_t i = 0; i < header_.file_count; ++i) {
        size_t offset = static_cast<size_t>(i) * ENTRY_SIZE;
        
        BinFileEntry entry;
        entry.name_hash = read_u32_le(data, offset);
        entry.offset    = read_u32_le(data, offset + 4);
        entry.size      = read_u32_le(data, offset + 8);
        entry.flags     = read_u32_le(data, offset + 12);
        entry.type      = read_u32_le(data, offset + 16);
        // reserved at offset + 20
        
        // Generate a default name
        entry.name = fmt::format("file_{:04d}_{:08X}", i, entry.name_hash);
        
        entries_.push_back(std::move(entry));
    }
    
    LOG_DEBUG("Parsed {} index entries", entries_.size());
    return BinResult<void>::success();
}

BinResult<void> BinArchive::validate_data_blocks() const {
    uint64_t data_start = header_.data_offset;
    uint64_t data_end = file_data_.size();
    
    for (const auto& entry : entries_) {
        uint64_t block_start = data_start + entry.offset;
        uint64_t block_end = block_start + entry.size;
        
        if (block_end > data_end) {
            LOG_ERROR("Entry '{}' extends beyond data: [0x{:X}, 0x{:X}) > 0x{:X}",
                      entry.name, block_start, block_end, data_end);
            return BinResult<void>::failure(BinParseError::CorruptedData);
        }
    }
    return BinResult<void>::success();
}

const BinFileEntry* BinArchive::find_entry(std::string_view name) const noexcept {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&name](const BinFileEntry& e) { return e.name == name; });
    return it != entries_.end() ? &(*it) : nullptr;
}

const BinFileEntry* BinArchive::find_entry_by_hash(uint32_t hash) const noexcept {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [hash](const BinFileEntry& e) { return e.name_hash == hash; });
    return it != entries_.end() ? &(*it) : nullptr;
}

BinResult<std::vector<uint8_t>> BinArchive::extract(const BinFileEntry& entry) const {
    if (!loaded_) {
        return BinResult<std::vector<uint8_t>>::failure(BinParseError::CorruptedData);
    }
    
    auto view_result = view(entry);
    if (!view_result) return BinResult<std::vector<uint8_t>>::failure(view_result.error());
    
    std::vector<uint8_t> buffer(view_result->begin(), view_result->end());
    return BinResult<std::vector<uint8_t>>::success(std::move(buffer));
}

BinResult<void> BinArchive::extract_to(const BinFileEntry& entry, std::vector<uint8_t>& out_buffer) const {
    auto view_result = view(entry);
    if (!view_result) return BinResult<void>::failure(view_result.error());
    
    out_buffer.assign(view_result->begin(), view_result->end());
    return BinResult<void>::success();
}

BinResult<std::span<const uint8_t>> BinArchive::view(const BinFileEntry& entry) const {
    if (!loaded_) {
        return BinResult<std::span<const uint8_t>>::failure(BinParseError::CorruptedData);
    }
    
    uint64_t data_start = header_.data_offset;
    uint64_t block_start = data_start + entry.offset;
    uint64_t block_end = block_start + entry.size;
    
    if (block_end > file_data_.size()) {
        LOG_ERROR("Entry '{}' view out of bounds", entry.name);
        return BinResult<std::span<const uint8_t>>::failure(BinParseError::CorruptedData);
    }
    
    return BinResult<std::span<const uint8_t>>::success(
        std::span<const uint8_t>(file_data_.data() + block_start, entry.size)
    );
}

BinResult<void> BinArchive::extract_all(const std::filesystem::path& output_dir) const {
    if (!loaded_) {
        return BinResult<void>::failure(BinParseError::CorruptedData);
    }
    
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        LOG_ERROR("Failed to create output directory: {}", ec.message());
        return BinResult<void>::failure(BinParseError::IOError);
    }
    
    size_t success = 0, failed = 0;
    for (const auto& entry : entries_) {
        auto out_path = output_dir / entry.name;
        auto view_result = view(entry);
        if (!view_result) {
            LOG_ERROR("Failed to view entry '{}': {}", entry.name, to_string(view_result.error()));
            failed++;
            continue;
        }
        
        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            LOG_ERROR("Failed to create output file: {}", out_path.string());
            failed++;
            continue;
        }
        
        out.write(reinterpret_cast<const char*>(view_result->data()), static_cast<std::streamsize>(view_result->size()));
        if (!out) {
            LOG_ERROR("Failed to write output file: {}", out_path.string());
            failed++;
            continue;
        }
        
        success++;
    }
    
    LOG_INFO("Extract all: {} success, {} failed to {}", success, failed, output_dir.string());
    return failed == 0 ? BinResult<void>::success() : BinResult<void>::failure(BinParseError::IOError);
}

BinArchive::Info BinArchive::info() const noexcept {
    Info i;
    i.source_path = source_path_.string();
    i.version = header_.version;
    i.file_count = header_.file_count;
    i.total_size = file_data_.size();
    i.index_table_offset = header_.index_offset;
    i.data_offset = header_.data_offset;
    return i;
}

void BinArchive::clear() noexcept {
    source_path_.clear();
    file_data_.clear();
    entries_.clear();
    header_ = Header{};
    loaded_ = false;
}

std::string BinFileEntry::guess_extension() const {
    // Guess the extension from type and flags
    switch (type) {
        case 1:  return ".tex";   // texture
        case 2:  return ".mdl";   // model
        case 3:  return ".shd";   // shader
        case 4:  return ".snd";   // audio
        case 5:  return ".txt";   // text
        case 6:  return ".scr";   // script
        case 7:  return ".anim";  // animation
        case 8:  return ".ui";    // UI resource
        case 9:  return ".fnt";   // font
        case 10: return ".vid";   // video
        case 11: return ".dat";   // data table
        default:
            // Guess from the file name hash or content
            if (flags & 0x1) return ".cmp";  // compressed
            if (flags & 0x2) return ".enc";  // encrypted
            return ".bin";
    }
}

} // namespace tensyoki
