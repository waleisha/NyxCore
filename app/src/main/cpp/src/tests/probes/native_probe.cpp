#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#if defined(__clang__)
#define NYX_TEST_PROBE_NOINLINE __attribute__((noinline, optnone))
#elif defined(__GNUC__)
#define NYX_TEST_PROBE_NOINLINE __attribute__((noinline))
#else
#define NYX_TEST_PROBE_NOINLINE
#endif

#ifndef NYX_RUNTIME_PROBE_EXPECTED
#define NYX_RUNTIME_PROBE_EXPECTED 20260712
#endif

#ifndef NYX_PLT_PROBE_INPUT
#define NYX_PLT_PROBE_INPUT 7
#endif

#ifndef NYX_PLT_PROBE_EXPECTED
#define NYX_PLT_PROBE_EXPECTED 41
#endif

#ifndef NYX_PLT_REPLACEMENT_EXPECTED
#define NYX_PLT_REPLACEMENT_EXPECTED 82
#endif

namespace {

constexpr int kPltProbeOffset = NYX_PLT_PROBE_EXPECTED - NYX_PLT_PROBE_INPUT;

} // namespace

extern "C" __attribute__((visibility("default"))) int nyx_runtime_probe_value() {
    return NYX_RUNTIME_PROBE_EXPECTED;
}

extern "C" __attribute__((visibility("default"))) NYX_TEST_PROBE_NOINLINE int nyx_plt_call_probe_value(
    int seed
) {
    const pid_t pid = getpid();
    if (pid <= 0) {
        return NYX_PLT_REPLACEMENT_EXPECTED;
    }

    return seed + kPltProbeOffset;
}

extern "C" __attribute__((visibility("default"))) NYX_TEST_PROBE_NOINLINE int nyx_vfs_open_write_probe(
    const char* path,
    const char* payload
) {
    if (path == nullptr || payload == nullptr) {
        return -EINVAL;
    }

    const int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return -errno;
    }

    const auto size = static_cast<int>(std::strlen(payload));
    const auto written = write(fd, payload, static_cast<size_t>(size));
    const int saved_errno = errno;
    close(fd);

    if (written < 0) {
        return -saved_errno;
    }
    return static_cast<int>(written);
}

extern "C" __attribute__((visibility("default"))) NYX_TEST_PROBE_NOINLINE int nyx_vfs_openat_write_probe(
    const char* path,
    const char* payload
) {
    if (path == nullptr || payload == nullptr) {
        return -EINVAL;
    }

    const int fd = openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        return -errno;
    }

    const auto size = static_cast<int>(std::strlen(payload));
    const auto written = write(fd, payload, static_cast<size_t>(size));
    const int saved_errno = errno;
    close(fd);

    if (written < 0) {
        return -saved_errno;
    }
    return static_cast<int>(written);
}

extern "C" __attribute__((visibility("default"))) NYX_TEST_PROBE_NOINLINE int nyx_vfs_stat_probe(
    const char* path
) {
    if (path == nullptr) {
        return -EINVAL;
    }

    struct stat info {};
    if (stat(path, &info) != 0) {
        return -errno;
    }

    return static_cast<int>(info.st_size);
}

extern "C" __attribute__((visibility("default"))) NYX_TEST_PROBE_NOINLINE int nyx_vfs_lstat_probe(
    const char* path
) {
    if (path == nullptr) {
        return -EINVAL;
    }

    struct stat info {};
    if (lstat(path, &info) != 0) {
        return -errno;
    }

    return static_cast<int>(info.st_size);
}

extern "C" __attribute__((visibility("default"))) NYX_TEST_PROBE_NOINLINE int nyx_vfs_fstatat_probe(
    const char* path
) {
    if (path == nullptr) {
        return -EINVAL;
    }

    struct stat info {};
    if (fstatat(AT_FDCWD, path, &info, 0) != 0) {
        return -errno;
    }

    return static_cast<int>(info.st_size);
}
