/**
 * test_bin_parser.cpp - unit tests
 */

#include "gtest/gtest.h"
#include "bin_parser.hpp"
#include "filesystem.hpp"
#include "logging.hpp"
#include <filesystem>
#include <fstream>

namespace tensyoki {
namespace test {

class BinArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logging::init("test.log", spdlog::level::off, spdlog::level::debug);
        
        // Create temporary test file
        test_dir_ = std::filesystem::temp_directory_path() / "tensyoki_test";
        std::filesystem::create_directories(test_dir_);
        
        // Create test .bin file
        create_test_bin();
    }
    
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
        Logging::shutdown();
    }
    
    void create_test_bin() {
        test_bin_path_ = test_dir_ / "test.bin";
        std::ofstream ofs(test_bin_path_, std::ios::binary);
        
        // Write LINK header (44 bytes)
        uint32_t magic = 0x4B4E494C;  // "LINK" little-endian
        uint32_t version = 1;
        uint32_t file_count = 2;
        uint32_t index_offset = 44;   // after header
        uint32_t index_size = 2 * 24; // 2 index entries
        uint32_t data_offset = 44 + 48; // header + index table
        uint32_t flags = 0;
        uint32_t reserved[4] = {0};
        
        ofs.write(reinterpret_cast<char*>(&magic), 4);
        ofs.write(reinterpret_cast<char*>(&version), 4);
        ofs.write(reinterpret_cast<char*>(&file_count), 4);
        ofs.write(reinterpret_cast<char*>(&index_offset), 4);
        ofs.write(reinterpret_cast<char*>(&index_size), 4);
        ofs.write(reinterpret_cast<char*>(&data_offset), 4);
        ofs.write(reinterpret_cast<char*>(&flags), 4);
        ofs.write(reinterpret_cast<char*>(reserved), 16);
        
        // Write index table (2 entries, 24 bytes each)
        // Entry 1: name_hash=0x12345678, offset=0, size=50, flags=0, type=1(Texture)
        uint32_t entry1[] = {0x12345678, 0, 50, 0, 1, 0};
        ofs.write(reinterpret_cast<char*>(entry1), 24);
        
        // Entry 2: name_hash=0x87654321, offset=50, size=50, flags=0, type=3(Shader)
        uint32_t entry2[] = {0x87654321, 50, 50, 0, 3, 0};
        ofs.write(reinterpret_cast<char*>(entry2), 24);
        
        // Write data section (100 bytes)
        std::vector<char> data(100, 'A');
        for (int i = 0; i < 50; ++i) data[i] = 'T';  // Texture data
        for (int i = 50; i < 100; ++i) data[i] = 'S'; // Shader data
        ofs.write(data.data(), 100);
    }
    
    std::filesystem::path test_dir_;
    std::filesystem::path test_bin_path_;
};

TEST_F(BinArchiveTest, ParseValidBin) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << "Parse failed: " << tensyoki::to_string(result.error());
    
    const auto& entries = archive.entries();
    ASSERT_EQ(entries.size(), 2);
    
    EXPECT_EQ(entries[0].name_hash, 0x12345678);
    EXPECT_EQ(entries[0].offset, 0);
    EXPECT_EQ(entries[0].size, 50);
    EXPECT_EQ(entries[0].type, 1);
    
    EXPECT_EQ(entries[1].name_hash, 0x87654321);
    EXPECT_EQ(entries[1].offset, 50);
    EXPECT_EQ(entries[1].size, 50);
    EXPECT_EQ(entries[1].type, 3);
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
    
    const auto& entries = archive.entries();
    ASSERT_EQ(entries.size(), 2);
    
    auto view_result = archive.view(entries[0]);
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
    
    ASSERT_TRUE(std::filesystem::exists(out_dir / "file_0000_12345678"));
    ASSERT_TRUE(std::filesystem::exists(out_dir / "file_0001_87654321"));
}

TEST_F(BinArchiveTest, FindEntry) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());
    
    // Default name is the hash value
    const auto* entry = archive.find_entry("file_0000_12345678");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->name_hash, 0x12345678);
    
    // Nonexistent resource
    EXPECT_EQ(archive.find_entry("nonexistent"), nullptr);
}

TEST_F(BinArchiveTest, FindByHash) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());
    
    const auto* entry = archive.find_entry_by_hash(0x12345678);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->name_hash, 0x12345678);
    
    EXPECT_EQ(archive.find_entry_by_hash(0xDEADBEEF), nullptr);
}

TEST_F(BinArchiveTest, InvalidMagic) {
    // Create file with invalid magic
    auto bad_path = test_dir_ / "bad.bin";
    std::ofstream ofs(bad_path, std::ios::binary);
    uint32_t bad_magic = 0xDEADBEEF;
    ofs.write(reinterpret_cast<char*>(&bad_magic), 4);
    char pad[100] = {0};
    ofs.write(pad, 100);
    ofs.flush();
    ofs.close();
    
    BinArchive archive;
    auto result = archive.load_from_file(bad_path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), BinParseError::InvalidMagic);
}

TEST_F(BinArchiveTest, EmptyFile) {
    auto empty_path = test_dir_ / "empty.bin";
    std::ofstream ofs(empty_path, std::ios::binary);
    
    BinArchive archive;
    auto result = archive.load_from_file(empty_path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), BinParseError::FileTooSmall);
}

TEST_F(BinArchiveTest, LoadFromMemory) {
    BinArchive archive;
    auto result = archive.load_from_file(test_bin_path_);
    ASSERT_TRUE(result) << tensyoki::to_string(result.error());
    
    // Re-parse using load_from_memory
    BinArchive archive2;
    auto view_result = archive.view(archive.entries()[0]);
    ASSERT_TRUE(view_result);
    
    // Read entire file into memory
    std::ifstream file(test_bin_path_, std::ios::binary);
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> file_data(size);
    file.read(reinterpret_cast<char*>(file_data.data()), size);
    
    auto result2 = archive2.load_from_memory(file_data);
    ASSERT_TRUE(result2) << tensyoki::to_string(result2.error());
    ASSERT_EQ(archive2.entries().size(), 2);
}

} // namespace test
} // namespace tensyoki

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}