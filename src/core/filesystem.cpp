/**
 * filesystem.cpp - VFS implementation
 */

#include "filesystem.hpp"
#include "logging.hpp"
#include <algorithm>
#include <cctype>
#include <sys/stat.h>

namespace tensyoki {

class PhysicalFileHandle : public FileHandle {
public:
    explicit PhysicalFileHandle(std::filesystem::path path, std::ios::openmode mode)
        : path_(std::move(path)) {
        file_.open(path_, mode | std::ios::binary);
    }
    
    ~PhysicalFileHandle() override { close(); }
    
    ssize_t read(void* buffer, size_t size) override {
        if (!file_.is_open()) return -1;
        file_.read(static_cast<char*>(buffer), size);
        return file_.gcount();
    }
    
    ssize_t write(const void* buffer, size_t size) override {
        if (!file_.is_open()) return -1;
        file_.write(static_cast<const char*>(buffer), size);
        return file_.good() ? static_cast<ssize_t>(size) : -1;
    }
    
    bool seek(int64_t offset, int whence) override {
        if (!file_.is_open()) return false;
        std::ios::seekdir dir;
        switch (whence) {
            case SEEK_SET: dir = std::ios::beg; break;
            case SEEK_CUR: dir = std::ios::cur; break;
            case SEEK_END: dir = std::ios::end; break;
            default: return false;
        }
        file_.seekg(offset, dir);
        file_.seekp(offset, dir);
        return file_.good();
    }
    
    int64_t tell() override {
        if (!file_.is_open()) return -1;
        return file_.tellg();
    }
    
    int64_t size() override {
        if (!file_.is_open()) return -1;
        auto pos = file_.tellg();
        file_.seekg(0, std::ios::end);
        int64_t sz = file_.tellg();
        file_.seekg(pos);
        return sz;
    }
    
    bool is_valid() const override { return file_.is_open(); }
    
    void close() override { if (file_.is_open()) file_.close(); }

private:
    std::filesystem::path path_;
    std::fstream file_;
};

std::string VFS::normalize_path(const std::string& path) const {
    std::string result = path;
    // Unify separators
    std::replace(result.begin(), result.end(), '\\', '/');
    // Remove redundant slashes
    std::string cleaned;
    cleaned.reserve(result.size());
    bool last_slash = false;
    for (char c : result) {
        if (c == '/') {
            if (!last_slash) cleaned.push_back(c);
            last_slash = true;
        } else {
            cleaned.push_back(c);
            last_slash = false;
        }
    }
    // Remove trailing slash (except root)
    if (cleaned.size() > 1 && cleaned.back() == '/') {
        cleaned.pop_back();
    }
    // Convert to lowercase when case-insensitive
    if (!case_sensitive_) {
        std::transform(cleaned.begin(), cleaned.end(), cleaned.begin(), 
                       [](unsigned char c) { return std::tolower(c); });
    }
    return cleaned;
}

bool VFS::mount(const std::filesystem::path& physical_path, const std::string& virtual_path) {
    auto abs_path = std::filesystem::absolute(physical_path);
    if (!std::filesystem::exists(abs_path) || !std::filesystem::is_directory(abs_path)) {
        LOG_ERROR("Mount path does not exist or is not a directory: {}", abs_path.string());
        return false;
    }
    
    MountPoint mp;
    mp.virtual_path = normalize_path(virtual_path);
    mp.physical_path = abs_path;
    mp.is_bin_container = false;
    mp.priority = static_cast<int>(mounts_.size());  // later mounts have higher priority
    
    std::unique_lock lock(mounts_mutex_);
    mounts_.push_back(std::move(mp));
    // Sort by priority (highest first)
    std::sort(mounts_.begin(), mounts_.end(), 
              [](const MountPoint& a, const MountPoint& b) { return a.priority > b.priority; });
    
    LOG_INFO("Mounted: {} -> {}", abs_path.string(), virtual_path);
    return true;
}

bool VFS::mount_bin(const std::filesystem::path& bin_path, const std::string& virtual_path) {
    // TODO: implement .bin container mounting (requires BinParser support)
    LOG_WARN("BIN container mount not yet implemented: {}", bin_path.string());
    return false;
}

void VFS::unmount(const std::string& virtual_path) {
    std::string norm = normalize_path(virtual_path);
    std::unique_lock lock(mounts_mutex_);
    mounts_.erase(
        std::remove_if(mounts_.begin(), mounts_.end(),
                       [&norm](const MountPoint& mp) { return mp.virtual_path == norm; }),
        mounts_.end()
    );
}

void VFS::unmount_all() {
    std::unique_lock lock(mounts_mutex_);
    mounts_.clear();
}

std::optional<std::filesystem::path> VFS::find_file(const std::string& virtual_path) const {
    std::string norm = normalize_path(virtual_path);
    
    std::shared_lock lock(mounts_mutex_);
    for (const auto& mp : mounts_) {
        // Build physical path
        std::string rel_path = norm;
        if (mp.virtual_path != "/") {
            if (norm.rfind(mp.virtual_path, 0) == 0) {
                rel_path = norm.substr(mp.virtual_path.size());
                if (rel_path.empty() || rel_path[0] != '/') {
                    rel_path = "/" + rel_path;
                }
            } else {
                continue;  // not under this mount point
            }
        }
        
        auto physical = mp.physical_path / rel_path.substr(1);  // strip leading /
        
        if (std::filesystem::exists(physical) && std::filesystem::is_regular_file(physical)) {
            return physical;
        }
    }
    return std::nullopt;
}

std::unique_ptr<FileHandle> VFS::open_read(const std::string& virtual_path) {
    auto physical = find_file(virtual_path);
    if (!physical) {
        LOG_DEBUG("File not found in VFS: {}", virtual_path);
        return nullptr;
    }
    
    auto handle = std::make_unique<PhysicalFileHandle>(*physical, std::ios::in);
    if (!handle->is_valid()) {
        LOG_ERROR("Failed to open file: {}", physical->string());
        return nullptr;
    }
    return handle;
}

std::unique_ptr<FileHandle> VFS::open_write(const std::string& virtual_path, bool append) {
    // Writes are only allowed in the user data directory
    auto user_dir = get_user_data_dir();
    auto full_path = user_dir / virtual_path.substr(virtual_path[0] == '/' ? 1 : 0);
    
    std::filesystem::create_directories(full_path.parent_path());
    
    auto mode = std::ios::out | (append ? std::ios::app : std::ios::trunc);
    auto handle = std::make_unique<PhysicalFileHandle>(full_path, mode);
    if (!handle->is_valid()) {
        LOG_ERROR("Failed to create file: {}", full_path.string());
        return nullptr;
    }
    return handle;
}

bool VFS::exists(const std::string& virtual_path) const {
    return find_file(virtual_path).has_value();
}

int64_t VFS::file_size(const std::string& virtual_path) const {
    auto physical = find_file(virtual_path);
    if (!physical) return -1;
    
    struct stat st;
    if (stat(physical->c_str(), &st) == 0) {
        return st.st_size;
    }
    return -1;
}

std::vector<std::string> VFS::list_directory(const std::string& virtual_path) const {
    std::vector<std::string> result;
    std::string norm = normalize_path(virtual_path);
    
    std::shared_lock lock(mounts_mutex_);
    for (const auto& mp : mounts_) {
        std::string rel_path = norm;
        if (mp.virtual_path != "/") {
            if (norm.rfind(mp.virtual_path, 0) == 0) {
                rel_path = norm.substr(mp.virtual_path.size());
            } else {
                continue;
            }
        }
        
        auto physical = mp.physical_path / rel_path.substr(1);
        if (std::filesystem::exists(physical) && std::filesystem::is_directory(physical)) {
            for (const auto& entry : std::filesystem::directory_iterator(physical)) {
                std::string name = entry.path().filename().string();
                if (!case_sensitive_) {
                    std::transform(name.begin(), name.end(), name.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                }
                result.push_back(name);
            }
        }
    }
    
    // Deduplicate
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    
    return result;
}

std::optional<std::filesystem::path> VFS::resolve(const std::string& virtual_path) const {
    return find_file(virtual_path);
}

std::filesystem::path VFS::get_user_data_dir() const {
    // Priority: environment variable > XDG_DATA_HOME > ~/.local/share
    if (const char* env = std::getenv("TENSYOKI_USER_DATA")) {
        return std::filesystem::path(env);
    }
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        return std::filesystem::path(xdg) / "tensyoki";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share" / "tensyoki";
    }
    return std::filesystem::temp_directory_path() / "tensyoki";
}

std::filesystem::path VFS::get_cache_dir() const {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
        return std::filesystem::path(xdg) / "tensyoki";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".cache" / "tensyoki";
    }
    return std::filesystem::temp_directory_path() / "tensyoki_cache";
}

} // namespace tensyoki