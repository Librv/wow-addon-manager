#pragma once
#include <filesystem>
#include <utility>

namespace wam {

// Deletes the file at path when it goes out of scope, so a download that
// fails to install (or throws mid-install) never leaves a stray zip behind.
class ScopedTempFile {
public:
    explicit ScopedTempFile(std::filesystem::path p) : path_(std::move(p)) {}
    ~ScopedTempFile() { std::error_code ec; std::filesystem::remove(path_, ec); }
    ScopedTempFile(const ScopedTempFile&) = delete;
    ScopedTempFile& operator=(const ScopedTempFile&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace wam
