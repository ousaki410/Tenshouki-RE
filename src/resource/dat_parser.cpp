/**
 * dat_parser.cpp - Koei game .DAT / scenario data parser implementation
 */

#include "dat_parser.hpp"
#include "logging.hpp"
#include <fstream>
#include <system_error>
#include <cstring>
#include <cstdio>

#if defined(__has_include)
#if __has_include(<iconv.h>)
#include <iconv.h>
#define TENSYOKI_HAVE_ICONV 1
#endif
#endif

namespace tensyoki {

std::string decode_cp932(std::span<const uint8_t> data) {
#if TENSYOKI_HAVE_ICONV
    // Strip trailing NUL bytes for the conversion
    size_t len = data.size();
    while (len > 0 && data[len - 1] == 0) --len;
    if (len == 0) return {};

    iconv_t cd = iconv_open("UTF-8", "CP932");
    if (cd == (iconv_t)(-1)) {
        LOG_WARN("iconv CP932 not available; falling back to ASCII passthrough");
    } else {
        std::string in((const char*)data.data(), len);
        std::string out;
        out.reserve(len * 4);  // UTF-8 can be up to 4 bytes per source byte
        const char* in_ptr = in.data();
        size_t in_left = in.size();
        char out_buf[256];
        while (in_left > 0) {
            char* out_ptr = out_buf;
            size_t out_left = sizeof(out_buf);
            size_t rc = iconv(cd, const_cast<char**>(&in_ptr), &in_left, &out_ptr, &out_left);
            if (rc == (size_t)(-1)) {
                // Skip the offending source byte and emit a replacement
                if (errno == EILSEQ || errno == EINVAL) {
                    out.append("?");
                    in_ptr += 1;
                    in_left -= 1;
                    continue;
                }
                break;  // E2BIG: flush buffer below
            }
            out.append(out_buf, sizeof(out_buf) - out_left);
        }
        iconv_close(cd);
        return out;
    }
#endif
    // Fallback: printable ASCII through, everything else '?'
    std::string out;
    out.reserve(data.size());
    for (uint8_t b : data) {
        if (b >= 0x20 && b < 0x7F) out.push_back(static_cast<char>(b));
        else out.push_back('?');
    }
    return out;
}

namespace {

void put_name_raw(const char* name, size_t name_len, std::string& out) {
    out.clear();
    out.reserve(name_len * 2);
    for (size_t i = 0; i < name_len; ++i) {
        if (name[i] == 0) break;
        char buf[4];
        int n = std::snprintf(buf, sizeof(buf), "%02x", static_cast<unsigned char>(name[i]));
        out.append(buf, static_cast<size_t>(n));
    }
}

} // namespace

SndataResult<SndataArchive> parse_sndata_memory(std::span<const uint8_t> data, uint32_t scenario) {
    SndataArchive archive;
    archive.scenario = scenario;

    if (data.size() < SNDATA_ATTR_BASE + SNDATA_ATTR_STRIDE) {
        LOG_ERROR("Sndata too small: {} bytes (need at least {})",
                  data.size(), SNDATA_ATTR_BASE + SNDATA_ATTR_STRIDE);
        return SndataResult<SndataArchive>::failure(SndataParseError::FileTooSmall);
    }

    // --- Officer attribute block ---
    for (uint32_t slot = 0; slot < SNDATA_NSLOT; ++slot) {
        uint64_t off = SNDATA_ATTR_BASE + static_cast<uint64_t>(slot) * SNDATA_ATTR_STRIDE;
        if (off + SNDATA_ATTR_STRIDE > data.size()) break;

        const auto* rec = reinterpret_cast<const SndataAttrRecord*>(data.data() + off);
        if (rec->id == 0xFFFF) continue;  // empty slot

        Officer off_;
        off_.slot = slot;
        off_.id = rec->id;
        off_.status = rec->status;
        off_.gender = rec->gender;
        off_.attr.assign(data.data() + off, data.data() + off + 47);
        off_.name_raw.clear();  // filled from the name block below

        archive.officers.push_back(std::move(off_));
    }

    // --- Officer name block ---
    {
        size_t ni = 0;
        for (uint32_t slot = 0; slot < SNDATA_NSLOT; ++slot) {
            uint64_t off = SNDATA_NAME_BASE + static_cast<uint64_t>(slot) * SNDATA_NAME_STRIDE;
            if (off + SNDATA_NAME_STRIDE > data.size()) break;

            const auto* rec = reinterpret_cast<const SndataNameRecord*>(data.data() + off);
            if (rec->name[0] == 0) continue;

            // Match by slot to the officer parsed above
            Officer* matched = nullptr;
            for (auto& o : archive.officers) {
                if (o.slot == slot) { matched = &o; break; }
            }
            if (!matched) continue;

            put_name_raw(rec->name, 18, matched->name_raw);
            matched->name_utf8 = decode_cp932(
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(rec->name), 18));
        }
    }

    // --- Castle block ---
    for (uint32_t idx = 0; idx < SNDATA_NCASTLE; ++idx) {
        uint64_t off = SNDATA_CASTLE_BASE + static_cast<uint64_t>(idx) * SNDATA_CASTLE_STRIDE;
        if (off + SNDATA_CASTLE_STRIDE > data.size()) break;

        const auto* rec = reinterpret_cast<const SndataCastleRecord*>(data.data() + off);
        if (rec->name[0] == 0) continue;  // empty castle slot

        Castle c;
        c.index = idx;
        c.header[0] = rec->header[0];
        c.header[1] = rec->header[1];
        c.header[2] = rec->header[2];
        c.header[3] = rec->header[3];
        put_name_raw(rec->name, 9, c.name_raw);
        c.name_utf8 = decode_cp932(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(rec->name), 9));
        c.tail.assign(rec->tail, rec->tail + 16);
        c.raw.assign(data.data() + off, data.data() + off + 33);

        archive.castles.push_back(std::move(c));
    }

    LOG_INFO("Parsed sndata scenario {}: {} officers, {} castles",
             scenario, archive.officers.size(), archive.castles.size());
    return SndataResult<SndataArchive>::success(std::move(archive));
}

SndataResult<SndataArchive> parse_sndata_file(const std::filesystem::path& path, uint32_t scenario) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        LOG_ERROR("Sndata file not found: {}", path.string());
        return SndataResult<SndataArchive>::failure(SndataParseError::FileNotFound);
    }

    auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        LOG_ERROR("Cannot stat {}: {}", path.string(), ec.message());
        return SndataResult<SndataArchive>::failure(SndataParseError::IOError);
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open sndata file: {}", path.string());
        return SndataResult<SndataArchive>::failure(SndataParseError::IOError);
    }

    std::vector<uint8_t> data(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file) {
        LOG_ERROR("Failed to read sndata file: {}", path.string());
        return SndataResult<SndataArchive>::failure(SndataParseError::IOError);
    }

    return parse_sndata_memory(data, scenario);
}

} // namespace tensyoki
