/**
 * test_dat_parser.cpp - unit tests for the DAT / sndata data layer
 */

#include "gtest/gtest.h"
#include "dat_parser.hpp"
#include "logging.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace tensyoki {
namespace test {

namespace {

void put_u16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    data[offset]     = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void put_u32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    data[offset]     = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

/// Build a minimal sndata-like buffer with one officer and one castle.
std::vector<uint8_t> make_sndata() {
    std::vector<uint8_t> data(SNDATA_ATTR_BASE + SNDATA_ATTR_STRIDE * 2 +
                              SNDATA_NAME_BASE + SNDATA_NAME_STRIDE * 2 +
                              SNDATA_CASTLE_BASE + SNDATA_CASTLE_STRIDE, 0);

    // ---- Officer slot 0 (attribute block at 0x0AE8) ----
    uint64_t attr0 = SNDATA_ATTR_BASE;
    // id at +0x12 = 999 (0x03E7), status +0x16 = 0, gender +0x17 = 1
    put_u16(data, attr0 + 0x12, 999);
    data[attr0 + 0x16] = 0;
    data[attr0 + 0x17] = 1;

    // ---- All other officer slots are empty (id == 0xFFFF) ----
    for (uint32_t slot = 1; slot < SNDATA_NSLOT; ++slot) {
        uint64_t attr = SNDATA_ATTR_BASE + static_cast<uint64_t>(slot) * SNDATA_ATTR_STRIDE;
        put_u16(data, attr + 0x12, 0xFFFF);
    }

    // ---- Name block (0x6C94, stride 24) ----
    // Slot 0 name: "蛎崎義広" in CP932 (fake bytes for test purposes)
    const char fake_name0[18] = {'\x8a', '\x61', '\x8d', '\xe8',
                                 '\x8b', '\x60', '\x8d', '\x4c', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::copy_n(fake_name0, 18, data.begin() + SNDATA_NAME_BASE);

    // All other name slots are empty
    for (uint32_t slot = 1; slot < SNDATA_NSLOT; ++slot) {
        data[SNDATA_NAME_BASE + static_cast<uint64_t>(slot) * SNDATA_NAME_STRIDE] = 0;
    }

    // ---- Castle 0 (0x9E74, stride 33) ----
    uint64_t castle0 = SNDATA_CASTLE_BASE;
    put_u16(data, castle0 + 0, 22);   // header[0]
    put_u16(data, castle0 + 2, 12);   // header[1]
    put_u16(data, castle0 + 4, 320);  // header[2]
    put_u16(data, castle0 + 6, 0xFFFF); // header[3]
    const char fake_castle_name[9] = {'\x8b', '\xe0', '\x90', '\xce', 0, 0, 0, 0, 0};
    std::copy_n(fake_castle_name, 9, data.begin() + castle0 + 8);

    // All other castle slots are empty
    for (uint32_t idx = 1; idx < SNDATA_NCASTLE; ++idx) {
        data[SNDATA_CASTLE_BASE + static_cast<uint64_t>(idx) * SNDATA_CASTLE_STRIDE + 8] = 0;
    }

    return data;
}

void write_file(const std::filesystem::path& path,
                const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

class DatParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logging::init("test_dat.log", spdlog::level::off, spdlog::level::debug);
        test_dir_ = std::filesystem::temp_directory_path() / "tensyoki_dat_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
        Logging::shutdown();
    }

    std::filesystem::path test_dir_;
};

TEST_F(DatParserTest, ParseSyntheticSndata) {
    auto data = make_sndata();
    auto result = parse_sndata_memory(data, 1);
    ASSERT_TRUE(result) << "Parse failed: "
                        << to_string(result.error());

    const auto& archive = *result;
    EXPECT_EQ(archive.scenario, 1u);
    EXPECT_EQ(archive.officer_count(), 1u);  // slot 1 is empty (0xFFFF)
    EXPECT_EQ(archive.castle_count(), 1u);

    const auto& officer = archive.officers[0];
    EXPECT_EQ(officer.slot, 0u);
    EXPECT_EQ(officer.id, 999u);
    EXPECT_EQ(officer.status, 0u);
    EXPECT_EQ(officer.gender, 1u);
    EXPECT_EQ(officer.attr.size(), 47u);
    EXPECT_FALSE(officer.name_raw.empty());
    EXPECT_EQ(officer.name_raw, "8a618de88b608d4c");

    const auto& castle = archive.castles[0];
    EXPECT_EQ(castle.index, 0u);
    EXPECT_EQ(castle.header[0], 22u);
    EXPECT_EQ(castle.header[1], 12u);
    EXPECT_EQ(castle.header[2], 320u);
    EXPECT_EQ(castle.header[3], 0xFFFFu);
    EXPECT_EQ(castle.name_raw, "8be090ce");
    EXPECT_EQ(castle.tail.size(), 16u);
    EXPECT_EQ(castle.raw.size(), 33u);
}

TEST_F(DatParserTest, EmptySndata) {
    auto data = make_sndata();
    // Override slot 0 id to 0xFFFF so there are no valid officers
    put_u16(data, SNDATA_ATTR_BASE + 0x12, 0xFFFF);
    // Override castle name to empty
    data[SNDATA_CASTLE_BASE + 8] = 0;

    auto result = parse_sndata_memory(data, 1);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->officer_count(), 0u);
    EXPECT_EQ(result->castle_count(), 0u);  // all castle names empty
}

TEST_F(DatParserTest, TooSmallBuffer) {
    std::vector<uint8_t> data(100, 0);  // far below SNDATA_ATTR_BASE + stride
    auto result = parse_sndata_memory(data, 1);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), SndataParseError::FileTooSmall);
}

TEST_F(DatParserTest, ParseRealSndata1WhenAvailable) {
    const char* root = std::getenv("TENSYOKI_DATA_ROOT");
    if (!root) {
        GTEST_SKIP() << "TENSYOKI_DATA_ROOT not set";
    }
    auto path = std::filesystem::path(root) / "scenario" / "sndata1.dat";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "sndata1.dat not found under " << root;
    }

    auto result = parse_sndata_file(path, 1);
    ASSERT_TRUE(result) << "Parse failed: " << to_string(result.error());

    const auto& archive = *result;
    EXPECT_GT(archive.officer_count(), 0u);
    EXPECT_GT(archive.castle_count(), 0u);
    EXPECT_EQ(archive.castle_count(), 214u);

    // Spot-check known values from parse_sndata.py output (sndata1.dat)
    // Slot 0: id=999, name 蛎崎義广, status=0, gender=1
    bool found_slot0 = false;
    for (const auto& o : archive.officers) {
        if (o.slot == 0) {
            found_slot0 = true;
            EXPECT_EQ(o.id, 999u);
            EXPECT_EQ(o.status, 0u);
            EXPECT_EQ(o.gender, 1u);
            EXPECT_FALSE(o.name_raw.empty());
            break;
        }
    }
    EXPECT_TRUE(found_slot0);

    // Castle 0: name 金石, header [22, 12, 320, 65535]
    bool found_castle0 = false;
    for (const auto& c : archive.castles) {
        if (c.index == 0) {
            found_castle0 = true;
            EXPECT_EQ(c.header[0], 22u);
            EXPECT_EQ(c.header[1], 12u);
            EXPECT_EQ(c.header[2], 320u);
            EXPECT_EQ(c.header[3], 0xFFFFu);
            EXPECT_FALSE(c.name_raw.empty());
            break;
        }
    }
    EXPECT_TRUE(found_castle0);
}

} // namespace test
} // namespace tensyoki

} // namespace tensyoki

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
