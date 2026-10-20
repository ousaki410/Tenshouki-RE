/**
 * filesystem.hpp - virtual file system (VFS)
 * Supports mounting multiple directories, transparent .bin container reads, case-insensitive paths
 */

#pragma once

#include <filesystem>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <cstdint>
#include <optional>

namespace tensyoki {

class FileHandle {
public:
    virtual ~FileHandle() = default;
    virtual ssize_t read(void* buffer, size_t size) = 0;
    virtual ssize_t write(const void* buffer, size_t size) = 0;
    virtual bool seek(int64_t offset, int whence = SEEK_SET) = 0;
    virtual int64_t tell() = 0;
    virtual int64_t size() = 0;
    virtual bool is_valid() const = 0;
    virtual void close() = 0;
};

class VFS {
public:
    // Get singleton
    static VFS& instance() {
        static VFS instance;
        return instance;
    }
    
    VFS(const VFS&) = delete;
    VFS& operator=(const VFS&) = delete;
    
    // Mount directory to virtual path
    // For example: mount("/path/to/game/data", "/") mounts the game data directory as root
    bool mount(const std::filesystem::path& physical_path, const std::string& virtual_path = "/");
    
    // Mount .bin container
    bool mount_bin(const std::filesystem::path& bin_path, const std::string& virtual_path = "/");
    
    // Unmount
    void unmount(const std::string& virtual_path);
    void unmount_all();
    
    // Open file for reading
    std::unique_ptr<FileHandle> open_read(const std::string& virtual_path);
    
    // Open file for writing (user directory only)
    std::unique_ptr<FileHandle> open_write(const std::string& virtual_path, bool append = false);
    
    // Check whether file exists
    bool exists(const std::string& virtual_path) const;
    
    // Get file size
    int64_t file_size(const std::string& virtual_path) const;
    
    // List directory
    std::vector<std::string> list_directory(const std::string& virtual_path) const;
    
    // Resolve virtual path to physical path (for debugging)
    std::optional<std::filesystem::path> resolve(const std::string& virtual_path) const;
    
    // Set case sensitivity (emulate Windows case-insensitivity)
    void set_case_sensitive(bool sensitive) { case_sensitive_ = sensitive; }
    
    // Get user data directory (saves, config, logs)
    std::filesystem::path get_user_data_dir() const;
    
    // Get cache directory
    std::filesystem::path get_cache_dir() const;

private:
    VFS() = default;
    
    struct MountPoint {
        std::string virtual_path;
        std::filesystem::path physical_path;
        bool is_bin_container = false;
        int priority = 0;  // priority, higher priority searched first
    };
    
    mutable std::shared_mutex mounts_mutex_;
    std::vector<MountPoint> mounts_;
    bool case_sensitive_ = false;  // insensitive by default, emulating Windows
    
    // Normalize path (unify separators, case)
    std::string normalize_path(const std::string& path) const;
    
    // Find file among mount points
    std::optional<std::filesystem::path> find_file(const std::string& virtual_path) const;
};

// Convenience functions
inline std::unique_ptr<FileHandle> vfs_open_read(const std::string& path) {
    return VFS::instance().open_read(path);
}

inline std::unique_ptr<FileHandle> vfs_open_write(const std::string& path, bool append = false) {
    return VFS::instance().open_write(path, append);
}

inline bool vfs_exists(const std::string& path) {
    return VFS::instance().exists(path);
}

} // namespace tensyoki