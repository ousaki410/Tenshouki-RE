/**
 * test_bin_parser.cpp - unit tests for the LINK container parser
 */

#include "gtest/gtest.h"
#include "bin_parser.hpp"
#include "logging.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <vector>

namespace tensyoki {
namespace test {

namespace {

void put_u32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    data[offset]     = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void put_u16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    data[offset]     = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void append_u32(std::vector<uint8_t>& data, uint32_t value) {
    size_t pos = data.size();
    data.resize(pos + 4);
    put_u32(data, pos, value);
}

void append_u16(std::vector<uint8_t>& data, uint16_t value) {
    size_t pos = data.size();
    data.resize(pos + 2);
    put_u16(data, pos, value);
}

/// Build a minimal GT1G 0600 block with one 4x4 texture and 64 pixel bytes
std::vector<uint8_t> make_gt1g_block() {
    std::vector<uint8_t> block(0x80, 0);
    const char magic[4] = {'G', 'T', '1', 'G'};
    std::copy(magic, magic + 4, block.begin());
    block[4] = '0'; block[5] = '6'; block[6] = '0'; block[7] = '0';

    put_u32(block, 0x08, static_cast<uint32_t>(block.size())); // total_size
    put_u32(block, 0x0C, 0x20);                                // header_size (1 texture)
    put_u32(block, 0x10, 1);                                   // texture_count
    put_u32(block, 0x1C, 1);                                   // per-texture value
    put_u32(block, 0x20, 0x0C);                                // texture offset rel. header_size

    const size_t tex = 0x2C;  // 0x20 + 0x0C
    block[tex + 0] = 0x10;    // mip_info
    block[tex + 1] = 0x01;    // format id
    block[tex + 2] = 0x22;    // size_log2: 4 x 4
    put_u32(block, tex + 8, 0x0C); // extra header size

    // Pixel data begins at 0x2C + 20 = 0x40 and runs to the end
    for (size_t i = 0x40; i < block.size(); ++i) {
        block[i] = static_cast<uint8_t>(i & 0xFF);
    }
    return block;
}

/// Wrap a payload in a nested LINK container with two descriptor records
std::vector<uint8_t> make_nested_link(const std::vector<uint8_t>& gt1g) {
    constexpr uint32_t descriptor_offset = 0x40;
    constexpr uint32_t record_count = 2;
    constexpr uint32_t data_offset = 0x60; // ((0x40 + 2*12) & ~15) + 16

    std::vector<uint8_t> nested(data_offset + gt1g.size(), 0);
    std::copy_n("LINK", 4, nested.begin());
    put_u32(nested, 0x04, 1);                              // version
    put_u32(nested, 0x08, 0x20);                           // data table offset
    put_u32(nested, 0x0C, record_count);                   // descriptor record count
    put_u32(nested, 0x10, descriptor_offset);              // descriptor offset
    put_u32(nested, 0x20, data_offset);                    // data offset
    put_u32(nested, 0x24, static_cast<uint32_t>(gt1g.size())); // data size

    const struct { uint16_t x, y, w, h; uint32_t tex; } records[] = {
        {10, 20, 30, 40, 0},
        {50, 60, 70, 80, 0},
    };
    for (uint32_t i = 0; i < record_count; ++i) {
        size_t pos = descriptor_offset + i * 12;
        put_u16(nested, pos + 0, records[i].x);
        put_u16(nested, pos + 2, records[i].y);
        put_u16(nested, pos + 4, records[i].w);
        put_u16(nested, pos + 6, records[i].h);
        put_u32(nested, pos + 8, records[i].tex);
    }

    std::copy(gt1g.begin(), gt1g.end(), nested.begin() + data_offset);
    return nested;
}

} // namespace

class BinArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logging::init("test.log", spdlog::level::off, spdlog::level::debug);

        test_dir_ = std::filesystem::temp_directory_path() / "tensyoki_test";
        std::filesystem::create_directories(test_dir_);

        write_test_bin();
        write_empty_slot_bin();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
        Logging::shutdown();
    }

    void write_file(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }

    /// Two payloads ("T" * 50 and "S" * 50), each followed by the sentinel
    void write_test_bin() {
        constexpr uint32_t count = 2;
        constexpr uint32_t table_end = 16 + count * 8; // 0x20
        constexpr uint32_t size = 50;

        std::vector<uint8_t> data(table_end + (size + 4) * count, 0);
        std::copy_n("LINK", 4, data.begin());
        put_u32(data, 0x04, count);
        put_u32(data, 0x08, 1);
        put_u32(data, 0x0C, 0);

        put_u32(data, 0x10, table_end);
        put_u32(data, 0x14, size);
        put_u32(data, 0x18, table_end + size + 4);
        put_u32(data, 0x1C, size);

        std::fill_n(data.begin() + table_end, size, 'T');
        put_u32(data, table_end + size, BinArchive::SENTINEL);
        std::fill_n(data.begin() + table_end + size + 4, size, 'S');
        put_u32(data, table_end + size + 4 + size, BinArchive::SENTINEL);

        test_bin_path_ = test_dir_ / "test.bin";
        write_file(test_bin_path_, data);
    }

    /// Third slot is empty and points at EOF, like the tail of res_shader.bin
    void write_empty_slot_bin() {
        constexpr uint32_t count = 3;
        constexpr uint32_t table_end = 16 + count * 8; // 0x28

        std::vector<uint8_t> data(table_end, 0);
        std::copy_n("LINK", 4, data.begin());
        put_u32(data, 0x04, count);
        put_u32(data, 0x08, 1);
        put_u32(data, 0x10, table_end);
        put_u32(data, 0x14, 0);
        put_u32(data, 0x18, table_end);
        put_u32(data, 0x1C, 0);
        put_u32(data, 0x20, static_cast<uint32_t>(data.size()));
        put_u32(data, 0x24, 0);

        empty_slot_path_ = test_dir_ / "empty_slots.bin";
        write_file(empty_slot_path_, data);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path test_bin_path_;
    std::filesystem::path empty_slot_path_;
};

TEST_F(BinArchiveTest, ParseValidBin) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << "Parse failed: " << tensyoki::to_string(result.error());

    const auto& entries = archive.entries();
    ASSERT_EQ(entries.size(), 2);

    EXPECT_EQ(entries[0].name, "entry_0000");
    EXPECT_EQ(entries[0].offset, 0x20u);
    EXPECT_EQ(entries[0].size, 50u);
    EXPECT_TRUE(entries[0].has_sentinel);
    EXPECT_FALSE(entries[0].is_nested_link);

    EXPECT_EQ(entries[1].name, "entry_0001");
    EXPECT_EQ(entries[1].offset, 0x20u + 50 + 4);
    EXPECT_EQ(entries[1].size, 50u);
    EXPECT_TRUE(entries[1].has_sentinel);
}

TEST_F(BinArchiveTest, ExtractResource) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());

    const auto& entries = archive.entries();
    ASSERT_EQ(entries.size(), 2);

    auto data_result = archive.extract(entries[0]);
    ASSERT_TRUE(data_result);
    ASSERT_EQ(data_result->size(), 50);
    for (uint8_t b : *data_result) EXPECT_EQ(b, 'T');

    data_result = archive.extract(entries[1]);
    ASSERT_TRUE(data_result);
    ASSERT_EQ(data_result->size(), 50);
    for (uint8_t b : *data_result) EXPECT_EQ(b, 'S');
}

TEST_F(BinArchiveTest, ViewResource) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());

    auto view_result = archive.view(archive.entries()[0]);
    ASSERT_TRUE(view_result);
    ASSERT_EQ(view_result->size(), 50);
    for (uint8_t b : *view_result) EXPECT_EQ(b, 'T');
}

TEST_F(BinArchiveTest, ExtractAll) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());

    auto out_dir = test_dir_ / "extracted";
    auto extract_result = archive.extract_all(out_dir);
    ASSERT_TRUE(extract_result);

    ASSERT_TRUE(std::filesystem::exists(out_dir / "entry_0000"));
    ASSERT_TRUE(std::filesystem::exists(out_dir / "entry_0001"));
    EXPECT_EQ(std::filesystem::file_size(out_dir / "entry_0000"), 50u);
}

TEST_F(BinArchiveTest, FindEntry) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());

    const auto* entry = archive.find_entry("entry_0000");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->index, 0u);
    EXPECT_EQ(entry->size, 50u);

    EXPECT_EQ(archive.find_entry("nonexistent"), nullptr);
    EXPECT_EQ(archive.find_by_index(0), entry);
    EXPECT_EQ(archive.find_by_index(99), nullptr);
}

TEST_F(BinArchiveTest, EmptySlotsAtEof) {
    BinArchive archive;
    auto result = archive.load_from_file(empty_slot_path_);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());

    const auto& entries = archive.entries();
    ASSERT_EQ(entries.size(), 3);
    EXPECT_TRUE(entries[0].empty());
    EXPECT_TRUE(entries[2].empty());
    EXPECT_EQ(entries[2].offset, 0x28u);
}

TEST_F(BinArchiveTest, InvalidMagic) {
    auto bad_path = test_dir_ / "bad.bin";
    std::vector<uint8_t> data(128, 0);
    put_u32(data, 0, 0xDEADBEEF);
    write_file(bad_path, data);

    BinArchive archive;
    auto result = archive.load_from_file(bad_path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), BinParseError::InvalidMagic);
}

TEST_F(BinArchiveTest, EmptyFile) {
    auto empty_path = test_dir_ / "empty.bin";
    std::ofstream(empty_path, std::ios::binary);

    BinArchive archive;
    auto result = archive.load_from_file(empty_path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), BinParseError::FileTooSmall);
}

TEST_F(BinArchiveTest, TruncatedTable) {
    auto bad_path = test_dir_ / "truncated.bin";
    std::vector<uint8_t> data(16 + 4, 0);
    std::copy_n("LINK", 4, data.begin());
    put_u32(data, 0x04, 4);  // claims 4 entries but table is truncated
    put_u32(data, 0x08, 1);
    write_file(bad_path, data);

    BinArchive archive;
    auto result = archive.load_from_file(bad_path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), BinParseError::CorruptedIndex);
}

TEST_F(BinArchiveTest, PayloadOutOfBounds) {
    auto bad_path = test_dir_ / "oob.bin";
    std::vector<uint8_t> data(0x20, 0);
    std::copy_n("LINK", 4, data.begin());
    put_u32(data, 0x04, 1);
    put_u32(data, 0x08, 1);
    put_u32(data, 0x10, 0x20);
    put_u32(data, 0x14, 0x1000);  // payload beyond EOF
    write_file(bad_path, data);

    BinArchive archive;
    auto result = archive.load_from_file(bad_path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), BinParseError::CorruptedData);
}

TEST_F(BinArchiveTest, NestedLinkAndGt1g) {
    auto gt1g = make_gt1g_block();
    auto nested = make_nested_link(gt1g);

    constexpr uint32_t table_end = 0x18; // one entry
    std::vector<uint8_t> data(table_end + nested.size() + 4, 0);
    std::copy_n("LINK", 4, data.begin());
    put_u32(data, 0x04, 1);
    put_u32(data, 0x08, 1);
    put_u32(data, 0x10, table_end);
    put_u32(data, 0x14, static_cast<uint32_t>(nested.size()));
    std::copy(nested.begin(), nested.end(), data.begin() + table_end);
    put_u32(data, table_end + nested.size(), BinArchive::SENTINEL);

    auto path = test_dir_ / "nested.bin";
    write_file(path, data);

    BinArchive archive;
    auto result = archive.load_from_file(path);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());

    ASSERT_EQ(archive.entries().size(), 1);
    EXPECT_TRUE(archive.entries()[0].is_nested_link);
    EXPECT_EQ(archive.entries()[0].signature, "LINK");
    EXPECT_EQ(archive.entries()[0].guess_extension(), ".link");

    auto nested_info = archive.parse_nested(archive.entries()[0]);
    ASSERT_TRUE(nested_info) << tensyoki::to_string(nested_info.error());

    EXPECT_EQ(nested_info->version, 1u);
    EXPECT_EQ(nested_info->record_count, 2u);
    ASSERT_EQ(nested_info->records.size(), 2);
    EXPECT_EQ(nested_info->records[0].x, 10);
    EXPECT_EQ(nested_info->records[0].y, 20);
    EXPECT_EQ(nested_info->records[0].w, 30);
    EXPECT_EQ(nested_info->records[0].h, 40);
    EXPECT_EQ(nested_info->records[1].texture_index, 0u);

    ASSERT_TRUE(nested_info->textures.has_value());
    const auto& gt = *nested_info->textures;
    EXPECT_EQ(gt.texture_count, 1u);
    ASSERT_EQ(gt.textures.size(), 1u);
    EXPECT_EQ(gt.textures[0].width, 4u);
    EXPECT_EQ(gt.textures[0].height, 4u);
    EXPECT_EQ(gt.textures[0].data_offset, 0x40u);
    EXPECT_EQ(gt.textures[0].data_size, 0x40u);
}

TEST_F(BinArchiveTest, NotANestedLink) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result);

    auto nested = archive.parse_nested(archive.entries()[0]);
    ASSERT_FALSE(nested);
    EXPECT_EQ(nested.error(), BinParseError::NotANestedLink);
}

TEST_F(BinArchiveTest, LoadFromMemory) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());

    std::ifstream file(test_bin_path_, std::ios::binary);
    std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());

    BinArchive archive2;
    auto result2 = archive2.load_from_memory(file_data);
    ASSERT_TRUE(result2) << tensyoki::to_string(result2.error());
    ASSERT_EQ(archive2.entries().size(), 2);

    auto view2 = archive2.view(archive2.entries()[0]);
    ASSERT_TRUE(view2);
    EXPECT_EQ(view2->size(), 50u);
}

/// Reads a real container when TENSYOKI_DATA_ROOT points at the game directory
TEST_F(BinArchiveTest, ParseRealResElseWhenAvailable) {
    const char* root = std::getenv("TENSYOKI_DATA_ROOT");
    if (!root) {
        GTEST_SKIP() << "TENSYOKI_DATA_ROOT not set";
    }
    auto path = std::filesystem::path(root) / "res" / "res_else.bin";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "res_else.bin not found under " << root;
    }

    BinArchive archive;
    auto result = archive.load_from_file(path);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());

    // Documented in docs/formats/link.md: 3 entries, first payload at 0x28
    EXPECT_EQ(archive.entries().size(), 3u);
    EXPECT_EQ(archive.entries().front().offset, 0x28u);
    // Two non-empty payloads; the third slot is an empty terminator
    EXPECT_GE(archive.info().sentinel_count, 2u);
}

/// Cross-checks all six containers against the counts documented in link.md
TEST_F(BinArchiveTest, ParseAllRealContainersWhenAvailable) {
    const char* root = std::getenv("TENSYOKI_DATA_ROOT");
    if (!root) {
        GTEST_SKIP() << "TENSYOKI_DATA_ROOT not set";
    }

    const struct { const char* name; uint32_t expected_entries; } containers[] = {
        {"res_grp.bin", 21},
        {"res_else.bin", 3},
        {"res_exp.bin", 1},
        {"res_exp_pc.bin", 1},
        {"res_shader.bin", 6},
        {"res_snd.bin", 2},
    };

    for (const auto& container : containers) {
        auto path = std::filesystem::path(root) / "res" / container.name;
        if (!std::filesystem::exists(path)) {
            ADD_FAILURE() << "missing " << path;
            continue;
        }

        BinArchive archive;
        auto result = archive.load_from_file(path);
        ASSERT_TRUE(result) << container.name << ": " << tensyoki::to_string(result.error());

        EXPECT_EQ(archive.entries().size(), container.expected_entries) << container.name;
        if (!archive.entries().empty()) {
            uint32_t table_end = static_cast<uint32_t>(16 + 8 * archive.entries().size());
            EXPECT_EQ(archive.entries().front().offset, table_end) << container.name;
        }
    }
}

/// res_grp entry 1 is a `_N1G` payload, not a nested LINK container
TEST_F(BinArchiveTest, ResGrpN1gSignatureWhenAvailable) {
    const char* root = std::getenv("TENSYOKI_DATA_ROOT");
    if (!root) {
        GTEST_SKIP() << "TENSYOKI_DATA_ROOT not set";
    }
    auto path = std::filesystem::path(root) / "res" / "res_grp.bin";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "res_grp.bin not found under " << root;
    }

    BinArchive archive;
    auto result = archive.load_from_file(path);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());
    ASSERT_GT(archive.entries().size(), 1u);

    const auto& entry = archive.entries()[1];
    EXPECT_EQ(entry.signature, "N1G");
    EXPECT_EQ(entry.guess_extension(), ".n1g");
    EXPECT_FALSE(entry.is_nested_link);

    // The remaining 20 entries are nested LINK containers
    size_t nested = 0;
    for (const auto& e : archive.entries()) {
        if (e.is_nested_link) nested++;
    }
    EXPECT_EQ(nested, 20u);
}

} // namespace test
} // namespace tensyoki

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
