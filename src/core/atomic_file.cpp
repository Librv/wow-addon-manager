#include "core/atomic_file.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdexcept>
#include <unistd.h>

namespace wam {

namespace fs = std::filesystem;

void writeFileAtomic(const fs::path& path, const std::string& content, fs::perms perms) {
    fs::create_directories(path.parent_path());
    const fs::path tmp = path.string() + ".tmp";
    const auto fail = [&](const std::string& what) {
        const std::string msg = what + " '" + tmp.string() + "': " + std::strerror(errno);
        ::unlink(tmp.c_str());
        throw std::runtime_error(msg);
    };

    ::unlink(tmp.c_str()); // a leftover from a crash may have looser permissions
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, static_cast<mode_t>(perms));
    if (fd < 0) fail("could not create");
    ::fchmod(fd, static_cast<mode_t>(perms)); // open() is subject to the umask; this is not

    const char* p = content.data();
    std::size_t left = content.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            fail("could not write");
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) { ::close(fd); fail("could not sync"); }
    if (::close(fd) != 0) fail("could not close");
    if (::rename(tmp.c_str(), path.c_str()) != 0) fail("could not rename");
}

} // namespace wam
