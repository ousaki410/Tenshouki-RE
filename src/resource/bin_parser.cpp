/**
 * bin_parser.cpp - Implementation of the Koei "LINK" .bin container parser
 *
 * The layout implemented here is the one validated against all six res/*.bin
 * files; see docs/formats/link.md and docs/formats/gt1g.md for the evidence.
 */

#include "bin_parser.hpp"
#include "logging.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <limits>
#include <system_error>

namespace tensyoki {

namespace {

constexpr uint64_t align_down_16(uint64_t value) { return value & ~uint64_t{0xF}; }

bool starts_with(std::span<const uint8_t> data, std::string_view tag) {
    if (data.size() < tag.size()) return false;
    return std::memcmp(data.data(), tag.data(), tag.size()) == 0;
}

} // namespace

// Little-endian read helpers
uint32_t BinArchive::read_u32_le(std::span<const uint8_t> data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint16_t BinArchive::read_u16_le(std::span<const uint8_t> data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<uint16_t>(data[offset]) |
           static_cast<uint16_t>(data[offset + 1] << 8);
}

// Take ownership of a buffer and parse it. clear() resets source_path_, so the
// caller must restore it afterwards on success.
BinResult<void> BinArchive::parse_owned(std::vector<uint8_t>&& data) {
    clear();
    file_data_ = std::move(data);

    auto result = parse_from_span(file_data_);
    if (!result) {
        clear();
    }
    return result;
}

BinResult<void> BinArchive::parse_from_span(std::span<const uint8_t> data) {
    // 1. Header
    auto header_result = parse_header(data);
    if (!header_result) return header_result;

    // 2. Entry table
    auto table_result = parse_table(data);
    if (!table_result) return table_result;

    // 3. Payload bounds and sentinels
    auto validate_result = validate_entries();
    if (!validate_result) return validate_result;

    loaded_ = true;
    LOG_INFO("Loaded LINK archive: {} entries, version {}, {} bytes",
             entries_.size(), header_.version, file_data_.size());
    return BinResult<void>::success();
}

BinResult<void> BinArchive::load_from_file(const std::filesystem::path& path) {
    LOG_INFO("Loading BIN archive: {}", path.string());

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        LOG_ERROR("File not found: {}", path.string());
        return BinResult<void>::failure(BinParseError::FileNotFound);
    }

    auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        LOG_ERROR("Cannot stat {}: {}", path.string(), ec.message());
        return BinResult<void>::failure(BinParseError::IOError);
    }
    if (file_size < HEADER_SIZE) {
        LOG_ERROR("File too small ({} bytes, min {}): {}", file_size, HEADER_SIZE, path.string());
        return BinResult<void>::failure(BinParseError::FileTooSmall);
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open file: {}", path.string());
        return BinResult<void>::failure(BinParseError::IOError);
    }

    // Refuse absurd allocations rather than throwing std::bad_alloc
    if (file_size > static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
        LOG_ERROR("File too large to map into memory: {}", path.string());
        return BinResult<void>::failure(BinParseError::UnsupportedFormat);
    }

    std::vector<uint8_t> file_data(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(file_data.data()), static_cast<std::streamsize>(file_data.size()));
    if (!file) {
        LOG_ERROR("Failed to read file: {}", path.string());
        return BinResult<void>::failure(BinParseError::IOError);
    }

    // parse_owned() clears source_path_, so set it after a successful parse
    auto result = parse_owned(std::move(file_data));
    if (result) {
        source_path_ = path;
    }
    return result;
}

BinResult<void> BinArchive::load_from_memory(std::span<const uint8_t> data) {
    // Copy external data so view() spans remain valid for the archive's lifetime
    return parse_owned(std::vector<uint8_t>(data.begin(), data.end()));
}

BinResult<void> BinArchive::parse_header(std::span<const uint8_t> data) {
    if (data.size() < HEADER_SIZE) {
        LOG_ERROR("Data too small for LINK header: {} < {}", data.size(), HEADER_SIZE);
        return BinResult<void>::failure(BinParseError::FileTooSmall);
    }

    header_ = Header{};
    std::memcpy(&header_, data.data(), sizeof(Header));

    if (std::memcmp(header_.magic, "LINK", 4) != 0) {
        uint32_t magic_val = read_u32_le(data, 0);
        LOG_ERROR("Invalid magic: 0x{:08X} (expected 0x{:08X} 'LINK')", magic_val, MAGIC_LINK);
        return BinResult<void>::failure(BinParseError::InvalidMagic);
    }

    LOG_DEBUG("Header: entries={}, version={}, reserved={}",
              header_.entry_count, header_.version, header_.reserved);

    if (header_.version != SUPPORTED_VERSION) {
        LOG_WARN("Unexpected LINK version {} (known files use {})", header_.version, SUPPORTED_VERSION);
    }
    if (header_.reserved != 0) {
        LOG_DEBUG("Non-zero reserved header field: 0x{:08X}", header_.reserved);
    }

    if (header_.entry_count > MAX_ENTRIES) {
        LOG_ERROR("Suspicious entry count: {}", header_.entry_count);
        return BinResult<void>::failure(BinParseError::CorruptedIndex);
    }

    // 64-bit arithmetic: table extends from 0x10 for entry_count * 8 bytes
    uint64_t table_end = static_cast<uint64_t>(HEADER_SIZE) +
                         static_cast<uint64_t>(header_.entry_count) * TABLE_ENTRY_SIZE;
    if (table_end > data.size()) {
        LOG_ERROR("Entry table extends beyond file: {} > {}", table_end, data.size());
        return BinResult<void>::failure(BinParseError::CorruptedIndex);
    }

    return BinResult<void>::success();
}

BinResult<void> BinArchive::parse_table(std::span<const uint8_t> data) {
    entries_.clear();
    entries_.reserve(header_.entry_count);

    for (uint32_t i = 0; i < header_.entry_count; ++i) {
        size_t pos = HEADER_SIZE + static_cast<size_t>(i) * TABLE_ENTRY_SIZE;

        BinFileEntry entry;
        entry.index = i;
        entry.offset = read_u32_le(data, pos);
        entry.size = read_u32_le(data, pos + 4);
        entry.name = fmt::format("entry_{:04d}", i);

        // Sniff the first bytes of the payload (bounded by what is available)
        if (entry.size > 0 && entry.offset < data.size()) {
            size_t available = std::min<size_t>(entry.size, data.size() - entry.offset);
            size_t probe = std::min<size_t>(available, 16);
            entry.signature = sniff_signature(data.subspan(entry.offset, probe));
            entry.is_nested_link = (entry.signature == "LINK");
        }

        entries_.push_back(std::move(entry));
    }

    LOG_DEBUG("Parsed {} entry-table records", entries_.size());
    return BinResult<void>::success();
}

BinResult<void> BinArchive::validate_entries() {
    const uint64_t table_end = static_cast<uint64_t>(HEADER_SIZE) +
                               static_cast<uint64_t>(header_.entry_count) * TABLE_ENTRY_SIZE;
    const uint64_t file_size = file_data_.size();

    bool sentinels_ok = true;

    for (auto& entry : entries_) {
        if (entry.size == 0) {
            // Empty slots continue the offset progression and may point at or
            // even past EOF (res_shader.bin entries 3-5); only check the floor.
            if (entry.offset < table_end) {
                LOG_WARN("Empty entry {} has offset 0x{:X} inside the table (table ends at 0x{:X})",
                         entry.index, entry.offset, table_end);
            }
            continue;
        }

        const uint64_t start = entry.offset;
        const uint64_t end = start + entry.size;

        if (start < table_end) {
            LOG_ERROR("Entry {} starts inside the table: 0x{:X} < 0x{:X}", entry.index, start, table_end);
            return BinResult<void>::failure(BinParseError::CorruptedIndex);
        }
        if (end > file_size) {
            LOG_ERROR("Entry {} extends beyond file: [0x{:X}, 0x{:X}) > 0x{:X}",
                      entry.index, start, end, file_size);
            return BinResult<void>::failure(BinParseError::CorruptedData);
        }

        // Sentinel check: only two gaps are documented as verified so far,
        // therefore a mismatch is a warning rather than a hard failure.
        if (end + SENTINEL_SIZE <= file_size &&
            read_u32_le(file_data_, static_cast<size_t>(end)) == SENTINEL) {
            entry.has_sentinel = true;
        } else {
            sentinels_ok = false;
            LOG_DEBUG("Entry {} has no 0x{:08X} sentinel at 0x{:X}", entry.index, SENTINEL, end);
        }
    }

    if (!entries_.empty() && entries_.front().size > 0 &&
        entries_.front().offset != table_end) {
        LOG_WARN("First payload starts at 0x{:X}, expected 0x{:X}",
                 entries_.front().offset, table_end);
    }

    if (!sentinels_ok) {
        LOG_WARN("Not every payload is followed by the 0x{:08X} sentinel", SENTINEL);
    }

    return BinResult<void>::success();
}

std::string BinArchive::sniff_signature(std::span<const uint8_t> data) {
    if (data.size() >= 4) {
        if (starts_with(data, "LINK")) return "LINK";
        if (starts_with(data, "GT1G")) return "GT1G";
        if (starts_with(data, "LS11")) return "LS11";
        if (starts_with(data, "DBUG")) return "DBUG";
        if (starts_with(data, "DXBC")) return "DXBC";
        if (starts_with(data, "DDS ")) return "DDS";
        if (starts_with(data, "OggS")) return "OggS";
        if (starts_with(data, "RIFF")) return "RIFF";
        if (starts_with(data, "MZ"))   return "MZ";
        if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') return "ELF";
        if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return "PNG";
    }
    if (data.size() >= 2) {
        if (data[0] == 0x78 && (data[1] == 0x9C || data[1] == 0x01 || data[1] == 0xDA)) return "zlib";
        if (data[0] == 0x1F && data[1] == 0x8B) return "gzip";
    }
    return {};
}

BinResult<std::span<const uint8_t>> BinArchive::view(const BinFileEntry& entry) const {
    if (!loaded_) {
        return BinResult<std::span<const uint8_t>>::failure(BinParseError::CorruptedData);
    }

    const uint64_t start = entry.offset;
    const uint64_t end = start + entry.size;

    if (end > file_data_.size()) {
        LOG_ERROR("Entry '{}' view out of bounds", entry.name);
        return BinResult<std::span<const uint8_t>>::failure(BinParseError::CorruptedData);
    }

    return BinResult<std::span<const uint8_t>>::success(
        std::span<const uint8_t>(file_data_.data() + start, entry.size)
    );
}

BinResult<NestedLinkInfo> BinArchive::parse_nested(const BinFileEntry& entry) const {
    auto payload_result = view(entry);
    if (!payload_result) {
        return BinResult<NestedLinkInfo>::failure(payload_result.error());
    }
    auto payload = *payload_result;

    if (!starts_with(payload, "LINK")) {
        LOG_ERROR("Entry '{}' is not a nested LINK container", entry.name);
        return BinResult<NestedLinkInfo>::failure(BinParseError::NotANestedLink);
    }
    if (payload.size() < NESTED_DATA_TABLE_OFFSET + 8) {
        LOG_ERROR("Nested LINK '{}' is too small: {} bytes", entry.name, payload.size());
        return BinResult<NestedLinkInfo>::failure(BinParseError::CorruptedNestedTable);
    }

    NestedLinkInfo info;
    info.version = read_u32_le(payload, 0x04);
    info.data_table_offset = read_u32_le(payload, 0x08);
    info.record_count = read_u32_le(payload, 0x0C);
    info.descriptor_offset = read_u32_le(payload, 0x10);

    if (info.version != SUPPORTED_VERSION) {
        // Some res_grp entries carry other values here; only the three GT1G
        // samples have version 1 (docs/formats/link.md, E-0020).
        LOG_DEBUG("Nested LINK '{}' has version field {}", entry.name, info.version);
    }
    if (info.data_table_offset + 8 > payload.size()) {
        LOG_ERROR("Nested LINK '{}' data table at 0x{:X} is out of bounds",
                  entry.name, info.data_table_offset);
        return BinResult<NestedLinkInfo>::failure(BinParseError::CorruptedNestedTable);
    }

    info.data_offset = read_u32_le(payload, info.data_table_offset);
    info.data_size = read_u32_le(payload, info.data_table_offset + 4);

    if (static_cast<uint64_t>(info.data_offset) + info.data_size > payload.size()) {
        LOG_ERROR("Nested LINK '{}' data block [0x{:X}, 0x{:X}) exceeds payload of {} bytes",
                  entry.name, info.data_offset, info.data_offset + info.data_size, payload.size());
        return BinResult<NestedLinkInfo>::failure(BinParseError::CorruptedNestedTable);
    }

    // Descriptor table must fit before the data block
    uint64_t descriptor_bytes = static_cast<uint64_t>(info.record_count) * NESTED_RECORD_SIZE;
    uint64_t descriptor_end = static_cast<uint64_t>(info.descriptor_offset) + descriptor_bytes;
    if (info.descriptor_offset < NESTED_DATA_TABLE_OFFSET || descriptor_end > info.data_offset) {
        LOG_ERROR("Nested LINK '{}' descriptor table [0x{:X}, 0x{:X}) does not fit before data at 0x{:X}",
                  entry.name, info.descriptor_offset, descriptor_end, info.data_offset);
        return BinResult<NestedLinkInfo>::failure(BinParseError::CorruptedNestedTable);
    }

    // Documented data-start rule; a mismatch is informational only
    uint64_t predicted_data = align_down_16(descriptor_end) + 16;
    if (predicted_data != info.data_offset) {
        LOG_DEBUG("Nested LINK '{}' data offset 0x{:X} differs from predicted 0x{:X}",
                  entry.name, info.data_offset, predicted_data);
    }

    info.records.reserve(info.record_count);
    for (uint32_t i = 0; i < info.record_count; ++i) {
        size_t pos = info.descriptor_offset + static_cast<size_t>(i) * NESTED_RECORD_SIZE;
        LinkRecord record;
        record.x = read_u16_le(payload, pos);
        record.y = read_u16_le(payload, pos + 2);
        record.w = read_u16_le(payload, pos + 4);
        record.h = read_u16_le(payload, pos + 6);
        record.texture_index = read_u32_le(payload, pos + 8);
        info.records.push_back(record);
    }

    // Optional GT1G texture block
    auto block = payload.subspan(info.data_offset, info.data_size);
    if (starts_with(block, "GT1G")) {
        auto gt1g_result = parse_gt1g(block);
        if (!gt1g_result) {
            return BinResult<NestedLinkInfo>::failure(gt1g_result.error());
        }
        info.textures = std::move(*gt1g_result);

        for (const auto& record : info.records) {
            if (record.texture_index >= info.textures->textures.size()) {
                LOG_WARN("Nested LINK '{}': record references texture {} but only {} exist",
                         entry.name, record.texture_index, info.textures->textures.size());
                break;
            }
        }
    }

    LOG_DEBUG("Nested LINK '{}': {} records, data at 0x{:X} ({} bytes)",
              entry.name, info.record_count, info.data_offset, info.data_size);
    return BinResult<NestedLinkInfo>::success(std::move(info));
}

BinResult<Gt1gInfo> BinArchive::parse_gt1g(std::span<const uint8_t> block) const {
    if (block.size() < GT1G_FIXED_HEADER_SIZE || !starts_with(block, "GT1G")) {
        return BinResult<Gt1gInfo>::failure(BinParseError::CorruptedTextureBlock);
    }

    // Version tag, e.g. "0600"
    if (block.size() < 8 || std::memcmp(block.data() + 4, "0600", 4) != 0) {
        LOG_ERROR("Unsupported GT1G version tag");
        return BinResult<Gt1gInfo>::failure(BinParseError::UnsupportedFormat);
    }

    Gt1gInfo info;
    info.total_size = read_u32_le(block, 0x08);
    info.header_size = read_u32_le(block, 0x0C);
    info.texture_count = read_u32_le(block, 0x10);

    if (info.texture_count == 0 || info.texture_count > 4096) {
        LOG_ERROR("Invalid GT1G texture count: {}", info.texture_count);
        return BinResult<Gt1gInfo>::failure(BinParseError::CorruptedTextureBlock);
    }
    if (info.header_size != GT1G_FIXED_HEADER_SIZE + 4 * info.texture_count) {
        LOG_WARN("GT1G header size {} != expected {}",
                 info.header_size, GT1G_FIXED_HEADER_SIZE + 4 * info.texture_count);
    }
    if (info.total_size > block.size()) {
        LOG_ERROR("GT1G total size 0x{:X} exceeds block size 0x{:X}", info.total_size, block.size());
        return BinResult<Gt1gInfo>::failure(BinParseError::CorruptedTextureBlock);
    }
    uint32_t effective_size = info.total_size ? info.total_size : static_cast<uint32_t>(block.size());

    uint64_t table_end = static_cast<uint64_t>(info.header_size) + 4ull * info.texture_count;
    if (table_end > effective_size) {
        LOG_ERROR("GT1G offset table ends at 0x{:X}, beyond block size 0x{:X}", table_end, effective_size);
        return BinResult<Gt1gInfo>::failure(BinParseError::CorruptedTextureBlock);
    }

    info.textures.reserve(info.texture_count);
    for (uint32_t i = 0; i < info.texture_count; ++i) {
        uint32_t rel = read_u32_le(block, info.header_size + 4ull * i);
        uint64_t start = static_cast<uint64_t>(info.header_size) + rel;

        if (start + GT1G_TEXTURE_HEADER_SIZE > effective_size) {
            LOG_ERROR("GT1G texture {} header at 0x{:X} is out of bounds", i, start);
            return BinResult<Gt1gInfo>::failure(BinParseError::CorruptedTextureBlock);
        }

        Gt1gTexture texture;
        texture.index = i;
        texture.header_offset = static_cast<uint32_t>(start);
        texture.mip_info = block[start];
        texture.format_id = block[start + 1];
        texture.size_log2 = block[start + 2];

        unsigned width_log2 = texture.size_log2 >> 4;
        unsigned height_log2 = texture.size_log2 & 0x0F;
        texture.width = 1u << width_log2;
        texture.height = 1u << height_log2;

        texture.data_offset = static_cast<uint32_t>(start + GT1G_TEXTURE_HEADER_SIZE);

        uint64_t next_start = (i + 1 < info.texture_count)
            ? static_cast<uint64_t>(info.header_size) + read_u32_le(block, info.header_size + 4ull * (i + 1))
            : effective_size;

        if (next_start < texture.data_offset || next_start > effective_size) {
            LOG_ERROR("GT1G texture {} data range is invalid", i);
            return BinResult<Gt1gInfo>::failure(BinParseError::CorruptedTextureBlock);
        }
        texture.data_size = static_cast<uint32_t>(next_start - texture.data_offset);

        info.textures.push_back(texture);
    }

    LOG_DEBUG("GT1G block: {} textures, header 0x{:X}, total 0x{:X}",
              info.texture_count, info.header_size, info.total_size);
    return BinResult<Gt1gInfo>::success(std::move(info));
}

const BinFileEntry* BinArchive::find_entry(std::string_view name) const noexcept {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&name](const BinFileEntry& e) { return e.name == name; });
    return it != entries_.end() ? &(*it) : nullptr;
}

const BinFileEntry* BinArchive::find_by_index(uint32_t index) const noexcept {
    if (index >= entries_.size()) return nullptr;
    return &entries_[index];
}

BinResult<std::vector<uint8_t>> BinArchive::extract(const BinFileEntry& entry) const {
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
    i.file_count = header_.entry_count;
    i.total_size = file_data_.size();
    i.table_offset = HEADER_SIZE;
    i.data_offset = entries_.empty() ? 0 : entries_.front().offset;
    i.sentinel_count = static_cast<uint64_t>(std::count_if(
        entries_.begin(), entries_.end(), [](const BinFileEntry& e) { return e.has_sentinel; }));
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
    if (signature == "LINK")  return ".link";
    if (signature == "GT1G")  return ".gt1g";
    if (signature == "LS11")  return ".ls11";
    if (signature == "DDS")   return ".dds";
    if (signature == "DBUG" || signature == "DXBC") return ".shader";
    if (signature == "PNG")   return ".png";
    if (signature == "OggS")  return ".ogg";
    if (signature == "RIFF")  return ".riff";
    if (signature == "zlib")  return ".z";
    if (signature == "gzip")  return ".gz";
    if (signature == "MZ")    return ".exe";
    if (signature == "ELF")   return ".elf";
    return ".bin";
}

} // namespace tensyoki
