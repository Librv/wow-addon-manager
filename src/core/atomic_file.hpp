#pragma once
#include <filesystem>
#include <string>

namespace wam {

// Replaces path with content so that a crash leaves either the old file or
// the new one, never a half-written mix: writes a temp file next to it,
// syncs it, then renames it over path. The file is created with `perms`
// before any content goes in, so a secret is never readable by others, not
// even briefly. Throws on failure, leaving path untouched.
void writeFileAtomic(const std::filesystem::path& path, const std::string& content,
                     std::filesystem::perms perms);

} // namespace wam
