#include "sdk/include/test.h"

#include "sdk/include/hook.h"
#include "sdk/include/memory.h"
#include "sdk/include/stack.h"
#include "sdk/include/vfs_patcher.h"

#include "src/engines/unity/il2cpp_resolver.h"
#include "src/runtime/hook/hook_registry.h"
#include "src/runtime/hook/inline_backend.h"
#include "src/runtime/hook/plt_backend.h"
#include "src/runtime/loader/native_library.h"
#include "src/runtime/memory/memory_normalizer.h"
#include "src/runtime/memory/memory_map.h"
#include "src/runtime/memory/memory_protect.h"
#include "src/runtime/memory/memory_system.h"
#include "src/runtime/memory/memory_attributes.h"
#include "src/runtime/memory/memory_transaction.h"
#include "src/runtime/stack/stack_trace.h"
#include "src/runtime/stack/stack_context.h"
#include "src/runtime/stack/stack_hooks.h"
#include "src/runtime/stack/stack_normalizer.h"
#include "src/runtime/stack/stack_repair.h"
#include "src/runtime/vfs/io_redirector.h"
#include "src/runtime/vfs/path_mapper.h"

#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#ifndef MREMAP_MAYMOVE
#define MREMAP_MAYMOVE 1
#endif

#ifndef NYX_TEST_PROBE_LIBRARY
#define NYX_TEST_PROBE_LIBRARY "libnyx_test_probe.so"
#endif

#ifndef NYX_RUNTIME_PROBE_LIBRARY
#define NYX_RUNTIME_PROBE_LIBRARY NYX_TEST_PROBE_LIBRARY
#endif

#ifndef NYX_RUNTIME_PROBE_SYMBOL
#define NYX_RUNTIME_PROBE_SYMBOL "nyx_runtime_probe_value"
#endif

#ifndef NYX_RUNTIME_PROBE_EXPECTED
#define NYX_RUNTIME_PROBE_EXPECTED 20260712
#endif

#ifndef NYX_PLT_CALLEE_LIBRARY
#define NYX_PLT_CALLEE_LIBRARY ""
#endif

#ifndef NYX_PLT_CALLER_LIBRARY
#define NYX_PLT_CALLER_LIBRARY NYX_TEST_PROBE_LIBRARY
#endif

#ifndef NYX_PLT_CALL_SYMBOL
#define NYX_PLT_CALL_SYMBOL "nyx_plt_call_probe_value"
#endif

#ifndef NYX_PLT_TARGET_SYMBOL
#define NYX_PLT_TARGET_SYMBOL "getpid"
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

#ifndef NYX_VFS_CALLER_LIBRARY
#define NYX_VFS_CALLER_LIBRARY NYX_TEST_PROBE_LIBRARY
#endif

#ifndef NYX_VFS_OPEN_WRITE_SYMBOL
#define NYX_VFS_OPEN_WRITE_SYMBOL "nyx_vfs_open_write_probe"
#endif

#ifndef NYX_VFS_OPENAT_WRITE_SYMBOL
#define NYX_VFS_OPENAT_WRITE_SYMBOL "nyx_vfs_openat_write_probe"
#endif

#ifndef NYX_VFS_STAT_SYMBOL
#define NYX_VFS_STAT_SYMBOL "nyx_vfs_stat_probe"
#endif

#ifndef NYX_VFS_LSTAT_SYMBOL
#define NYX_VFS_LSTAT_SYMBOL "nyx_vfs_lstat_probe"
#endif

#ifndef NYX_VFS_FSTATAT_SYMBOL
#define NYX_VFS_FSTATAT_SYMBOL "nyx_vfs_fstatat_probe"
#endif

#if defined(__clang__)
#define NYX_RUNTIME_DOCTOR_NOINLINE __attribute__((noinline, optnone))
#elif defined(__GNUC__)
#define NYX_RUNTIME_DOCTOR_NOINLINE __attribute__((noinline))
#else
#define NYX_RUNTIME_DOCTOR_NOINLINE
#endif

#ifndef NYX_TARGET_TAG
#define NYX_TARGET_TAG "demo"
#endif

#ifndef NYX_NATIVE_LIBRARY_NAME
#define NYX_NATIVE_LIBRARY_NAME "lib" NYX_TARGET_TAG ".so"
#endif

namespace nyx {
namespace sdk {
namespace test {

namespace {

volatile std::uint32_t g_vfs_patch_probe = 0x4E595856;
runtime::stack::StackTraceSnapshot* g_inline_hook_snapshot = nullptr;
runtime::stack::StackTraceSnapshot* g_plt_hook_snapshot = nullptr;

bool expect_status(const char* name, runtime::RuntimeStatus actual, runtime::RuntimeStatus expected) {
    if (actual == expected) {
        NYX_LOGI("runtime doctor %s: passed", name);
        return true;
    }

    NYX_LOGE(
        "runtime doctor %s: expected %s, got %s",
        name,
        runtime::status_name(expected),
        runtime::status_name(actual)
    );
    return false;
}

bool expect_sdk_status(const char* name, Status actual, Status expected) {
    if (actual == expected) {
        NYX_LOGI("runtime doctor %s: passed", name);
        return true;
    }

    NYX_LOGE(
        "runtime doctor %s: expected %s, got %s",
        name,
        StatusStr(expected),
        StatusStr(actual)
    );
    return false;
}

bool expect_engine_status(const char* name, engines::EngineStatus actual, engines::EngineStatus expected) {
    if (actual == expected) {
        NYX_LOGI("runtime doctor %s: passed", name);
        return true;
    }

    NYX_LOGE(
        "runtime doctor %s: expected %s, got %s",
        name,
        engines::status_name(expected),
        engines::status_name(actual)
    );
    return false;
}

bool expect_true(const char* name, bool value) {
    if (value) {
        NYX_LOGI("runtime doctor %s: passed", name);
        return true;
    }

    NYX_LOGE("runtime doctor %s: expected true", name);
    return false;
}

bool expect_false(const char* name, bool value) {
    if (!value) {
        NYX_LOGI("runtime doctor %s: passed", name);
        return true;
    }

    NYX_LOGE("runtime doctor %s: expected false", name);
    return false;
}

bool expect_int(const char* name, int actual, int expected) {
    if (actual == expected) {
        NYX_LOGI("runtime doctor %s: passed", name);
        return true;
    }

    NYX_LOGE("runtime doctor %s: expected %d, got %d", name, expected, actual);
    return false;
}

bool has_stack_kind(
    const std::vector<runtime::stack::NormalizedStackFrame>& frames,
    runtime::stack::StackType kind
) {
    return std::any_of(frames.begin(), frames.end(), [kind](const runtime::stack::NormalizedStackFrame& frame) {
        return frame.kind == kind;
    });
}

bool has_stack_status(
    const std::vector<runtime::stack::NormalizedStackFrame>& frames,
    runtime::stack::StackStatus status
) {
    return std::any_of(frames.begin(), frames.end(), [status](const runtime::stack::NormalizedStackFrame& frame) {
        return frame.status == status;
    });
}

bool raw_frames_preserved(const runtime::stack::StackTraceSnapshot& snapshot) {
    if (snapshot.raw_frames.empty() || snapshot.normalized_frames.empty()) {
        return false;
    }

    for (const auto& frame : snapshot.normalized_frames) {
        const bool found = std::any_of(
            snapshot.raw_frames.begin(),
            snapshot.raw_frames.end(),
            [&frame](const runtime::stack::RawStackFrame& raw) {
                return raw.pc == frame.raw.pc;
            }
        );
        if (!found) {
            return false;
        }
    }
    return true;
}

bool expect_string(const char* name, const std::string& actual, const std::string& expected) {
    if (actual == expected) {
        NYX_LOGI("runtime doctor %s: passed", name);
        return true;
    }

    NYX_LOGE("runtime doctor %s: expected %s, got %s", name, expected.c_str(), actual.c_str());
    return false;
}

int doctor_prot(runtime::memory::PagePermission permission) {
    int prot = 0;
    if (permission.read) {
        prot |= PROT_READ;
    }
    if (permission.write) {
        prot |= PROT_WRITE;
    }
    if (permission.execute) {
        prot |= PROT_EXEC;
    }
    return prot;
}

class OwnedMapping {
public:
    OwnedMapping() = default;

    OwnedMapping(std::uintptr_t address, std::size_t size)
        : address_(address), size_(size) {}

    OwnedMapping(const OwnedMapping&) = delete;
    OwnedMapping& operator=(const OwnedMapping&) = delete;

    OwnedMapping(OwnedMapping&& other) noexcept {
        reset(other.address_, other.size_);
        other.release();
    }

    OwnedMapping& operator=(OwnedMapping&& other) noexcept {
        if (this != &other) {
            close();
            reset(other.address_, other.size_);
            other.release();
        }
        return *this;
    }

    ~OwnedMapping() {
        close();
    }

    static OwnedMapping create(std::size_t size, runtime::memory::PagePermission permission) {
        void* mapped = ::mmap(
            nullptr,
            size,
            doctor_prot(permission),
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0
        );
        if (mapped == MAP_FAILED) {
            return OwnedMapping{};
        }

        return OwnedMapping{reinterpret_cast<std::uintptr_t>(mapped), size};
    }

    bool valid() const {
        return address_ != 0 && size_ != 0;
    }

    std::uintptr_t start() const {
        return address_;
    }

    std::uintptr_t end() const {
        return address_ + static_cast<std::uintptr_t>(size_);
    }

    std::size_t size() const {
        return size_;
    }

    void reset(std::uintptr_t address, std::size_t size) {
        address_ = address;
        size_ = size;
    }

    void release() {
        address_ = 0;
        size_ = 0;
    }

    void close() {
        if (valid()) {
            ::munmap(reinterpret_cast<void*>(address_), size_);
            release();
        }
    }

private:
    std::uintptr_t address_ = 0;
    std::size_t size_ = 0;
};

bool expect_hook_state(
    const char* name,
    const runtime::hook::HookRegistry& registry,
    const std::string& owner,
    const std::string& target,
    runtime::hook::HookState expected
) {
    for (const auto& record : registry.records()) {
        if (record.owner == owner && record.target == target) {
            if (record.state == expected) {
                NYX_LOGI("runtime doctor %s: passed", name);
                return true;
            }

            NYX_LOGE(
                "runtime doctor %s: expected %s, got %s",
                name,
                runtime::hook::state_name(expected),
                runtime::hook::state_name(record.state)
            );
            return false;
        }
    }

    NYX_LOGE("runtime doctor %s: hook record not found", name);
    return false;
}

class FakeHookBackend final : public runtime::hook::HookBackend {
public:
    FakeHookBackend(runtime::RuntimeStatus install_status, runtime::RuntimeStatus remove_status)
        : install_status_(install_status), remove_status_(remove_status) {}

    runtime::RuntimeResult install(runtime::hook::HookRecord& record) override {
        ++install_count_;
        if (install_status_ != runtime::RuntimeStatus::Ok) {
            return runtime::RuntimeResult{install_status_, "fake install failed"};
        }

        record.original = fake_original();
        return runtime::RuntimeResult{};
    }

    runtime::RuntimeResult remove(runtime::hook::HookRecord& record) override {
        ++remove_count_;
        if (remove_status_ != runtime::RuntimeStatus::Ok) {
            return runtime::RuntimeResult{remove_status_, "fake remove failed"};
        }

        record.original = nullptr;
        return runtime::RuntimeResult{};
    }

    int install_count() const {
        return install_count_;
    }

    int remove_count() const {
        return remove_count_;
    }

private:
    static void* fake_original() {
        return reinterpret_cast<void*>(0x3);
    }

    runtime::RuntimeStatus install_status_;
    runtime::RuntimeStatus remove_status_;
    int install_count_ = 0;
    int remove_count_ = 0;
};

constexpr int kHookProbeValue = 41;
constexpr int kHookReplacementValue = 82;
constexpr int kHookProbeInput = 7;

using HookProbe = int (*)(int);
using PidFn = pid_t (*)();
using PltProbe = int (*)(int);
using VfsOpenWriteProbe = int (*)(const char*, const char*);
using VfsOpenAtWriteProbe = int (*)(const char*, const char*);
using VfsStatProbe = int (*)(const char*);
using VfsLstatProbe = int (*)(const char*);
using VfsFstatAtProbe = int (*)(const char*);

runtime::vfs::IoRedirector* g_vfs_hook_redirector = nullptr;

NYX_RUNTIME_DOCTOR_NOINLINE int hook_probe_value(int seed) {
    volatile int value = seed;
    value += 13;
    value -= 13;
    value ^= 0x5a;
    value ^= 0x5a;
    return value + (kHookProbeValue - kHookProbeInput);
}

NYX_RUNTIME_DOCTOR_NOINLINE int hook_probe_replacement(int seed) {
    if (g_inline_hook_snapshot != nullptr) {
        runtime::stack::StackNormalizer normalizer;
        static_cast<void>(normalizer.capture(runtime::stack::StackNormalizeRequest{}, g_inline_hook_snapshot));
    }

    volatile int value = seed;
    value += 29;
    value -= 29;
    value ^= 0xa5;
    value ^= 0xa5;
    return value + (kHookReplacementValue - kHookProbeInput);
}

NYX_RUNTIME_DOCTOR_NOINLINE int sdk_hook_probe_value(int seed) {
    volatile int value = seed;
    value += 17;
    value -= 17;
    value ^= 0x33;
    value ^= 0x33;
    return value + (kHookProbeValue - kHookProbeInput);
}

NYX_RUNTIME_DOCTOR_NOINLINE int sdk_hook_probe_replacement(int seed) {
    volatile int value = seed;
    value += 31;
    value -= 31;
    value ^= 0xcc;
    value ^= 0xcc;
    return value + (kHookReplacementValue - kHookProbeInput);
}

void* hook_address(HookProbe probe) {
    return reinterpret_cast<void*>(probe);
}

const char* self_library_name() {
    return NYX_NATIVE_LIBRARY_NAME;
}

NYX_RUNTIME_DOCTOR_NOINLINE int call_hook_probe() {
    return hook_probe_value(kHookProbeInput);
}

NYX_RUNTIME_DOCTOR_NOINLINE int call_sdk_hook_probe() {
    return sdk_hook_probe_value(kHookProbeInput);
}

extern "C" NYX_RUNTIME_DOCTOR_NOINLINE pid_t plt_probe_replacement() {
    PLT_SCOPE();
    if (g_plt_hook_snapshot != nullptr) {
        runtime::stack::StackNormalizer normalizer;
        static_cast<void>(normalizer.capture(runtime::stack::StackNormalizeRequest{}, g_plt_hook_snapshot));
    }
    return -1;
}

int errno_for(runtime::RuntimeStatus status) {
    switch (status) {
        case runtime::RuntimeStatus::NotFound:
            return ENOENT;
        case runtime::RuntimeStatus::InvalidArgument:
            return EINVAL;
        case runtime::RuntimeStatus::Denied:
            return EACCES;
        case runtime::RuntimeStatus::Ok:
            return 0;
        case runtime::RuntimeStatus::Disabled:
        case runtime::RuntimeStatus::Unavailable:
        case runtime::RuntimeStatus::Failed:
            return EIO;
    }

    return EIO;
}

bool uses_mode(int flags) {
    return (flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE;
}

int fd_from(const runtime::RuntimeResult& result, const runtime::vfs::OpenHandle& handle) {
    if (!result.ok()) {
        errno = errno_for(result.status);
        return -1;
    }

    return handle.fd;
}

int stat_from(const runtime::RuntimeResult& result) {
    if (!result.ok()) {
        errno = errno_for(result.status);
        return -1;
    }

    return 0;
}

extern "C" int vfs_open_replacement(const char* path, int flags, ...) {
    PLT_SCOPE();
    if (g_vfs_hook_redirector == nullptr || path == nullptr) {
        errno = EIO;
        return -1;
    }

    va_list args;
    va_start(args, flags);
    const int mode = uses_mode(flags) ? va_arg(args, int) : 0;
    va_end(args);

    runtime::vfs::OpenHandle handle;
    const auto result = g_vfs_hook_redirector->open(runtime::vfs::OpenRequest{path, flags, mode}, &handle);
    return fd_from(result, handle);
}

extern "C" int vfs_openat_replacement(int dir_fd, const char* path, int flags, ...) {
    PLT_SCOPE();
    if (g_vfs_hook_redirector == nullptr || path == nullptr) {
        errno = EIO;
        return -1;
    }

    va_list args;
    va_start(args, flags);
    const int mode = uses_mode(flags) ? va_arg(args, int) : 0;
    va_end(args);

    runtime::vfs::OpenHandle handle;
    const auto result = g_vfs_hook_redirector->open_at(runtime::vfs::OpenAtRequest{dir_fd, path, flags, mode}, &handle);
    return fd_from(result, handle);
}

extern "C" int vfs_stat_replacement(const char* path, struct stat* out) {
    PLT_SCOPE();
    if (g_vfs_hook_redirector == nullptr || path == nullptr || out == nullptr) {
        errno = EIO;
        return -1;
    }

    const auto result = g_vfs_hook_redirector->stat_path(runtime::vfs::StatRequest{path, 0}, out);
    return stat_from(result);
}

extern "C" int vfs_lstat_replacement(const char* path, struct stat* out) {
    PLT_SCOPE();
    if (g_vfs_hook_redirector == nullptr || path == nullptr || out == nullptr) {
        errno = EIO;
        return -1;
    }

    const auto result = g_vfs_hook_redirector->lstat_path(runtime::vfs::StatRequest{path, 0}, out);
    return stat_from(result);
}

extern "C" int vfs_fstatat_replacement(int dir_fd, const char* path, struct stat* out, int flags) {
    PLT_SCOPE();
    if (g_vfs_hook_redirector == nullptr || path == nullptr || out == nullptr) {
        errno = EIO;
        return -1;
    }

    const auto result = g_vfs_hook_redirector->fstat_at(runtime::vfs::StatAtRequest{dir_fd, path, flags}, out);
    return stat_from(result);
}

bool check_unity_unavailable() {
    bool ok = true;

    engines::unity::Il2CppResolver resolver;
    const auto probe = resolver.probe();
    ok = expect_engine_status(
        "Unity resolver unavailable without libil2cpp",
        probe.status,
        engines::EngineStatus::Unavailable
    ) && ok;

    std::vector<engines::unity::Il2CppImageView> images;
    const auto images_result = resolver.images(&images);
    ok = expect_status(
        "Unity images unavailable without libil2cpp",
        images_result.status,
        runtime::RuntimeStatus::Unavailable
    ) && ok;
    ok = expect_true("Unity images kept empty without libil2cpp", images.empty()) && ok;

    engines::unity::Il2CppImageView image;
    const auto image_result = resolver.find_image(engines::unity::Il2CppImageQuery{"Assembly-CSharp.dll"}, &image);
    ok = expect_status(
        "Unity image query unavailable without libil2cpp",
        image_result.status,
        runtime::RuntimeStatus::Unavailable
    ) && ok;
    ok = expect_false("Unity image query produced no image", image.valid()) && ok;

    return ok;
}

bool check_hook_registry_states() {
    bool ok = true;

    runtime::hook::HookRegistry registry;
    FakeHookBackend ok_backend(runtime::RuntimeStatus::Ok, runtime::RuntimeStatus::Ok);

    auto missing_install = registry.install("runtime_doctor", "missing_hook", ok_backend);
    ok = expect_status("hook install reports missing record", missing_install.status, runtime::RuntimeStatus::NotFound) && ok;

    auto missing_remove = registry.remove("runtime_doctor", "missing_hook", ok_backend);
    ok = expect_status("hook remove reports missing record", missing_remove.status, runtime::RuntimeStatus::NotFound) && ok;

    runtime::hook::HookRecord hook;
    hook.owner = "runtime_doctor";
    hook.target = "fake_probe";
    hook.kind = runtime::hook::HookKind::Inline;
    hook.target_address = reinterpret_cast<void*>(0x1);
    hook.replacement = reinterpret_cast<void*>(0x2);
    ok = expect_true("hook state record add", registry.add(hook)) && ok;

    auto remove_before_install = registry.remove("runtime_doctor", "fake_probe", ok_backend);
    ok = expect_status(
        "hook remove before install rejected",
        remove_before_install.status,
        runtime::RuntimeStatus::InvalidArgument
    ) && ok;
    ok = expect_hook_state(
        "hook remove before install keeps state",
        registry,
        "runtime_doctor",
        "fake_probe",
        runtime::hook::HookState::Registered
    ) && ok;
    ok = expect_int("hook remove before install skips backend", ok_backend.remove_count(), 0) && ok;

    auto install = registry.install("runtime_doctor", "fake_probe", ok_backend);
    ok = expect_status("hook install succeeds", install.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "hook install marks installed",
        registry,
        "runtime_doctor",
        "fake_probe",
        runtime::hook::HookState::Installed
    ) && ok;
    ok = expect_int("hook install calls backend once", ok_backend.install_count(), 1) && ok;

    auto install_again = registry.install("runtime_doctor", "fake_probe", ok_backend);
    ok = expect_status("hook duplicate install is stable", install_again.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_int("hook duplicate install skips backend", ok_backend.install_count(), 1) && ok;

    runtime::hook::HookRecord conflict_hook = hook;
    conflict_hook.replacement = reinterpret_cast<void*>(0x3);
    ok = expect_false("hook installed conflict rejected", registry.add(conflict_hook)) && ok;
    ok = expect_hook_state(
        "hook installed conflict keeps state",
        registry,
        "runtime_doctor",
        "fake_probe",
        runtime::hook::HookState::Installed
    ) && ok;

    FakeHookBackend fail_remove(runtime::RuntimeStatus::Ok, runtime::RuntimeStatus::Failed);
    auto remove_failure = registry.remove("runtime_doctor", "fake_probe", fail_remove);
    ok = expect_status("hook remove failure reported", remove_failure.status, runtime::RuntimeStatus::Failed) && ok;
    ok = expect_hook_state(
        "hook remove failure keeps installed",
        registry,
        "runtime_doctor",
        "fake_probe",
        runtime::hook::HookState::Installed
    ) && ok;

    auto remove = registry.remove("runtime_doctor", "fake_probe", ok_backend);
    ok = expect_status("hook remove succeeds", remove.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "hook remove marks removed",
        registry,
        "runtime_doctor",
        "fake_probe",
        runtime::hook::HookState::Removed
    ) && ok;
    ok = expect_int("hook remove calls backend once", ok_backend.remove_count(), 1) && ok;

    auto remove_again = registry.remove("runtime_doctor", "fake_probe", ok_backend);
    ok = expect_status("hook duplicate remove is stable", remove_again.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_int("hook duplicate remove skips backend", ok_backend.remove_count(), 1) && ok;

    auto reinstall = registry.install("runtime_doctor", "fake_probe", ok_backend);
    ok = expect_status("hook reinstall after remove succeeds", reinstall.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "hook reinstall marks installed",
        registry,
        "runtime_doctor",
        "fake_probe",
        runtime::hook::HookState::Installed
    ) && ok;
    ok = expect_int("hook reinstall calls backend again", ok_backend.install_count(), 2) && ok;

    runtime::hook::HookRecord failing_hook;
    failing_hook.owner = "runtime_doctor";
    failing_hook.target = "fake_install_failure";
    failing_hook.kind = runtime::hook::HookKind::Inline;
    failing_hook.target_address = reinterpret_cast<void*>(0x4);
    failing_hook.replacement = reinterpret_cast<void*>(0x5);
    ok = expect_true("hook failing record add", registry.add(failing_hook)) && ok;

    FakeHookBackend fail_install(runtime::RuntimeStatus::Failed, runtime::RuntimeStatus::Ok);
    auto install_failure = registry.install("runtime_doctor", "fake_install_failure", fail_install);
    ok = expect_status("hook install failure reported", install_failure.status, runtime::RuntimeStatus::Failed) && ok;
    ok = expect_hook_state(
        "hook install failure marks failed",
        registry,
        "runtime_doctor",
        "fake_install_failure",
        runtime::hook::HookState::Failed
    ) && ok;

    auto install_after_failure = registry.install("runtime_doctor", "fake_install_failure", ok_backend);
    ok = expect_status("hook install after failure succeeds", install_after_failure.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "hook install after failure marks installed",
        registry,
        "runtime_doctor",
        "fake_install_failure",
        runtime::hook::HookState::Installed
    ) && ok;

    return ok;
}

bool check_inline_hook() {
    bool ok = true;

    runtime::hook::HookRegistry registry;
    runtime::hook::HookRecord hook;
    hook.owner = "runtime_doctor";
    hook.target = "inline_local_probe";
    hook.kind = runtime::hook::HookKind::Inline;
    hook.target_address = hook_address(hook_probe_value);
    hook.replacement = hook_address(hook_probe_replacement);

    ok = expect_int("inline hook baseline value", call_hook_probe(), kHookProbeValue) && ok;
    ok = expect_true("inline hook record add", registry.add(hook)) && ok;

    auto install = registry.install("runtime_doctor", "inline_local_probe", runtime::hook::inline_backend());
    ok = expect_status("inline hook install succeeds", install.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "inline hook marks installed",
        registry,
        "runtime_doctor",
        "inline_local_probe",
        runtime::hook::HookState::Installed
    ) && ok;
    runtime::stack::StackTraceSnapshot hooked_snapshot;
    g_inline_hook_snapshot = &hooked_snapshot;
    const int hooked_value = call_hook_probe();
    g_inline_hook_snapshot = nullptr;
    ok = expect_int("inline hook replacement value", hooked_value, kHookReplacementValue) && ok;
    ok = expect_true(
        "inline hook stack marks replacement",
        has_stack_kind(hooked_snapshot.normalized_frames, runtime::stack::StackType::HookReplacement)
    ) && ok;
    ok = expect_true(
        "inline hook stack adjusts return address",
        has_stack_status(
            hooked_snapshot.normalized_frames,
            runtime::stack::StackStatus::AdjustedReturnAddress
        )
    ) && ok;
    ok = expect_true("inline hook stack preserves raw frames", raw_frames_preserved(hooked_snapshot)) && ok;

    const auto records = registry.records();
    void* original_address = records.empty() ? nullptr : records.front().original;
    ok = expect_true("inline hook original pointer is valid", original_address != nullptr) && ok;
    if (original_address != nullptr) {
        const auto original = reinterpret_cast<HookProbe>(original_address);
        ok = expect_int("inline hook original value", original(kHookProbeInput), kHookProbeValue) && ok;
    }

    auto install_again = registry.install("runtime_doctor", "inline_local_probe", runtime::hook::inline_backend());
    ok = expect_status("inline hook duplicate install is stable", install_again.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_int("inline hook duplicate install keeps replacement", call_hook_probe(), kHookReplacementValue) && ok;

    auto remove = registry.remove("runtime_doctor", "inline_local_probe", runtime::hook::inline_backend());
    ok = expect_status("inline hook remove succeeds", remove.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "inline hook marks removed",
        registry,
        "runtime_doctor",
        "inline_local_probe",
        runtime::hook::HookState::Removed
    ) && ok;
    ok = expect_int("inline hook restored value", call_hook_probe(), kHookProbeValue) && ok;
    bool stack_record_removed = false;
    for (const auto& record : runtime::stack::StackHookRegistry().records()) {
        if (record.owner == "runtime_doctor" && record.target == "inline_local_probe") {
            stack_record_removed = !record.installed;
        }
    }
    ok = expect_true("inline hook stack record marks removed", stack_record_removed) && ok;

    auto remove_again = registry.remove("runtime_doctor", "inline_local_probe", runtime::hook::inline_backend());
    ok = expect_status("inline hook duplicate remove is stable", remove_again.status, runtime::RuntimeStatus::Ok) && ok;

    return ok;
}

bool check_sdk_hook() {
    bool ok = true;

    ok = expect_int("sdk hook baseline value", call_sdk_hook_probe(), kHookProbeValue) && ok;

    HookProbe original = nullptr;
    const auto install = hook::Inline(sdk_hook_probe_value, sdk_hook_probe_replacement, &original);

    ok = expect_sdk_status("sdk hook install succeeds", install.status, Status::Ok) && ok;
    ok = expect_true("sdk hook original pointer is valid", original != nullptr) && ok;
    ok = expect_int("sdk hook replacement value", call_sdk_hook_probe(), kHookReplacementValue) && ok;

    const auto conflict_install = hook::Inline(sdk_hook_probe_value, hook_probe_replacement);
    ok = expect_sdk_status("sdk hook conflict rejected", conflict_install.status, Status::InvalidArgument) && ok;
    ok = expect_int("sdk hook conflict keeps replacement", call_sdk_hook_probe(), kHookReplacementValue) && ok;

    if (original != nullptr) {
        ok = expect_int("sdk hook original value", original(kHookProbeInput), kHookProbeValue) && ok;
    }

    std::size_t record_count = 0;
    auto query_count = hook::Get(nullptr, &record_count);
    ok = expect_sdk_status("sdk hook query count succeeds", query_count.status, Status::Ok) && ok;
    ok = expect_true("sdk hook query reports records", record_count > 0) && ok;
    if (record_count > 0) {
        std::vector<hook::Record> records(record_count);
        auto query = hook::Get(records.data(), &record_count);
        ok = expect_sdk_status("sdk hook query records succeeds", query.status, Status::Ok) && ok;
    }

    const auto remove = hook::UnhookInline(sdk_hook_probe_value);
    ok = expect_sdk_status("sdk hook cleanup succeeds", remove.status, Status::Ok) && ok;
    ok = expect_int("sdk hook cleanup restores value", call_sdk_hook_probe(), kHookProbeValue) && ok;

    return ok;
}

bool check_sdk_hook_resolvers() {
    bool ok = true;

    const auto missing_offset = hook::ResolveOffset("libnyx_missing_hook_target.so", 0);
    ok = expect_sdk_status("sdk hook offset resolver reports missing library", missing_offset.result.status, Status::NotFound) && ok;
    ok = expect_true("sdk hook offset resolver clears missing value", missing_offset.value == nullptr) && ok;

    const auto missing_symbol = hook::ResolveSymbol("libnyx_missing_hook_target.so", "nyx_missing_symbol");
    ok = expect_sdk_status("sdk hook symbol resolver reports missing library", missing_symbol.result.status, Status::NotFound) && ok;
    ok = expect_true("sdk hook symbol resolver clears missing value", missing_symbol.value == nullptr) && ok;

    const auto base_value = hook::ResolveOffset(self_library_name(), 0);
    ok = expect_sdk_status("sdk hook offset base resolves", base_value.result.status, Status::Ok) && ok;
    ok = expect_true("sdk hook offset base is valid", base_value.value != nullptr) && ok;
    const auto target_address = reinterpret_cast<std::uintptr_t>(hook_address(sdk_hook_probe_value));
    if (base_value.ok() && base_value.value != nullptr) {
        const auto base = reinterpret_cast<std::uintptr_t>(base_value.value);
        ok = expect_true("sdk hook offset target is after base", target_address >= base) && ok;
        if (target_address < base) {
            return ok;
        }
        const std::uintptr_t offset = target_address - base;
        const auto resolved = hook::ResolveOffset(self_library_name(), offset);
        ok = expect_sdk_status("sdk hook offset resolver succeeds", resolved.result.status, Status::Ok) && ok;
        ok = expect_true("sdk hook offset resolver returns target", resolved.value == hook_address(sdk_hook_probe_value)) && ok;

        HookProbe offset_original = nullptr;
        const auto install = hook::InlineOffset(
            self_library_name(),
            offset,
            sdk_hook_probe_replacement,
            &offset_original
        );
        ok = expect_sdk_status("sdk hook offset install succeeds", install.status, Status::Ok) && ok;
        ok = expect_true("sdk hook offset original pointer is valid", offset_original != nullptr) && ok;
        ok = expect_int("sdk hook offset replacement value", call_sdk_hook_probe(), kHookReplacementValue) && ok;
        if (offset_original != nullptr) {
            ok = expect_int("sdk hook offset original value", offset_original(kHookProbeInput), kHookProbeValue) && ok;
        }

        const auto remove = hook::UnhookInlineOffset(self_library_name(), offset);
        ok = expect_sdk_status("sdk hook offset cleanup succeeds", remove.status, Status::Ok) && ok;
        ok = expect_int("sdk hook offset cleanup restores value", call_sdk_hook_probe(), kHookProbeValue) && ok;
    }

#if NYX_DEBUG_MODE
    runtime::loader::NativeLibrary loader;
    runtime::loader::LoadHandle caller_handle;
    const auto caller_load = loader.load(runtime::loader::LoadRequest{NYX_PLT_CALLER_LIBRARY, 0}, &caller_handle);
    ok = expect_status("sdk hook symbol caller loads", caller_load.status, runtime::RuntimeStatus::Ok) && ok;
    if (caller_load.ok() && caller_handle.valid()) {
        const auto resolved = hook::ResolveSymbol(NYX_PLT_CALLER_LIBRARY, NYX_PLT_CALL_SYMBOL);
        ok = expect_sdk_status("sdk hook symbol resolver succeeds", resolved.result.status, Status::Ok) && ok;
        ok = expect_true("sdk hook symbol resolver returns address", resolved.value != nullptr) && ok;

        const auto missing = hook::ResolveSymbol(NYX_PLT_CALLER_LIBRARY, "nyx_missing_inline_symbol");
        ok = expect_sdk_status("sdk hook symbol resolver reports missing symbol", missing.result.status, Status::NotFound) && ok;

        if (resolved.value != nullptr) {
            const auto call_probe = reinterpret_cast<HookProbe>(resolved.value);
            ok = expect_int("sdk hook symbol baseline value", call_probe(NYX_PLT_PROBE_INPUT), NYX_PLT_PROBE_EXPECTED) && ok;

            HookProbe symbol_original = nullptr;
            const auto install = hook::InlineSymbol(
                NYX_PLT_CALLER_LIBRARY,
                NYX_PLT_CALL_SYMBOL,
                hook_probe_replacement,
                &symbol_original
            );
            ok = expect_sdk_status("sdk hook symbol install succeeds", install.status, Status::Ok) && ok;
            ok = expect_true("sdk hook symbol original pointer is valid", symbol_original != nullptr) && ok;
            ok = expect_int("sdk hook symbol replacement value", call_probe(NYX_PLT_PROBE_INPUT), kHookReplacementValue) && ok;

            const auto remove = hook::UnhookInlineSymbol(NYX_PLT_CALLER_LIBRARY, NYX_PLT_CALL_SYMBOL);
            ok = expect_sdk_status("sdk hook symbol cleanup succeeds", remove.status, Status::Ok) && ok;
            ok = expect_int("sdk hook symbol cleanup restores value", call_probe(NYX_PLT_PROBE_INPUT), NYX_PLT_PROBE_EXPECTED) && ok;
        }

        const auto close = loader.close(&caller_handle);
        ok = expect_status("sdk hook symbol caller closes", close.status, runtime::RuntimeStatus::Ok) && ok;
    }
#else
    NYX_LOGI("runtime doctor SDK symbol hook probe skipped outside Debug build");
#endif

    return ok;
}

bool check_sdk_plt_hook() {
    bool ok = true;

    const auto invalid_install = hook::Plt(hook::PltOptions{});
    ok = expect_sdk_status("sdk PLT hook invalid request rejected", invalid_install.status, Status::InvalidArgument) && ok;

    if (!runtime::hook::plt_hook_available()) {
        hook::PltOptions request;
        request.caller = NYX_PLT_CALLER_LIBRARY;
        request.callee = NYX_PLT_CALLEE_LIBRARY;
        request.symbol = NYX_PLT_TARGET_SYMBOL;
        request.replacement = reinterpret_cast<void*>(plt_probe_replacement);

        const auto install = hook::Plt(request);
        ok = expect_sdk_status("sdk PLT hook unavailable install", install.status, Status::Unavailable) && ok;
        return ok;
    }

#if !NYX_DEBUG_MODE
    NYX_LOGI("runtime doctor SDK PLT hook probe skipped outside Debug build");
    return ok;
#else
    hook::PltOptions all_request;
    all_request.symbol = "nyx_missing_plt_all_probe";
    all_request.replacement = reinterpret_cast<void*>(plt_probe_replacement);
    const auto all_install = hook::Plt(all_request);
    ok = expect_sdk_status("sdk PLT hook all request installs", all_install.status, Status::Ok) && ok;
    const auto all_remove = hook::UnhookPlt(all_request);
    ok = expect_sdk_status("sdk PLT hook all request removes", all_remove.status, Status::Ok) && ok;

    runtime::loader::NativeLibrary loader;
    runtime::loader::LoadHandle caller_handle;
    auto caller_load = loader.load(runtime::loader::LoadRequest{NYX_PLT_CALLER_LIBRARY, 0}, &caller_handle);
    ok = expect_status("sdk PLT hook caller loads", caller_load.status, runtime::RuntimeStatus::Ok) && ok;

    runtime::loader::Symbol call_symbol;
    auto symbol_result = loader.find_symbol(
        runtime::loader::SymbolRequest{caller_handle.handle, NYX_PLT_CALL_SYMBOL},
        &call_symbol
    );
    ok = expect_status("sdk PLT hook caller symbol resolves", symbol_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("sdk PLT hook caller symbol is valid", call_symbol.found()) && ok;

    if (call_symbol.found()) {
        const auto call_probe = reinterpret_cast<PltProbe>(call_symbol.address);
        ok = expect_int(
            "sdk PLT hook baseline value",
            call_probe(NYX_PLT_PROBE_INPUT),
            NYX_PLT_PROBE_EXPECTED
        ) && ok;

        PidFn original_address = nullptr;
        const auto install = hook::Plt(
            NYX_PLT_TARGET_SYMBOL,
            plt_probe_replacement,
            &original_address,
            NYX_PLT_CALLER_LIBRARY
        );
        ok = expect_sdk_status("sdk PLT hook install succeeds", install.status, Status::Ok) && ok;
        ok = expect_true("sdk PLT hook original pointer is valid", original_address != nullptr) && ok;
        ok = expect_int(
            "sdk PLT hook replacement value",
            call_probe(NYX_PLT_PROBE_INPUT),
            NYX_PLT_REPLACEMENT_EXPECTED
        ) && ok;

        std::size_t plt_record_count = 0;
        auto plt_query_count = hook::Get(nullptr, &plt_record_count);
        ok = expect_sdk_status("sdk PLT hook query count succeeds", plt_query_count.status, Status::Ok) && ok;
        bool plt_hit_reported = false;
        if (plt_record_count > 0) {
            std::vector<hook::Record> records(plt_record_count);
            auto plt_query = hook::Get(records.data(), &plt_record_count);
            ok = expect_sdk_status("sdk PLT hook query records succeeds", plt_query.status, Status::Ok) && ok;
            for (const auto& record : records) {
                if (record.kind == hook::Kind::Plt && record.symbol == NYX_PLT_TARGET_SYMBOL) {
                    plt_hit_reported = record.hit_count > 0;
                }
            }
        }
        ok = expect_true("sdk PLT hook query reports hit", plt_hit_reported) && ok;

        hook::PltOptions conflict_options;
        conflict_options.symbol = NYX_PLT_TARGET_SYMBOL;
        conflict_options.caller = NYX_PLT_CALLER_LIBRARY;
        conflict_options.replacement = reinterpret_cast<void*>(vfs_open_replacement);
        const auto conflict_install = hook::Plt(conflict_options);
        ok = expect_sdk_status("sdk PLT hook conflict rejected", conflict_install.status, Status::InvalidArgument) && ok;
        ok = expect_int(
            "sdk PLT hook conflict keeps replacement",
            call_probe(NYX_PLT_PROBE_INPUT),
            NYX_PLT_REPLACEMENT_EXPECTED
        ) && ok;

        const auto remove = hook::UnhookPlt(NYX_PLT_TARGET_SYMBOL, NYX_PLT_CALLER_LIBRARY);
        ok = expect_sdk_status("sdk PLT hook cleanup succeeds", remove.status, Status::Ok) && ok;
        ok = expect_int(
            "sdk PLT hook cleanup restores value",
            call_probe(NYX_PLT_PROBE_INPUT),
            NYX_PLT_PROBE_EXPECTED
        ) && ok;

        const auto remove_again = hook::UnhookPlt(NYX_PLT_TARGET_SYMBOL, NYX_PLT_CALLER_LIBRARY);
        ok = expect_sdk_status("sdk PLT hook duplicate cleanup succeeds", remove_again.status, Status::Ok) && ok;
    }

    if (caller_handle.valid()) {
        auto close = loader.close(&caller_handle);
        ok = expect_status("sdk PLT hook caller closes", close.status, runtime::RuntimeStatus::Ok) && ok;
    }

    return ok;
#endif
}

bool check_plt_backend() {
    bool ok = true;

    if (!runtime::hook::plt_hook_available()) {
        runtime::hook::HookRegistry registry;
        runtime::hook::HookRecord hook;
        hook.owner = "runtime_doctor";
        hook.target = "plt_hook_probe";
        hook.kind = runtime::hook::HookKind::Plt;
        hook.callee_path = "libc.so";
        hook.symbol = "strlen";
        hook.replacement = reinterpret_cast<void*>(0x2);

        ok = expect_true("PLT hook record add", registry.add(hook)) && ok;

        auto install = registry.install("runtime_doctor", "plt_hook_probe", runtime::hook::plt_backend());
        ok = expect_status("PLT hook ABI unavailable", install.status, runtime::RuntimeStatus::Unavailable) && ok;
        ok = expect_hook_state(
            "PLT hook unavailable keeps registered",
            registry,
            "runtime_doctor",
            "plt_hook_probe",
            runtime::hook::HookState::Registered
        ) && ok;
        return ok;
    }

    runtime::hook::HookRegistry invalid_registry;
    runtime::hook::HookRecord invalid_hook;
    invalid_hook.owner = "runtime_doctor";
    invalid_hook.target = "plt_invalid_probe";
    invalid_hook.kind = runtime::hook::HookKind::Plt;
    ok = expect_true("PLT hook invalid record add", invalid_registry.add(invalid_hook)) && ok;

    auto invalid_install = invalid_registry.install(
        "runtime_doctor",
        "plt_invalid_probe",
        runtime::hook::plt_backend()
    );
    ok = expect_status(
        "PLT hook invalid hook rejected",
        invalid_install.status,
        runtime::RuntimeStatus::InvalidArgument
    ) && ok;
    ok = expect_hook_state(
        "PLT hook invalid hook keeps registered",
        invalid_registry,
        "runtime_doctor",
        "plt_invalid_probe",
        runtime::hook::HookState::Registered
    ) && ok;

#if !NYX_DEBUG_MODE
    NYX_LOGI("runtime doctor PLT hook probe skipped outside Debug build");
    return ok;
#else
    runtime::loader::NativeLibrary loader;
    runtime::loader::LoadHandle caller_handle;
    auto caller_load = loader.load(runtime::loader::LoadRequest{NYX_PLT_CALLER_LIBRARY, 0}, &caller_handle);
    ok = expect_status("PLT hook caller loads", caller_load.status, runtime::RuntimeStatus::Ok) && ok;

    runtime::loader::Symbol call_symbol;
    auto symbol_result = loader.find_symbol(
        runtime::loader::SymbolRequest{caller_handle.handle, NYX_PLT_CALL_SYMBOL},
        &call_symbol
    );
    ok = expect_status("PLT hook caller symbol resolves", symbol_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("PLT hook caller symbol is valid", call_symbol.found()) && ok;

    if (call_symbol.found()) {
        const auto call_probe = reinterpret_cast<PltProbe>(call_symbol.address);
        ok = expect_int(
            "PLT hook baseline value",
            call_probe(NYX_PLT_PROBE_INPUT),
            NYX_PLT_PROBE_EXPECTED
        ) && ok;

        runtime::hook::HookRegistry registry;
        runtime::hook::HookRecord hook;
        hook.owner = "runtime_doctor";
        hook.target = "plt_local_probe";
        hook.kind = runtime::hook::HookKind::Plt;
        hook.caller_path = NYX_PLT_CALLER_LIBRARY;
        hook.callee_path = NYX_PLT_CALLEE_LIBRARY;
        hook.symbol = NYX_PLT_TARGET_SYMBOL;
        hook.replacement = reinterpret_cast<void*>(plt_probe_replacement);
        ok = expect_true("PLT hook record add", registry.add(hook)) && ok;

        auto install = registry.install(
            "runtime_doctor",
            "plt_local_probe",
            runtime::hook::plt_backend()
        );
        ok = expect_status("PLT hook install succeeds", install.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_hook_state(
            "PLT hook marks installed",
            registry,
            "runtime_doctor",
            "plt_local_probe",
            runtime::hook::HookState::Installed
        ) && ok;
        runtime::stack::StackTraceSnapshot hooked_snapshot;
        g_plt_hook_snapshot = &hooked_snapshot;
        const int hooked_value = call_probe(NYX_PLT_PROBE_INPUT);
        g_plt_hook_snapshot = nullptr;
        ok = expect_int("PLT hook replacement value", hooked_value, NYX_PLT_REPLACEMENT_EXPECTED) && ok;
        ok = expect_true(
            "PLT hook stack marks replacement",
            has_stack_kind(hooked_snapshot.normalized_frames, runtime::stack::StackType::HookReplacement)
        ) && ok;
        ok = expect_true(
            "PLT hook stack adjusts return address",
            has_stack_status(
                hooked_snapshot.normalized_frames,
                runtime::stack::StackStatus::AdjustedReturnAddress
            )
        ) && ok;
        ok = expect_true("PLT hook stack preserves raw frames", raw_frames_preserved(hooked_snapshot)) && ok;

        auto install_again = registry.install(
            "runtime_doctor",
            "plt_local_probe",
            runtime::hook::plt_backend()
        );
        ok = expect_status("PLT hook duplicate install is stable", install_again.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_int(
            "PLT hook duplicate install keeps replacement",
            call_probe(NYX_PLT_PROBE_INPUT),
            NYX_PLT_REPLACEMENT_EXPECTED
        ) && ok;

        auto remove = registry.remove(
            "runtime_doctor",
            "plt_local_probe",
            runtime::hook::plt_backend()
        );
        ok = expect_status("PLT hook remove succeeds", remove.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_hook_state(
            "PLT hook marks removed",
            registry,
            "runtime_doctor",
            "plt_local_probe",
            runtime::hook::HookState::Removed
        ) && ok;
        ok = expect_int(
            "PLT hook restored value",
            call_probe(NYX_PLT_PROBE_INPUT),
            NYX_PLT_PROBE_EXPECTED
        ) && ok;

        auto remove_again = registry.remove(
            "runtime_doctor",
            "plt_local_probe",
            runtime::hook::plt_backend()
        );
        ok = expect_status("PLT hook duplicate remove is stable", remove_again.status, runtime::RuntimeStatus::Ok) && ok;
    }

    if (caller_handle.valid()) {
        auto close = loader.close(&caller_handle);
        ok = expect_status("PLT hook caller closes", close.status, runtime::RuntimeStatus::Ok) && ok;
    }

    return ok;
#endif
}

bool check_vfs_redirector() {
    bool ok = true;

    runtime::vfs::PathPolicy policy;
    policy.private_root = "/data/data/dev.nyxcore.manager/files";
    const std::string overlay_root = "/data/data/dev.nyxcore.overlay/files";
    policy.allowed_roots.push_back(overlay_root);
    runtime::vfs::PathMapper mapper(policy);

    const std::string virtual_path = "/nyx/virtual/vfs_doctor.txt";
    const std::string target_path = policy.private_root + "/vfs_doctor.txt";
    const std::string external_target_path = "/sdcard/nyx_vfs_asset.txt";
    const std::string overlay_target_path = overlay_root + "/vfs_overlay.txt";
    const std::string unconfigured_path = "/nyx/virtual/unconfigured.txt";

    auto traversal = mapper.add(runtime::vfs::PathRule{"/nyx/../escape", target_path});
    ok = expect_status("path mapper rejects traversal", traversal.status, runtime::RuntimeStatus::Denied) && ok;

    auto outside = mapper.add(runtime::vfs::PathRule{"/nyx/virtual/outside.txt", "/proc/nyx_outside.txt"});
    ok = expect_status("path mapper rejects outside target", outside.status, runtime::RuntimeStatus::Denied) && ok;

    auto external = mapper.add(runtime::vfs::PathRule{"/nyx/virtual/external.txt", external_target_path});
    ok = expect_status("path mapper allows common asset root", external.status, runtime::RuntimeStatus::Ok) && ok;

    auto overlay = mapper.add(runtime::vfs::PathRule{"/nyx/virtual/overlay.txt", overlay_target_path});
    ok = expect_status("path mapper allows configured overlay root", overlay.status, runtime::RuntimeStatus::Ok) && ok;

    std::string missing;
    auto missing_map = mapper.map(unconfigured_path, &missing);
    ok = expect_status("path mapper reports unconfigured path", missing_map.status, runtime::RuntimeStatus::NotFound) && ok;

    auto add = mapper.add(runtime::vfs::PathRule{virtual_path, target_path});
    ok = expect_status("path mapper adds VFS rule", add.status, runtime::RuntimeStatus::Ok) && ok;

    std::string mapped;
    auto map = mapper.map(virtual_path, &mapped);
    ok = expect_status("path mapper maps configured path", map.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_string("path mapper mapped target", mapped, target_path) && ok;

    runtime::vfs::IoRedirector redirector(mapper);
    runtime::vfs::PathDecision decision;
    auto resolve = redirector.resolve(virtual_path, O_RDONLY, &decision);
    ok = expect_status("VFS resolves configured path", resolve.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("VFS marks configured path redirected", decision.redirected) && ok;
    ok = expect_string("VFS decision keeps source path", decision.path, virtual_path) && ok;
    ok = expect_string("VFS decision maps target path", decision.target, target_path) && ok;

    runtime::vfs::PathDecision direct_decision;
    auto direct_resolve = redirector.resolve(unconfigured_path, O_RDONLY, &direct_decision);
    ok = expect_status("VFS resolves unconfigured path", direct_resolve.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_false("VFS keeps unconfigured path direct", direct_decision.redirected) && ok;
    ok = expect_string("VFS direct target is original path", direct_decision.target, unconfigured_path) && ok;

    runtime::vfs::OpenHandle missing_handle;
    auto missing_open = redirector.open(runtime::vfs::OpenRequest{unconfigured_path, O_RDONLY, 0600}, &missing_handle);
    ok = expect_status("VFS unconfigured open reports real failure", missing_open.status, runtime::RuntimeStatus::NotFound) && ok;
    ok = expect_false("VFS unconfigured open produced no handle", missing_handle.valid()) && ok;

    struct stat missing_stat {};
    auto missing_stat_result = redirector.stat_path(runtime::vfs::StatRequest{unconfigured_path, 0}, &missing_stat);
    ok = expect_status(
        "VFS unconfigured stat reports real failure",
        missing_stat_result.status,
        runtime::RuntimeStatus::NotFound
    ) && ok;

    ::unlink(target_path.c_str());

    runtime::vfs::OpenHandle write_handle;
    auto write_open = redirector.open(
        runtime::vfs::OpenRequest{virtual_path, O_CREAT | O_RDWR | O_TRUNC, 0600},
        &write_handle
    );
    ok = expect_status("VFS redirected open succeeds", write_open.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("VFS redirected open produced handle", write_handle.valid()) && ok;
    ok = expect_true("VFS open handle is redirected", write_handle.redirected) && ok;
    ok = expect_string("VFS open handle target", write_handle.target, target_path) && ok;

    const char payload[] = "nyx-vfs";
    constexpr int payload_size = static_cast<int>(sizeof(payload) - 1);
    if (write_handle.valid()) {
        const auto written = ::write(write_handle.fd, payload, payload_size);
        ok = expect_int("VFS writes redirected file", static_cast<int>(written), payload_size) && ok;
        const auto seek = ::lseek(write_handle.fd, 0, SEEK_SET);
        ok = expect_true("VFS seeks redirected file", seek == 0) && ok;
        char buffer[payload_size] = {};
        const auto read_count = ::read(write_handle.fd, buffer, payload_size);
        ok = expect_int("VFS reads redirected file", static_cast<int>(read_count), payload_size) && ok;
        ok = expect_true("VFS redirected payload matches", std::memcmp(buffer, payload, payload_size) == 0) && ok;

        auto close = redirector.close(&write_handle);
        ok = expect_status("VFS closes redirected handle", close.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_false("VFS redirected handle cleared", write_handle.valid()) && ok;
    }

    runtime::vfs::OpenHandle read_handle;
    auto read_open = redirector.open_at(
        runtime::vfs::OpenAtRequest{runtime::vfs::kCurrentDirectoryFd, virtual_path, O_RDONLY, 0600},
        &read_handle
    );
    ok = expect_status("VFS openat redirected read succeeds", read_open.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("VFS openat handle is redirected", read_handle.redirected) && ok;
    if (read_handle.valid()) {
        char buffer[payload_size] = {};
        const auto read_count = ::read(read_handle.fd, buffer, payload_size);
        ok = expect_int("VFS openat reads payload", static_cast<int>(read_count), payload_size) && ok;
        ok = expect_true("VFS openat payload matches", std::memcmp(buffer, payload, payload_size) == 0) && ok;

        auto close = redirector.close(&read_handle);
        ok = expect_status("VFS closes openat handle", close.status, runtime::RuntimeStatus::Ok) && ok;
    }

    struct stat stat_info {};
    auto stat_result = redirector.stat_path(runtime::vfs::StatRequest{virtual_path, 0}, &stat_info);
    ok = expect_status("VFS redirected stat succeeds", stat_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_int("VFS redirected stat reports size", static_cast<int>(stat_info.st_size), payload_size) && ok;

    struct stat lstat_info {};
    auto lstat_result = redirector.lstat_path(runtime::vfs::StatRequest{virtual_path, 0}, &lstat_info);
    ok = expect_status("VFS redirected lstat succeeds", lstat_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_int("VFS redirected lstat reports size", static_cast<int>(lstat_info.st_size), payload_size) && ok;

    struct stat fstatat_info {};
    auto fstatat_result = redirector.fstat_at(
        runtime::vfs::StatAtRequest{runtime::vfs::kCurrentDirectoryFd, virtual_path, 0},
        &fstatat_info
    );
    ok = expect_status("VFS redirected fstatat succeeds", fstatat_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_int("VFS redirected fstatat reports size", static_cast<int>(fstatat_info.st_size), payload_size) && ok;

    runtime::vfs::PathPolicy read_only_policy;
    read_only_policy.private_root = policy.private_root;
    read_only_policy.read_only = true;
    runtime::vfs::PathMapper read_only_mapper(read_only_policy);
    auto read_only_add = read_only_mapper.add(runtime::vfs::PathRule{"/nyx/virtual/readonly.txt", target_path});
    ok = expect_status("VFS read-only rule adds", read_only_add.status, runtime::RuntimeStatus::Ok) && ok;

    runtime::vfs::IoRedirector read_only_redirector(read_only_mapper);
    runtime::vfs::OpenHandle denied_handle;
    auto denied_open = read_only_redirector.open(
        runtime::vfs::OpenRequest{"/nyx/virtual/readonly.txt", O_CREAT | O_WRONLY | O_TRUNC, 0600},
        &denied_handle
    );
    ok = expect_status("VFS read-only policy rejects write", denied_open.status, runtime::RuntimeStatus::Denied) && ok;
    ok = expect_false("VFS read-only denied no handle", denied_handle.valid()) && ok;

    ::unlink(target_path.c_str());

    return ok;
}

bool check_sdk_vfs_contract() {
    bool ok = true;

    const std::string root = "/data/data/dev.nyxcore.manager/files";
    const std::string virtual_path = "/nyx/virtual/sdk_vfs.txt";
    const std::string target_path = root + "/sdk_vfs.txt";
    const std::string overlay_root = "/data/data/dev.nyxcore.overlay/files";
    const std::string overlay_target_path = overlay_root + "/sdk_overlay.txt";
    const char* allowed_roots[] = {overlay_root.c_str()};

    vfs::Config config;
    config.private_root = root.c_str();
    config.allowed_roots = allowed_roots;
    config.allowed_root_count = 1;

    auto configure = vfs::Init(config);
    ok = expect_sdk_status("sdk VFS configures private root", configure.status, Status::Ok) && ok;

    auto external = vfs::Redirect("/nyx/virtual/external.txt", "/sdcard/nyx_asset.txt");
    ok = expect_sdk_status("sdk VFS allows common asset root", external.status, Status::Ok) && ok;

    auto overlay = vfs::Redirect("/nyx/virtual/overlay.txt", overlay_target_path.c_str());
    ok = expect_sdk_status("sdk VFS allows configured overlay root", overlay.status, Status::Ok) && ok;

    auto outside = vfs::Redirect("/nyx/virtual/outside.txt", "/proc/nyx_outside.txt");
    ok = expect_sdk_status("sdk VFS rejects outside target", outside.status, Status::Denied) && ok;

    auto redirect = vfs::Redirect(virtual_path.c_str(), target_path.c_str());
    ok = expect_sdk_status("sdk VFS redirects file", redirect.status, Status::Ok) && ok;

    vfs::Decision decision;
    auto query = vfs::GetRedirect(virtual_path.c_str(), O_RDONLY, &decision);
    ok = expect_sdk_status("sdk VFS queries decision", query.status, Status::Ok) && ok;
    ok = expect_true("sdk VFS decision redirects", decision.redirected) && ok;
    ok = expect_string("sdk VFS decision target", decision.target, target_path) && ok;

    auto remove = vfs::Remove(virtual_path.c_str());
    ok = expect_sdk_status("sdk VFS removes rule", remove.status, Status::Ok) && ok;

    vfs::Decision direct;
    auto direct_query = vfs::GetRedirect(virtual_path.c_str(), O_RDONLY, &direct);
    ok = expect_sdk_status("sdk VFS direct decision succeeds", direct_query.status, Status::Ok) && ok;
    ok = expect_false("sdk VFS direct decision is not redirected", direct.redirected) && ok;

    runtime::memory::MemoryMap memory_map;
    runtime::memory::MemoryMapEntry probe_entry;
    const auto probe_address = reinterpret_cast<std::uintptr_t>(&g_vfs_patch_probe);
    const auto probe_map = memory_map.find_address(probe_address, &probe_entry);
    ok = expect_status("sdk VFS patch probe maps to module", probe_map.status, runtime::RuntimeStatus::Ok) && ok;

    if (probe_map.ok()) {
        const std::string module_name = probe_entry.name().empty() ? std::string(self_library_name()) : probe_entry.name();
        const auto file_offset = probe_entry.offset + (probe_address - probe_entry.start);
        const std::uint32_t original = g_vfs_patch_probe;
        const std::uint32_t replacement = original ^ 0x00FF00FFU;

        auto null_patch = vfs::Patch(module_name.c_str(), file_offset, nullptr, sizeof(replacement));
        ok = expect_sdk_status("sdk VFS patch module rejects null data", null_patch.status, Status::InvalidArgument) && ok;

        auto missing_patch = vfs::Patch("libnyx_missing_patch_target.so", 0, &replacement, sizeof(replacement));
        ok = expect_sdk_status("sdk VFS patch module reports missing module", missing_patch.status, Status::NotFound) && ok;

        auto overflow_patch = vfs::Patch(
            module_name.c_str(),
            std::numeric_limits<std::uintptr_t>::max(),
            &replacement,
            sizeof(replacement)
        );
        ok = expect_sdk_status("sdk VFS patch module rejects invalid offset", overflow_patch.status, Status::InvalidArgument) && ok;

        const auto cross_size = static_cast<std::size_t>(probe_entry.end - probe_address + 1);
        auto cross_patch = vfs::Patch(module_name.c_str(), file_offset, &replacement, cross_size);
        ok = expect_sdk_status("sdk VFS patch module rejects cross segment range", cross_patch.status, Status::NotFound) && ok;

        vfs::ModulePatchRecord record;
        auto patch = vfs::Patch(module_name.c_str(), file_offset, &replacement, sizeof(replacement), &record);
        ok = expect_sdk_status("sdk VFS patch module records writable probe", patch.status, Status::Ok) && ok;
        ok = expect_true("sdk VFS patch module changes probe value", g_vfs_patch_probe == replacement) && ok;
        ok = expect_true(
            "sdk VFS patch record has identity",
            record.id != 0 &&
                record.status == vfs::PatchStatus::Applied &&
                record.runtime_address == probe_address &&
                record.before.size() == sizeof(original) &&
                record.after.size() == sizeof(replacement)
        ) && ok;
        if (record.before.size() == sizeof(original) && record.after.size() == sizeof(replacement)) {
            ok = expect_true(
                "sdk VFS patch record captured bytes",
                std::memcmp(record.before.data(), &original, sizeof(original)) == 0 &&
                    std::memcmp(record.after.data(), &replacement, sizeof(replacement)) == 0
            ) && ok;
        }

        auto rollback = vfs::Rollback(&record);
        ok = expect_sdk_status("sdk VFS patch rollback succeeds", rollback.status, Status::Ok) && ok;
        ok = expect_true("sdk VFS patch rollback restores probe value", g_vfs_patch_probe == original) && ok;
        ok = expect_true("sdk VFS patch record marks rollback", record.status == vfs::PatchStatus::RolledBack) && ok;

        auto scalar_patch = vfs::Patch(module_name.c_str(), file_offset, replacement);
        ok = expect_sdk_status("sdk VFS scalar patch succeeds", scalar_patch.status, Status::Ok) && ok;
        ok = expect_true("sdk VFS scalar patch changes probe value", g_vfs_patch_probe == replacement) && ok;

        auto scalar_restore = vfs::Patch(module_name.c_str(), file_offset, original);
        ok = expect_sdk_status("sdk VFS scalar patch restores probe", scalar_restore.status, Status::Ok) && ok;
        ok = expect_true("sdk VFS scalar restore changes probe value", g_vfs_patch_probe == original) && ok;

        auto tracked_patch = vfs::PatchTracked(module_name.c_str(), file_offset, replacement);
        ok = expect_sdk_status("sdk VFS tracked patch succeeds", tracked_patch.result.status, Status::Ok) && ok;
        ok = expect_true("sdk VFS tracked patch has id", tracked_patch.value != 0) && ok;
        ok = expect_true("sdk VFS tracked patch changes probe value", g_vfs_patch_probe == replacement) && ok;

        auto tracked_rollback = vfs::Rollback(tracked_patch.value);
        ok = expect_sdk_status("sdk VFS tracked rollback succeeds", tracked_rollback.status, Status::Ok) && ok;
        ok = expect_true("sdk VFS tracked rollback restores probe value", g_vfs_patch_probe == original) && ok;

        auto tracked_rollback_again = vfs::Rollback(tracked_patch.value);
        ok = expect_sdk_status(
            "sdk VFS tracked rollback rejects inactive id",
            tracked_rollback_again.status,
            Status::InvalidArgument
        ) && ok;

        auto missing_rollback = vfs::Rollback(nullptr);
        ok = expect_sdk_status("sdk VFS patch rollback validates record", missing_rollback.status, Status::InvalidArgument) && ok;
        if (g_vfs_patch_probe != original) {
            g_vfs_patch_probe = original;
        }
    }

    return ok;
}

bool check_vfs_hook() {
    bool ok = true;

    runtime::hook::HookRegistry unavailable_registry;
    runtime::hook::HookRecord unavailable_hook;
    unavailable_hook.owner = "runtime_doctor";
    unavailable_hook.target = "vfs_open_hook_unavailable";
    unavailable_hook.kind = runtime::hook::HookKind::Plt;
    unavailable_hook.caller_path = NYX_VFS_CALLER_LIBRARY;
    unavailable_hook.callee_path = "libc.so";
    unavailable_hook.symbol = "open";
    unavailable_hook.replacement = reinterpret_cast<void*>(vfs_open_replacement);

    if (!runtime::hook::plt_hook_available()) {
        ok = expect_true("VFS hook unavailable record add", unavailable_registry.add(unavailable_hook)) && ok;
        auto install = unavailable_registry.install(
            "runtime_doctor",
            "vfs_open_hook_unavailable",
            runtime::hook::plt_backend()
        );
        ok = expect_status("VFS hook ABI unavailable", install.status, runtime::RuntimeStatus::Unavailable) && ok;
        ok = expect_hook_state(
            "VFS hook unavailable keeps registered",
            unavailable_registry,
            "runtime_doctor",
            "vfs_open_hook_unavailable",
            runtime::hook::HookState::Registered
        ) && ok;
        return ok;
    }

#if !NYX_DEBUG_MODE
    NYX_LOGI("runtime doctor VFS hook probe skipped outside Debug build");
    return ok;
#else
    runtime::loader::NativeLibrary loader;
    runtime::loader::LoadHandle caller_handle;
    auto caller_load = loader.load(runtime::loader::LoadRequest{NYX_VFS_CALLER_LIBRARY, 0}, &caller_handle);
    ok = expect_status("VFS hook caller loads", caller_load.status, runtime::RuntimeStatus::Ok) && ok;

    runtime::loader::Symbol open_write_symbol;
    auto open_write_result = loader.find_symbol(
        runtime::loader::SymbolRequest{caller_handle.handle, NYX_VFS_OPEN_WRITE_SYMBOL},
        &open_write_symbol
    );
    ok = expect_status("VFS hook open probe resolves", open_write_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("VFS hook open probe is valid", open_write_symbol.found()) && ok;

    runtime::loader::Symbol openat_write_symbol;
    auto openat_write_result = loader.find_symbol(
        runtime::loader::SymbolRequest{caller_handle.handle, NYX_VFS_OPENAT_WRITE_SYMBOL},
        &openat_write_symbol
    );
    ok = expect_status("VFS hook openat probe resolves", openat_write_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("VFS hook openat probe is valid", openat_write_symbol.found()) && ok;

    runtime::loader::Symbol stat_symbol;
    auto stat_result = loader.find_symbol(
        runtime::loader::SymbolRequest{caller_handle.handle, NYX_VFS_STAT_SYMBOL},
        &stat_symbol
    );
    ok = expect_status("VFS hook stat probe resolves", stat_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("VFS hook stat probe is valid", stat_symbol.found()) && ok;

    runtime::loader::Symbol lstat_symbol;
    auto lstat_result = loader.find_symbol(
        runtime::loader::SymbolRequest{caller_handle.handle, NYX_VFS_LSTAT_SYMBOL},
        &lstat_symbol
    );
    ok = expect_status("VFS hook lstat probe resolves", lstat_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("VFS hook lstat probe is valid", lstat_symbol.found()) && ok;

    runtime::loader::Symbol fstatat_symbol;
    auto fstatat_result = loader.find_symbol(
        runtime::loader::SymbolRequest{caller_handle.handle, NYX_VFS_FSTATAT_SYMBOL},
        &fstatat_symbol
    );
    ok = expect_status("VFS hook fstatat probe resolves", fstatat_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("VFS hook fstatat probe is valid", fstatat_symbol.found()) && ok;

    if (!open_write_symbol.found() ||
        !openat_write_symbol.found() ||
        !stat_symbol.found() ||
        !lstat_symbol.found() ||
        !fstatat_symbol.found()) {
        if (caller_handle.valid()) {
            auto close = loader.close(&caller_handle);
            ok = expect_status("VFS hook caller closes after resolve failure", close.status, runtime::RuntimeStatus::Ok) && ok;
        }
        return ok;
    }

    const auto open_write = reinterpret_cast<VfsOpenWriteProbe>(open_write_symbol.address);
    const auto openat_write = reinterpret_cast<VfsOpenAtWriteProbe>(openat_write_symbol.address);
    const auto stat_probe = reinterpret_cast<VfsStatProbe>(stat_symbol.address);
    const auto lstat_probe = reinterpret_cast<VfsLstatProbe>(lstat_symbol.address);
    const auto fstatat_probe = reinterpret_cast<VfsFstatAtProbe>(fstatat_symbol.address);

    runtime::vfs::PathPolicy policy;
    policy.private_root = "/data/data/dev.nyxcore.manager/files";
    runtime::vfs::PathMapper mapper(policy);
    const std::string virtual_path = "/nyx/virtual/vfs_hook.txt";
    const std::string target_path = policy.private_root + "/vfs_hook.txt";
    const char payload[] = "nyx-vfs-hook";
    const char openat_payload[] = "nyx-vfs-openat";
    constexpr int payload_size = static_cast<int>(sizeof(payload) - 1);
    constexpr int openat_payload_size = static_cast<int>(sizeof(openat_payload) - 1);

    ::unlink(target_path.c_str());

    ok = expect_int(
        "VFS hook baseline open uses original path",
        open_write(virtual_path.c_str(), payload),
        -ENOENT
    ) && ok;

    ok = expect_int(
        "VFS hook baseline openat uses original path",
        openat_write(virtual_path.c_str(), openat_payload),
        -ENOENT
    ) && ok;

    ok = expect_int(
        "VFS hook baseline stat uses original path",
        stat_probe(virtual_path.c_str()),
        -ENOENT
    ) && ok;

    ok = expect_int(
        "VFS hook baseline lstat uses original path",
        lstat_probe(virtual_path.c_str()),
        -ENOENT
    ) && ok;

    ok = expect_int(
        "VFS hook baseline fstatat uses original path",
        fstatat_probe(virtual_path.c_str()),
        -ENOENT
    ) && ok;

    auto add = mapper.add(runtime::vfs::PathRule{virtual_path, target_path});
    ok = expect_status("VFS hook path rule adds", add.status, runtime::RuntimeStatus::Ok) && ok;

    runtime::vfs::IoRedirector redirector(mapper);
    g_vfs_hook_redirector = &redirector;

    runtime::hook::HookRegistry registry;
    runtime::hook::HookRecord open_hook;
    open_hook.owner = "runtime_doctor";
    open_hook.target = "vfs_open_hook";
    open_hook.kind = runtime::hook::HookKind::Plt;
    open_hook.caller_path = NYX_VFS_CALLER_LIBRARY;
    open_hook.callee_path = "libc.so";
    open_hook.symbol = "open";
    open_hook.replacement = reinterpret_cast<void*>(vfs_open_replacement);
    ok = expect_true("VFS open hook record add", registry.add(open_hook)) && ok;

    runtime::hook::HookRecord openat_hook;
    openat_hook.owner = "runtime_doctor";
    openat_hook.target = "vfs_openat_hook";
    openat_hook.kind = runtime::hook::HookKind::Plt;
    openat_hook.caller_path = NYX_VFS_CALLER_LIBRARY;
    openat_hook.callee_path = "libc.so";
    openat_hook.symbol = "openat";
    openat_hook.replacement = reinterpret_cast<void*>(vfs_openat_replacement);
    ok = expect_true("VFS openat hook record add", registry.add(openat_hook)) && ok;

    runtime::hook::HookRecord stat_hook;
    stat_hook.owner = "runtime_doctor";
    stat_hook.target = "vfs_stat_hook";
    stat_hook.kind = runtime::hook::HookKind::Plt;
    stat_hook.caller_path = NYX_VFS_CALLER_LIBRARY;
    stat_hook.callee_path = "libc.so";
    stat_hook.symbol = "stat";
    stat_hook.replacement = reinterpret_cast<void*>(vfs_stat_replacement);
    ok = expect_true("VFS stat hook record add", registry.add(stat_hook)) && ok;

    runtime::hook::HookRecord lstat_hook;
    lstat_hook.owner = "runtime_doctor";
    lstat_hook.target = "vfs_lstat_hook";
    lstat_hook.kind = runtime::hook::HookKind::Plt;
    lstat_hook.caller_path = NYX_VFS_CALLER_LIBRARY;
    lstat_hook.callee_path = "libc.so";
    lstat_hook.symbol = "lstat";
    lstat_hook.replacement = reinterpret_cast<void*>(vfs_lstat_replacement);
    ok = expect_true("VFS lstat hook record add", registry.add(lstat_hook)) && ok;

    runtime::hook::HookRecord fstatat_hook;
    fstatat_hook.owner = "runtime_doctor";
    fstatat_hook.target = "vfs_fstatat_hook";
    fstatat_hook.kind = runtime::hook::HookKind::Plt;
    fstatat_hook.caller_path = NYX_VFS_CALLER_LIBRARY;
    fstatat_hook.callee_path = "libc.so";
    fstatat_hook.symbol = "fstatat";
    fstatat_hook.replacement = reinterpret_cast<void*>(vfs_fstatat_replacement);
    ok = expect_true("VFS fstatat hook record add", registry.add(fstatat_hook)) && ok;

    auto open_install = registry.install("runtime_doctor", "vfs_open_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS open hook install succeeds", open_install.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS open hook marks installed",
        registry,
        "runtime_doctor",
        "vfs_open_hook",
        runtime::hook::HookState::Installed
    ) && ok;
    ok = expect_int(
        "VFS open hook redirects configured path",
        open_write(virtual_path.c_str(), payload),
        payload_size
    ) && ok;

    auto open_install_again = registry.install("runtime_doctor", "vfs_open_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS open hook duplicate install is stable", open_install_again.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_int(
        "VFS open hook duplicate keeps redirect",
        open_write(virtual_path.c_str(), payload),
        payload_size
    ) && ok;

    auto openat_install = registry.install("runtime_doctor", "vfs_openat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS openat hook install succeeds", openat_install.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS openat hook marks installed",
        registry,
        "runtime_doctor",
        "vfs_openat_hook",
        runtime::hook::HookState::Installed
    ) && ok;

    ok = expect_int(
        "VFS openat hook redirects configured path",
        openat_write(virtual_path.c_str(), openat_payload),
        openat_payload_size
    ) && ok;

    auto stat_install = registry.install("runtime_doctor", "vfs_stat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS stat hook install succeeds", stat_install.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS stat hook marks installed",
        registry,
        "runtime_doctor",
        "vfs_stat_hook",
        runtime::hook::HookState::Installed
    ) && ok;
    ok = expect_int(
        "VFS stat hook redirects configured path",
        stat_probe(virtual_path.c_str()),
        openat_payload_size
    ) && ok;

    auto lstat_install = registry.install("runtime_doctor", "vfs_lstat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS lstat hook install succeeds", lstat_install.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS lstat hook marks installed",
        registry,
        "runtime_doctor",
        "vfs_lstat_hook",
        runtime::hook::HookState::Installed
    ) && ok;
    ok = expect_int(
        "VFS lstat hook redirects configured path",
        lstat_probe(virtual_path.c_str()),
        openat_payload_size
    ) && ok;

    auto fstatat_install = registry.install("runtime_doctor", "vfs_fstatat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS fstatat hook install succeeds", fstatat_install.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS fstatat hook marks installed",
        registry,
        "runtime_doctor",
        "vfs_fstatat_hook",
        runtime::hook::HookState::Installed
    ) && ok;
    ok = expect_int(
        "VFS fstatat hook redirects configured path",
        fstatat_probe(virtual_path.c_str()),
        openat_payload_size
    ) && ok;

    runtime::vfs::OpenHandle read_handle;
    auto read_open = redirector.open(runtime::vfs::OpenRequest{virtual_path, O_RDONLY, 0600}, &read_handle);
    ok = expect_status("VFS openat hook target opens for readback", read_open.status, runtime::RuntimeStatus::Ok) && ok;
    if (read_handle.valid()) {
        char read_buffer[openat_payload_size] = {};
        const auto read_count = ::read(read_handle.fd, read_buffer, openat_payload_size);
        ok = expect_int("VFS openat hook readback size", static_cast<int>(read_count), openat_payload_size) && ok;
        ok = expect_true(
            "VFS openat hook payload matches",
            std::memcmp(read_buffer, openat_payload, openat_payload_size) == 0
        ) && ok;
        auto close = redirector.close(&read_handle);
        ok = expect_status("VFS openat hook readback closes", close.status, runtime::RuntimeStatus::Ok) && ok;
    }

    auto open_remove = registry.remove("runtime_doctor", "vfs_open_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS open hook remove succeeds", open_remove.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS open hook marks removed",
        registry,
        "runtime_doctor",
        "vfs_open_hook",
        runtime::hook::HookState::Removed
    ) && ok;
    ok = expect_int(
        "VFS open hook remove restores original path",
        open_write(virtual_path.c_str(), payload),
        -ENOENT
    ) && ok;

    auto openat_remove = registry.remove("runtime_doctor", "vfs_openat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS openat hook remove succeeds", openat_remove.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS openat hook marks removed",
        registry,
        "runtime_doctor",
        "vfs_openat_hook",
        runtime::hook::HookState::Removed
    ) && ok;

    ok = expect_int(
        "VFS openat hook remove restores original path",
        openat_write(virtual_path.c_str(), openat_payload),
        -ENOENT
    ) && ok;

    auto stat_remove = registry.remove("runtime_doctor", "vfs_stat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS stat hook remove succeeds", stat_remove.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS stat hook marks removed",
        registry,
        "runtime_doctor",
        "vfs_stat_hook",
        runtime::hook::HookState::Removed
    ) && ok;
    ok = expect_int(
        "VFS stat hook remove restores original path",
        stat_probe(virtual_path.c_str()),
        -ENOENT
    ) && ok;

    auto lstat_remove = registry.remove("runtime_doctor", "vfs_lstat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS lstat hook remove succeeds", lstat_remove.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS lstat hook marks removed",
        registry,
        "runtime_doctor",
        "vfs_lstat_hook",
        runtime::hook::HookState::Removed
    ) && ok;
    ok = expect_int(
        "VFS lstat hook remove restores original path",
        lstat_probe(virtual_path.c_str()),
        -ENOENT
    ) && ok;

    auto fstatat_remove = registry.remove("runtime_doctor", "vfs_fstatat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS fstatat hook remove succeeds", fstatat_remove.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_hook_state(
        "VFS fstatat hook marks removed",
        registry,
        "runtime_doctor",
        "vfs_fstatat_hook",
        runtime::hook::HookState::Removed
    ) && ok;
    ok = expect_int(
        "VFS fstatat hook remove restores original path",
        fstatat_probe(virtual_path.c_str()),
        -ENOENT
    ) && ok;

    auto open_remove_again = registry.remove("runtime_doctor", "vfs_open_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS open hook duplicate remove is stable", open_remove_again.status, runtime::RuntimeStatus::Ok) && ok;

    auto openat_remove_again = registry.remove("runtime_doctor", "vfs_openat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS openat hook duplicate remove is stable", openat_remove_again.status, runtime::RuntimeStatus::Ok) && ok;

    auto stat_remove_again = registry.remove("runtime_doctor", "vfs_stat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS stat hook duplicate remove is stable", stat_remove_again.status, runtime::RuntimeStatus::Ok) && ok;

    auto lstat_remove_again = registry.remove("runtime_doctor", "vfs_lstat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS lstat hook duplicate remove is stable", lstat_remove_again.status, runtime::RuntimeStatus::Ok) && ok;

    auto fstatat_remove_again = registry.remove("runtime_doctor", "vfs_fstatat_hook", runtime::hook::plt_backend());
    ok = expect_status("VFS fstatat hook duplicate remove is stable", fstatat_remove_again.status, runtime::RuntimeStatus::Ok) && ok;

    g_vfs_hook_redirector = nullptr;
    ::unlink(target_path.c_str());

    if (caller_handle.valid()) {
        auto close = loader.close(&caller_handle);
        ok = expect_status("VFS hook caller closes", close.status, runtime::RuntimeStatus::Ok) && ok;
    }

    return ok;
#endif
}

std::size_t runtime_doctor_page_size() {
    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return 4096;
    }
    return static_cast<std::size_t>(page_size);
}

bool snapshot_is_sorted(const runtime::memory::VmaSnapshot& snapshot) {
    return std::is_sorted(snapshot.entries.begin(), snapshot.entries.end(), [](const auto& left, const auto& right) {
        return left.start < right.start || (left.start == right.start && left.end <= right.end);
    });
}

bool records_have_identity(const std::vector<runtime::memory::VmaOperationRecord>& records) {
    for (const auto& record : records) {
        if (record.id == 0 ||
            record.transaction_id == 0 ||
            !record.result.ok() ||
            record.status != runtime::memory::VmaOperationStatus::Applied ||
            record.before.start == 0 ||
            record.after.start == 0) {
            return false;
        }
    }
    return true;
}

bool rollback_records_ok(const std::vector<runtime::memory::VmaOperationRecord>& records) {
    for (const auto& record : records) {
        if (!record.result.ok() || record.status != runtime::memory::VmaOperationStatus::RolledBack) {
            return false;
        }
    }
    return true;
}

bool entry_permission_is(std::uintptr_t address, bool read, bool write, bool execute) {
    runtime::memory::MemoryMap memory_map;
    runtime::memory::MemoryMapEntry entry;
    if (!memory_map.find_address(address, &entry).ok()) {
        return false;
    }
    return entry.readable() == read && entry.writable() == write && entry.executable() == execute;
}

bool check_memory_normalization() {
    bool ok = true;

    runtime::memory::MemoryNormalizer normalizer;
    runtime::memory::VmaSnapshot snapshot;
    auto snapshot_result = normalizer.snapshot(&snapshot);
    ok = expect_status("memory normalization captures snapshot", snapshot_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("memory normalization snapshot has entries", !snapshot.entries.empty()) && ok;
    ok = expect_true("memory normalization snapshot is sorted", snapshot_is_sorted(snapshot)) && ok;

    runtime::memory::VmaDiff self_diff;
    auto self_diff_result = normalizer.diff(snapshot, snapshot, &self_diff);
    ok = expect_status("memory normalization diffs snapshots", self_diff_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("memory normalization self diff is empty", self_diff.entries.empty()) && ok;

    auto missing_snapshot = normalizer.snapshot(nullptr);
    ok = expect_status(
        "memory normalization validates snapshot output",
        missing_snapshot.status,
        runtime::RuntimeStatus::InvalidArgument
    ) && ok;

    const std::size_t page_size = runtime_doctor_page_size();
    auto wx_mapping = OwnedMapping::create(page_size, runtime::memory::PagePermission{true, true, true});
    if (wx_mapping.valid()) {
        runtime::memory::MemoryMap memory_map;
        runtime::memory::MemoryMapEntry wx_entry;
        auto wx_found = memory_map.find_address(wx_mapping.start(), &wx_entry);
        ok = expect_status("memory normalization finds writable code page", wx_found.status, runtime::RuntimeStatus::Ok) && ok;

        std::vector<runtime::memory::PermissionPlan> plans;
        auto plan_result = normalizer.plan_permissions(
            std::vector<runtime::memory::MemoryMapEntry>{wx_entry},
            runtime::memory::PermissionTransitionKind::SealWritableCode,
            &plans
        );
        ok = expect_status("memory normalization plans permission transition", plan_result.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_true(
            "memory normalization plans RWX to RX",
            !plans.empty() && plans.front().allowed && !plans.front().after.write && plans.front().after.execute
        ) && ok;

        runtime::memory::VmaTransaction seal_tx;
        ok = expect_status(
            "memory normalization queues protect",
            seal_tx.add_protect(runtime::memory::PageProtectRequest{
                wx_mapping.start(),
                wx_mapping.end(),
                true,
                false,
                true
            }).status,
            runtime::RuntimeStatus::Ok
        ) && ok;

        std::vector<runtime::memory::VmaOperationRecord> seal_records;
        auto seal_result = seal_tx.commit(&seal_records);
        ok = expect_status("memory normalization applies protect", seal_result.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_true("memory normalization records protect", records_have_identity(seal_records)) && ok;
        ok = expect_true(
            "memory normalization protect changed permission",
            entry_permission_is(wx_mapping.start(), true, false, true)
        ) && ok;

        std::vector<runtime::memory::VmaOperationRecord> seal_rollback;
        auto rollback_result = seal_tx.rollback(&seal_rollback);
        ok = expect_status("memory normalization rolls back protect", rollback_result.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_true("memory normalization rollback records succeeded", rollback_records_ok(seal_rollback)) && ok;
    } else {
        NYX_LOGW("runtime doctor memory normalization RWX test skipped: executable anonymous mmap unavailable");
    }

    auto batch_mapping = OwnedMapping::create(page_size, runtime::memory::PagePermission{true, true, false});
    ok = expect_true("memory normalization batch test mapping allocated", batch_mapping.valid()) && ok;
    if (batch_mapping.valid()) {
        runtime::memory::VmaTransaction batch_tx;
        ok = expect_status(
            "memory normalization queues readonly protect",
            batch_tx.add_protect(runtime::memory::PageProtectRequest{
                batch_mapping.start(),
                batch_mapping.end(),
                true,
                false,
                false
            }).status,
            runtime::RuntimeStatus::Ok
        ) && ok;
        ok = expect_status(
            "memory normalization queues readwrite protect",
            batch_tx.add_protect(runtime::memory::PageProtectRequest{
                batch_mapping.start(),
                batch_mapping.end(),
                true,
                true,
                false
            }).status,
            runtime::RuntimeStatus::Ok
        ) && ok;

        std::vector<runtime::memory::VmaOperationRecord> batch_records;
        auto batch_result = batch_tx.commit(&batch_records);
        ok = expect_status("memory normalization commits batch protect", batch_result.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_true("memory normalization batch has two records", batch_records.size() == 2) && ok;

        std::vector<runtime::memory::VmaOperationRecord> batch_rollback;
        auto batch_rollback_result = batch_tx.rollback(&batch_rollback);
        ok = expect_status("memory normalization rolls back batch", batch_rollback_result.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_true(
            "memory normalization restores batch permission",
            entry_permission_is(batch_mapping.start(), true, true, false)
        ) && ok;
    }

    auto name_mapping = OwnedMapping::create(page_size, runtime::memory::PagePermission{true, true, false});
    ok = expect_true("memory normalization anon-name mapping allocated", name_mapping.valid()) && ok;
    if (name_mapping.valid()) {
        runtime::memory::VmaTransaction name_tx;
        ok = expect_status(
            "memory normalization queues anon name",
            name_tx.add_anon_name(name_mapping.start(), name_mapping.size(), "nyx-doctor-buffer").status,
            runtime::RuntimeStatus::Ok
        ) && ok;

        std::vector<runtime::memory::VmaOperationRecord> name_records;
        auto name_result = name_tx.commit(&name_records);
        ok = expect_true(
            "memory normalization anon name is diagnostic",
            name_result.status == runtime::RuntimeStatus::Ok ||
                name_result.status == runtime::RuntimeStatus::Unavailable
        ) && ok;
        if (name_result.ok()) {
            ok = expect_true(
                "memory normalization maps anon name",
                !name_records.empty() && name_records.front().after.path.find("nyx-doctor-buffer") != std::string::npos
            ) && ok;
            std::vector<runtime::memory::VmaOperationRecord> name_rollback;
            auto name_rollback_result = name_tx.rollback(&name_rollback);
            ok = expect_status("memory normalization rolls back anon name", name_rollback_result.status, runtime::RuntimeStatus::Ok) && ok;
        }
    }

    auto advice_mapping = OwnedMapping::create(page_size, runtime::memory::PagePermission{true, true, false});
    ok = expect_true("memory normalization madvise mapping allocated", advice_mapping.valid()) && ok;
    if (advice_mapping.valid()) {
        runtime::memory::VmaTransaction advice_tx;
        ok = expect_status(
            "memory normalization queues madvise",
            advice_tx.add_advise(advice_mapping.start(), advice_mapping.size(), MADV_RANDOM).status,
            runtime::RuntimeStatus::Ok
        ) && ok;

        std::vector<runtime::memory::VmaOperationRecord> advice_records;
        auto advice_result = advice_tx.commit(&advice_records);
        ok = expect_status("memory normalization applies madvise", advice_result.status, runtime::RuntimeStatus::Ok) && ok;

        runtime::memory::VmaAttributes attributes;
        auto invalid_advice = attributes.advise(advice_mapping.start(), advice_mapping.size(), -1);
        ok = expect_status(
            "memory normalization validates invalid madvise",
            invalid_advice.status,
            runtime::RuntimeStatus::InvalidArgument
        ) && ok;
    }

    auto resize_mapping = OwnedMapping::create(page_size, runtime::memory::PagePermission{true, true, false});
    ok = expect_true("memory normalization resize mapping allocated", resize_mapping.valid()) && ok;
    if (resize_mapping.valid()) {
        const std::uintptr_t original = resize_mapping.start();
        runtime::memory::VmaTransaction resize_tx;
        ok = expect_status(
            "memory normalization queues mremap",
            resize_tx.add_resize(resize_mapping.start(), resize_mapping.size(), page_size * 2, MREMAP_MAYMOVE).status,
            runtime::RuntimeStatus::Ok
        ) && ok;

        std::vector<runtime::memory::VmaOperationRecord> resize_records;
        auto resize_result = resize_tx.commit(&resize_records);
        ok = expect_true(
            "memory normalization mremap is diagnostic",
            resize_result.status == runtime::RuntimeStatus::Ok ||
                resize_result.status == runtime::RuntimeStatus::Unavailable ||
                resize_result.status == runtime::RuntimeStatus::InvalidArgument
        ) && ok;
        if (resize_result.ok() && !resize_records.empty()) {
            resize_mapping.reset(resize_records.front().new_address, page_size * 2);
            ok = expect_true("memory normalization resize has new range", resize_records.front().new_address != 0) && ok;

            std::vector<runtime::memory::VmaOperationRecord> resize_rollback;
            auto resize_rollback_result = resize_tx.rollback(&resize_rollback);
            ok = expect_true(
                "memory normalization mremap rollback is diagnostic",
                resize_rollback_result.status == runtime::RuntimeStatus::Ok ||
                    resize_rollback_result.status == runtime::RuntimeStatus::Failed
            ) && ok;
            if (resize_rollback_result.ok()) {
                resize_mapping.reset(original, page_size);
            } else if (!resize_rollback.empty() && resize_rollback.front().new_address != 0) {
                resize_mapping.reset(resize_rollback.front().new_address, resize_rollback.front().new_size);
            } else {
                resize_mapping.release();
            }
        }
    }

    auto remap_mapping = OwnedMapping::create(page_size, runtime::memory::PagePermission{true, true, false});
    ok = expect_true("memory normalization remap mapping allocated", remap_mapping.valid()) && ok;
    if (remap_mapping.valid()) {
        std::memset(reinterpret_cast<void*>(remap_mapping.start()), 0x5a, remap_mapping.size());
        const std::uintptr_t original = remap_mapping.start();

        runtime::memory::VmaTransaction remap_tx;
        ok = expect_status(
            "memory normalization queues remap",
            remap_tx.add_remap(runtime::memory::RemapRequest{
                remap_mapping.start(),
                remap_mapping.size(),
                runtime::memory::PagePermission{true, true, false},
                true,
                "nyx-remap-buffer"
            }).status,
            runtime::RuntimeStatus::Ok
        ) && ok;

        std::vector<runtime::memory::VmaOperationRecord> remap_records;
        auto remap_result = remap_tx.commit(&remap_records);
        ok = expect_status("memory normalization applies remap", remap_result.status, runtime::RuntimeStatus::Ok) && ok;
        if (remap_result.ok() && !remap_records.empty()) {
            remap_mapping.reset(remap_records.front().new_address, page_size);
            const auto* bytes = reinterpret_cast<const unsigned char*>(remap_mapping.start());
            ok = expect_true("memory normalization remap preserves content", bytes[0] == 0x5a) && ok;

            std::vector<runtime::memory::VmaOperationRecord> remap_rollback;
            auto remap_rollback_result = remap_tx.rollback(&remap_rollback);
            ok = expect_true(
                "memory normalization remap rollback is diagnostic",
                remap_rollback_result.status == runtime::RuntimeStatus::Ok ||
                    remap_rollback_result.status == runtime::RuntimeStatus::Denied ||
                    remap_rollback_result.status == runtime::RuntimeStatus::Failed ||
                    remap_rollback_result.status == runtime::RuntimeStatus::Unavailable
            ) && ok;
            if (remap_rollback_result.ok()) {
                remap_mapping.reset(original, page_size);
            } else {
                remap_mapping.release();
            }
        }
    }

    return ok;
}

bool check_memory_diagnostics() {
    bool ok = true;

    runtime::memory::MemoryMap memory_map;
    std::vector<runtime::memory::MemoryMapEntry> maps;
    auto current = memory_map.current(&maps);
    ok = expect_status("memory maps parse", current.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("memory maps are non-empty", !maps.empty()) && ok;

    if (!maps.empty()) {
        ok = expect_true("memory map entry has range", maps.front().start < maps.front().end) && ok;
        ok = expect_true("memory map entry has permissions", !maps.front().permissions.empty()) && ok;
    }

    const auto own_address = reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_value));
    runtime::memory::MemoryMapEntry own_entry;
    auto own_result = memory_map.find_address(own_address, &own_entry);
    ok = expect_status("memory finds own code address", own_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("memory own code range contains address", own_entry.contains(own_address)) && ok;
    ok = expect_true("memory own code range is executable", own_entry.executable()) && ok;

    std::vector<runtime::memory::MemoryMapEntry> target_entries;
    auto target_result = memory_map.find_library(self_library_name(), &target_entries);
    ok = expect_status("memory finds active library", target_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("memory active library has segments", !target_entries.empty()) && ok;

    bool target_has_exec = false;
    for (const auto& entry : target_entries) {
        target_has_exec = target_has_exec || entry.executable();
    }
    ok = expect_true("memory active library has executable segment", target_has_exec) && ok;

    std::vector<runtime::memory::MemoryLibrary> libraries;
    auto libraries_result = memory_map.libraries(&libraries);
    ok = expect_status("memory lists libraries", libraries_result.status, runtime::RuntimeStatus::Ok) && ok;

    bool listed_target = false;
    for (const auto& library : libraries) {
        listed_target = listed_target ||
            library.name == self_library_name() ||
            library.path.find(self_library_name()) != std::string::npos;
    }
    ok = expect_true("memory library list includes active library", listed_target) && ok;

    runtime::memory::MemoryNormalizer normalizer;
    std::vector<runtime::memory::VmaOperationRecord> protect_records;
    auto protect_result = normalizer.protect_library_code(self_library_name(), &protect_records);
    ok = expect_status("memory protects writable code pages", protect_result.status, runtime::RuntimeStatus::Ok) && ok;

    ok = expect_true("memory page protection records succeeded", records_have_identity(protect_records)) && ok;

    return ok;
}

bool check_sdk_memory_contract() {
    bool ok = true;

    std::vector<memory::MapEntry> maps;
    auto maps_result = memory::GetMaps(&maps);
    ok = expect_sdk_status("sdk memory maps parse", maps_result.status, Status::Ok) && ok;
    ok = expect_true("sdk memory maps are non-empty", !maps.empty()) && ok;

    auto maps_value = memory::TryGetMaps();
    ok = expect_sdk_status("sdk memory value maps parse", maps_value.result.status, Status::Ok) && ok;
    ok = expect_true("sdk memory value maps are non-empty", !maps_value.value.empty()) && ok;
    ok = expect_true("sdk memory direct maps are non-empty", !memory::GetMaps().empty()) && ok;

    const auto own_address = reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_value));
    memory::MapEntry own_entry;
    auto own_result = memory::FindAddr(own_address, &own_entry);
    ok = expect_sdk_status("sdk memory finds own code address", own_result.status, Status::Ok) && ok;
    ok = expect_true("sdk memory own code range contains address",
        own_entry.start <= own_address && own_address < own_entry.end) && ok;

    std::vector<memory::MapEntry> target_entries;
    auto target_result = memory::FindLib(self_library_name(), &target_entries);
    ok = expect_sdk_status("sdk memory finds active library", target_result.status, Status::Ok) && ok;
    ok = expect_true("sdk memory active library has segments", !target_entries.empty()) && ok;

    std::vector<memory::Library> libraries;
    auto libraries_result = memory::GetLibs(&libraries);
    ok = expect_sdk_status("sdk memory lists libraries", libraries_result.status, Status::Ok) && ok;
    ok = expect_true("sdk memory library list is non-empty", !libraries.empty()) && ok;

    auto libraries_value = memory::TryGetLibs();
    ok = expect_sdk_status("sdk memory value lists libraries", libraries_value.result.status, Status::Ok) && ok;
    ok = expect_true("sdk memory value library list is non-empty", !libraries_value.value.empty()) && ok;

    memory::VmaSnapshot snapshot;
    auto snapshot_result = memory::Snapshot(&snapshot);
    ok = expect_sdk_status("sdk memory captures vma snapshot", snapshot_result.status, Status::Ok) && ok;
    ok = expect_true("sdk memory snapshot has entries", !snapshot.entries.empty()) && ok;

    memory::VmaDiff diff;
    auto diff_result = memory::Diff(snapshot, snapshot, &diff);
    ok = expect_sdk_status("sdk memory diffs vma snapshot", diff_result.status, Status::Ok) && ok;
    ok = expect_true("sdk memory self diff is empty", diff.entries.empty()) && ok;

    auto invalid_protect = memory::Protect(memory::ProtectOptions{});
    ok = expect_sdk_status(
        "sdk memory protect validates range",
        invalid_protect.status,
        Status::InvalidArgument
    ) && ok;

    auto sdk_mapping = OwnedMapping::create(runtime_doctor_page_size(), runtime::memory::PagePermission{true, true, false});
    ok = expect_true("sdk memory protect test mapping allocated", sdk_mapping.valid()) && ok;
    if (sdk_mapping.valid()) {
        const std::uint32_t write_value = 0x07142026U;
        auto write_result = memory::Write(reinterpret_cast<void*>(sdk_mapping.start()), write_value);
        ok = expect_sdk_status("sdk memory writes current writable page", write_result.status, Status::Ok) && ok;
        ok = expect_true(
            "sdk memory write changes value",
            *reinterpret_cast<std::uint32_t*>(sdk_mapping.start()) == write_value
        ) && ok;

        memory::OperationRecord protect_record;
        auto protect_result = memory::Protect(
            reinterpret_cast<void*>(sdk_mapping.start()),
            sdk_mapping.size(),
            memory::PagePermission{true, false, false},
            &protect_record
        );
        ok = expect_sdk_status("sdk memory protect records transaction", protect_result.status, Status::Ok) && ok;
        ok = expect_true(
            "sdk memory protect record has identity",
            protect_record.id != 0 &&
                protect_record.transaction_id != 0 &&
                protect_record.status == memory::OperationStatus::Applied
        ) && ok;

        const std::uint32_t safe_value = 0x20260715U;
        auto safe_write = memory::SafeWrite(reinterpret_cast<void*>(sdk_mapping.start()), safe_value);
        ok = expect_sdk_status("sdk memory auto-protect writes read-only page", safe_write.status, Status::Ok) && ok;
        ok = expect_true(
            "sdk memory safe write changes value",
            *reinterpret_cast<std::uint32_t*>(sdk_mapping.start()) == safe_value
        ) && ok;

        const std::uint32_t secure_value = 0x20260716U;
        auto secure_write = memory::Write(
            reinterpret_cast<void*>(sdk_mapping.start()),
            secure_value,
            memory::WritePolicy::SecureWrite
        );
        ok = expect_sdk_status("sdk memory secure-write writes read-only page", secure_write.status, Status::Ok) && ok;
        ok = expect_true(
            "sdk memory secure write changes value",
            *reinterpret_cast<std::uint32_t*>(sdk_mapping.start()) == secure_value
        ) && ok;

        memory::MapEntry secure_entry;
        auto secure_entry_result = memory::FindAddr(sdk_mapping.start(), &secure_entry);
        ok = expect_sdk_status("sdk memory secure write refinds page", secure_entry_result.status, Status::Ok) && ok;
        ok = expect_true(
            "sdk memory secure write restores read-only page",
            secure_entry.permissions.find('w') == std::string::npos
        ) && ok;

        std::vector<memory::OperationRecord> rollback_records;
        auto rollback_result = memory::Rollback(protect_record.transaction_id, &rollback_records);
        ok = expect_sdk_status("sdk memory rollback restores protection", rollback_result.status, Status::Ok) && ok;
        ok = expect_true("sdk memory rollback produced records", !rollback_records.empty()) && ok;
    }

    auto scan_mapping = OwnedMapping::create(runtime_doctor_page_size(), runtime::memory::PagePermission{true, true, false});
    ok = expect_true("sdk memory scan test mapping allocated", scan_mapping.valid()) && ok;
    if (scan_mapping.valid()) {
        auto* values = reinterpret_cast<std::int32_t*>(scan_mapping.start());
        values[0] = 324478056;
        values[1] = 4321;
        values[8] = 77;
        values[16] = 75;
        values[20] = 11;
        values[21] = 22;

        memory::MemTool mem;
        mem.maxResults = 8;
        const auto named = memory::SetName(scan_mapping.start(), scan_mapping.size(), "nyx-memtool-scan");
        if (named.ok()) {
            ok = expect_sdk_status("sdk memory names scan mapping", named.status, Status::Ok) && ok;
        }

        memory::MapEntry scan_entry;
        const auto scan_entry_result = memory::FindAddr(scan_mapping.start(), &scan_entry);
        ok = expect_sdk_status("sdk memory locates scan mapping", scan_entry_result.status, Status::Ok) && ok;

        if (scan_entry_result.ok() && !scan_entry.path.empty()) {
            auto custom_area = memory::setArea(&mem, scan_entry.path.c_str());
            ok = expect_sdk_status("sdk memory sets custom scan area", custom_area.status, Status::Ok) && ok;
        } else {
            auto anon_area = memory::setArea(&mem, memory::RANGE_ANONYMOUS);
            ok = expect_sdk_status("sdk memory sets anonymous scan area", anon_area.status, Status::Ok) && ok;
        }

        auto found = memory::Search(&mem, "324478056", memory::TYPE_DWORD);
        ok = expect_sdk_status("sdk memory searches dword", found.result.status, Status::Ok) && ok;
        ok = expect_true(
            "sdk memory search finds probe address",
            std::find(found.value.begin(), found.value.end(), scan_mapping.start()) != found.value.end()
        ) && ok;

        mem.results = {scan_mapping.start()};
        auto improved = memory::ImproveOffset(&mem, "4321", memory::TYPE_DWORD, sizeof(std::int32_t));
        ok = expect_sdk_status("sdk memory improves by offset", improved.result.status, Status::Ok) && ok;
        ok = expect_true("sdk memory improve keeps one result", memory::getResultCount(mem) == 1) && ok;

        auto offset_write = memory::OffsetWrite(&mem, "8765", memory::TYPE_DWORD, sizeof(std::int32_t));
        ok = expect_sdk_status("sdk memory writes result offset", offset_write.status, Status::Ok) && ok;
        ok = expect_true("sdk memory offset write changes value", values[1] == 8765) && ok;

        auto read_back = memory::getAddrData(mem, scan_mapping.start() + sizeof(std::int32_t), memory::TYPE_DWORD);
        ok = expect_sdk_status("sdk memory reads address data", read_back.result.status, Status::Ok) && ok;
        ok = expect_string("sdk memory address data formats dword", read_back.value, "8765") && ok;

        auto direct_write = memory::setAddrValue(
            &mem,
            "88",
            scan_mapping.start() + 8 * sizeof(std::int32_t),
            memory::TYPE_DWORD
        );
        ok = expect_sdk_status("sdk memory writes direct address", direct_write.status, Status::Ok) && ok;
        ok = expect_true("sdk memory direct write changes value", values[8] == 88) && ok;

        auto ranged = memory::SearchRange(&mem, "70~90", memory::TYPE_DWORD);
        ok = expect_sdk_status("sdk memory searches dword range", ranged.result.status, Status::Ok) && ok;
        ok = expect_true(
            "sdk memory range search finds direct write",
            std::find(
                ranged.value.begin(),
                ranged.value.end(),
                scan_mapping.start() + 8 * sizeof(std::int32_t)
            ) != ranged.value.end()
        ) && ok;

        auto united = memory::SearchUnited(&mem, "88D;70~80D:128", memory::TYPE_DWORD);
        ok = expect_sdk_status("sdk memory searches united values", united.result.status, Status::Ok) && ok;
        ok = expect_true(
            "sdk memory united search finds grouped address",
            std::find(
                united.value.begin(),
                united.value.end(),
                scan_mapping.start() + 8 * sizeof(std::int32_t)
            ) != united.value.end()
        ) && ok;

        mem.results = {scan_mapping.start()};
        auto united_improved = memory::ImproveOffsetUnited(
            &mem,
            "11D;22D:32",
            memory::TYPE_DWORD,
            20 * static_cast<std::intptr_t>(sizeof(std::int32_t))
        );
        ok = expect_sdk_status("sdk memory improves united values", united_improved.result.status, Status::Ok) && ok;
        ok = expect_true("sdk memory united improve keeps base result", memory::getResultCount(mem) == 1) && ok;

        auto* bytes = reinterpret_cast<std::uint8_t*>(scan_mapping.start());
        auto* word_slot = reinterpret_cast<std::int16_t*>(bytes + 128);
        auto* byte_slot = reinterpret_cast<std::int8_t*>(bytes + 136);
        auto* qword_slot = reinterpret_cast<std::int64_t*>(bytes + 144);
        auto* float_slot = reinterpret_cast<float*>(bytes + 160);
        auto* double_slot = reinterpret_cast<double*>(bytes + 176);
        ok = expect_sdk_status(
            "sdk memory writes word",
            memory::setAddrValue(&mem, "123", reinterpret_cast<std::uintptr_t>(word_slot), memory::TYPE_WORD).status,
            Status::Ok
        ) && ok;
        ok = expect_true("sdk memory word write changes value", *word_slot == 123) && ok;
        ok = expect_sdk_status(
            "sdk memory writes byte",
            memory::setAddrValue(&mem, "12", reinterpret_cast<std::uintptr_t>(byte_slot), memory::TYPE_BYTE).status,
            Status::Ok
        ) && ok;
        ok = expect_true("sdk memory byte write changes value", *byte_slot == 12) && ok;
        ok = expect_sdk_status(
            "sdk memory writes qword",
            memory::setAddrValue(&mem, "123456789", reinterpret_cast<std::uintptr_t>(qword_slot), memory::TYPE_QWORD).status,
            Status::Ok
        ) && ok;
        ok = expect_true("sdk memory qword write changes value", *qword_slot == 123456789) && ok;
        ok = expect_sdk_status(
            "sdk memory writes float",
            memory::setAddrValue(&mem, "1.5", reinterpret_cast<std::uintptr_t>(float_slot), memory::TYPE_FLOAT).status,
            Status::Ok
        ) && ok;
        ok = expect_true("sdk memory float write changes value", *float_slot > 1.49f && *float_slot < 1.51f) && ok;
        ok = expect_sdk_status(
            "sdk memory writes double",
            memory::setAddrValue(&mem, "2.5", reinterpret_cast<std::uintptr_t>(double_slot), memory::TYPE_DOUBLE).status,
            Status::Ok
        ) && ok;
        ok = expect_true("sdk memory double write changes value", *double_slot > 2.49 && *double_slot < 2.51) && ok;

        auto freeze_delay = memory::setFreezeDelayMs(&mem, 10);
        ok = expect_sdk_status("sdk memory sets freeze delay", freeze_delay.status, Status::Ok) && ok;
        auto freeze_write = memory::setAddrValue(
            &mem,
            "1234",
            scan_mapping.start() + 8 * sizeof(std::int32_t),
            memory::TYPE_DWORD,
            true
        );
        ok = expect_sdk_status("sdk memory starts direct freeze", freeze_write.status, Status::Ok) && ok;
        ok = expect_true("sdk memory freeze writes immediately", values[8] == 1234) && ok;
        auto* freeze_slot = reinterpret_cast<volatile std::int32_t*>(
            scan_mapping.start() + 8 * sizeof(std::int32_t)
        );
        *freeze_slot = 1;
        for (int poll = 0; poll < 20 && *freeze_slot != 1234; ++poll) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ok = expect_true("sdk memory freeze restores value", *freeze_slot == 1234) && ok;
        ok = expect_true("sdk memory freeze count is exposed", memory::getFreezeNum(mem) == 1) && ok;
        auto freeze_list = memory::getFreezeList(mem);
        ok = expect_sdk_status("sdk memory freeze list returns value", freeze_list.result.status, Status::Ok) && ok;
        ok = expect_true("sdk memory freeze list has item", freeze_list.value.size() == 1) && ok;
        auto stopped_once = memory::stopAllFreeze(&mem);
        ok = expect_sdk_status("sdk memory stops freeze before restart", stopped_once.status, Status::Ok) && ok;
        *freeze_slot = 2;
        auto restarted_freeze = memory::startAllFreeze(&mem);
        ok = expect_sdk_status("sdk memory restarts freeze worker", restarted_freeze.status, Status::Ok) && ok;
        for (int poll = 0; poll < 20 && *freeze_slot != 1234; ++poll) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ok = expect_true("sdk memory freeze restart restores value", *freeze_slot == 1234) && ok;
        auto removed_freeze = memory::removeFreezeItem(&mem, scan_mapping.start() + 8 * sizeof(std::int32_t));
        ok = expect_sdk_status("sdk memory removes freeze item", removed_freeze.status, Status::Ok) && ok;
        auto stopped_freeze = memory::stopAllFreeze(&mem);
        ok = expect_sdk_status("sdk memory stops freeze worker", stopped_freeze.status, Status::Ok) && ok;

        ok = expect_true("sdk memory result list is exposed", !memory::getResultList(mem).empty()) && ok;
        auto cleared = memory::clearResultList(&mem);
        ok = expect_sdk_status("sdk memory clears result list", cleared.status, Status::Ok) && ok;
        ok = expect_true("sdk memory result count clears", memory::getResultCount(mem) == 0) && ok;
    }

    std::string exec_output;
    auto exec_result = runtime::memory::SystemControl::exec("echo nyx-memory-system", &exec_output);
    ok = expect_status("runtime memory system exec runs", exec_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true(
        "runtime memory system exec captures output",
        exec_output.find("nyx-memory-system") != std::string::npos
    ) && ok;

    memory::OperationRecord invalid_advice_record;
    auto invalid_advice = memory::Advise(own_entry.start, own_entry.end - own_entry.start, -1, &invalid_advice_record);
    ok = expect_sdk_status(
        "sdk memory validates invalid madvise",
        invalid_advice.status,
        Status::InvalidArgument
    ) && ok;

    std::vector<memory::OperationRecord> protect_records;
    auto protect_code = memory::ProtectLib(self_library_name(), &protect_records);
    ok = expect_sdk_status("sdk memory protects library code", protect_code.status, Status::Ok) && ok;

    return ok;
}

bool check_stack_diagnostics() {
    bool ok = true;

    runtime::stack::StackTrace stack_trace;
    std::vector<runtime::stack::StackFrame> frames;
    auto capture = stack_trace.capture(&frames);
    ok = expect_status("stack trace captures", capture.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("stack trace has frames", !frames.empty()) && ok;
    ok = expect_true("stack trace respects frame limit", frames.size() <= 32) && ok;

    bool has_pc = false;
    bool has_module = false;
    for (const auto& frame : frames) {
        has_pc = has_pc || frame.pc != 0;
        has_module = has_module || !frame.module_path.empty();
    }
    ok = expect_true("stack trace has nonzero pc", has_pc) && ok;
    ok = expect_true("stack trace has module path", has_module) && ok;

    if (!frames.empty() && frames.front().pc != 0) {
        runtime::memory::MemoryMap memory_map;
        runtime::memory::MemoryMapEntry frame_entry;
        auto frame_map = memory_map.find_address(frames.front().pc, &frame_entry);
        ok = expect_status("stack top frame maps to memory", frame_map.status, runtime::RuntimeStatus::Ok) && ok;
    }

    runtime::stack::StackNormalizer normalizer;
    runtime::stack::StackTraceSnapshot snapshot;
    auto normalized_capture = normalizer.capture(runtime::stack::StackNormalizeRequest{}, &snapshot);
    ok = expect_status("stack normalization captures", normalized_capture.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("stack normalization keeps raw frames", !snapshot.raw_frames.empty()) && ok;
    ok = expect_true("stack normalization emits frames", !snapshot.normalized_frames.empty()) && ok;
    ok = expect_true("stack normalization preserves raw evidence", raw_frames_preserved(snapshot)) && ok;

    runtime::stack::StackTraceSnapshot invalid_snapshot;
    auto invalid_capture = normalizer.capture(runtime::stack::StackNormalizeRequest{0}, &invalid_snapshot);
    ok = expect_status(
        "stack normalization validates frame limit",
        invalid_capture.status,
        runtime::RuntimeStatus::InvalidArgument
    ) && ok;

    std::vector<runtime::stack::NormalizedStackFrame> invalid_frames;
    auto empty_normalize = normalizer.normalize({}, {}, &invalid_frames);
    ok = expect_status(
        "stack normalization validates raw input",
        empty_normalize.status,
        runtime::RuntimeStatus::InvalidArgument
    ) && ok;

    runtime::stack::StackContextReader context_reader;
    runtime::stack::StackCpuContext context;
    auto context_result = context_reader.current(&context);
    ok = expect_status("stack context reads current thread", context_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("stack context has pc", context.pc != 0) && ok;

    runtime::stack::FramePointerWalker walker;
    std::vector<runtime::stack::FramePointerNode> fp_nodes;
    auto walk_result = walker.walk(context, 8, &fp_nodes);
    ok = expect_true(
        "stack frame pointer walk is diagnostic",
        walk_result.status == runtime::RuntimeStatus::Ok ||
            walk_result.status == runtime::RuntimeStatus::Unavailable
    ) && ok;

    runtime::stack::StackHookRegistry stack_registry;
    stack_registry.clear();
    runtime::stack::HookFrameRecord manual_hook;
    manual_hook.owner = "runtime_doctor";
    manual_hook.target = "stack_manual_hook";
    manual_hook.kind = runtime::stack::HookType::Inline;
    manual_hook.replacement_range = runtime::stack::AddressRange{
        reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_replacement)),
        reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_replacement)) + 256,
        true
    };
    manual_hook.target_range = runtime::stack::AddressRange{
        reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_value)),
        reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_value)) + 32,
        true
    };
    manual_hook.trampoline_range = runtime::stack::AddressRange{
        reinterpret_cast<std::uintptr_t>(hook_address(sdk_hook_probe_value)),
        reinterpret_cast<std::uintptr_t>(hook_address(sdk_hook_probe_value)) + 32,
        true
    };
    manual_hook.original_range = manual_hook.trampoline_range;
    manual_hook.original_entry = reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_value));
    manual_hook.replacement_entry = reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_replacement));
    manual_hook.installed = true;
    ok = expect_status(
        "stack hook registry registers manual range",
        stack_registry.add_or_update(manual_hook).status,
        runtime::RuntimeStatus::Ok
    ) && ok;
    const auto manual_records = stack_registry.records();
    ok = expect_true(
        "stack hook replacement unwind metadata complete",
        !manual_records.empty() && manual_records.front().replacement_unwind.complete
    ) && ok;
    ok = expect_true(
        "stack hook trampoline unwind metadata complete",
        !manual_records.empty() && manual_records.front().trampoline_unwind.complete
    ) && ok;

    runtime::stack::RawStackFrame replacement_raw;
    replacement_raw.pc = reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_replacement));
    replacement_raw.return_address = replacement_raw.pc;
    replacement_raw.from_unwind = true;
    runtime::stack::RawStackFrame caller_raw;
    caller_raw.pc = reinterpret_cast<std::uintptr_t>(hook_address(hook_probe_value));
    caller_raw.return_address = caller_raw.pc;
    caller_raw.from_unwind = true;
    alignas(std::uintptr_t) std::uintptr_t canonical_frame[] = {0, caller_raw.pc};
    caller_raw.frame_pointer = reinterpret_cast<std::uintptr_t>(&canonical_frame[0]);
    caller_raw.from_frame_chain = true;

    std::vector<runtime::stack::NormalizedStackFrame> manual_frames;
    auto manual_normalize = normalizer.normalize(
        std::vector<runtime::stack::RawStackFrame>{replacement_raw, caller_raw},
        stack_registry.records(),
        &manual_frames
    );
    ok = expect_status("stack normalization maps manual hook", manual_normalize.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true(
        "stack normalization classifies manual replacement",
        has_stack_kind(manual_frames, runtime::stack::StackType::HookReplacement)
    ) && ok;
    ok = expect_true(
        "stack normalization adjusts manual replacement",
        has_stack_status(manual_frames, runtime::stack::StackStatus::AdjustedReturnAddress)
    ) && ok;
    if (!manual_frames.empty()) {
        ok = expect_true(
            "stack normalization restores caller return address",
            manual_frames.front().normalized_return_address == caller_raw.pc
        ) && ok;
        ok = expect_true(
            "stack normalization recovers canonical frame pointer",
            manual_frames.front().normalized_frame_pointer == caller_raw.frame_pointer
        ) && ok;
    }

    alignas(std::uintptr_t) std::uintptr_t repair_chain[] = {
        0,
        replacement_raw.pc,
        0,
        caller_raw.pc
    };
    runtime::stack::RawStackFrame repair_replacement_raw = replacement_raw;
    repair_replacement_raw.frame_pointer = reinterpret_cast<std::uintptr_t>(&repair_chain[0]);
    repair_replacement_raw.stack_pointer = repair_replacement_raw.frame_pointer;
    runtime::stack::RawStackFrame repair_caller_raw = caller_raw;
    repair_caller_raw.frame_pointer = reinterpret_cast<std::uintptr_t>(&repair_chain[2]);
    repair_caller_raw.stack_pointer = repair_caller_raw.frame_pointer;

    std::vector<runtime::stack::NormalizedStackFrame> repair_frames;
    auto repair_normalize = normalizer.normalize(
        std::vector<runtime::stack::RawStackFrame>{repair_replacement_raw, repair_caller_raw},
        stack_registry.records(),
        &repair_frames
    );
    ok = expect_status("stack repair normalizes writable frame", repair_normalize.status, runtime::RuntimeStatus::Ok) && ok;

    std::vector<runtime::stack::StackRepairPatch> repair_plan;
    auto repair_plan_result = runtime::stack::plan_stack_repair(repair_frames, &repair_plan);
    const auto has_repair_slot = [](const std::vector<runtime::stack::StackRepairPatch>& patches,
                                    runtime::stack::StackRepairSlot slot) {
        return std::any_of(patches.begin(), patches.end(), [slot](const runtime::stack::StackRepairPatch& patch) {
            return patch.slot == slot;
        });
    };
    ok = expect_status("stack repair plans saved slots", repair_plan_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true(
        "stack repair plans frame pair",
        has_repair_slot(repair_plan, runtime::stack::StackRepairSlot::SavedFramePointer) &&
            has_repair_slot(repair_plan, runtime::stack::StackRepairSlot::SavedReturnAddress)
    ) && ok;

    {
        runtime::stack::StackRepairScope repair_scope;
        std::vector<runtime::stack::StackRepairPatch> applied;
        auto repair_apply = repair_scope.apply(repair_frames, &applied);
        ok = expect_status("stack repair applies saved slots", repair_apply.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_true(
            "stack repair writes saved frame pointer",
            repair_chain[0] == repair_caller_raw.frame_pointer
        ) && ok;
        ok = expect_true(
            "stack repair writes saved return address",
            repair_chain[1] == repair_caller_raw.pc
        ) && ok;
        ok = expect_true(
            "stack repair reports applied patches",
            has_repair_slot(applied, runtime::stack::StackRepairSlot::SavedFramePointer) &&
                has_repair_slot(applied, runtime::stack::StackRepairSlot::SavedReturnAddress)
        ) && ok;
        repair_scope.restore();
    }
    ok = expect_true("stack repair restores saved frame pointer", repair_chain[0] == 0) && ok;
    ok = expect_true("stack repair restores saved return address", repair_chain[1] == replacement_raw.pc) && ok;

    runtime::stack::RawStackFrame trampoline_raw;
    trampoline_raw.pc = reinterpret_cast<std::uintptr_t>(hook_address(sdk_hook_probe_value));
    trampoline_raw.return_address = trampoline_raw.pc;
    trampoline_raw.from_unwind = true;
    std::vector<runtime::stack::RawStackFrame> hook_support_raw{
        replacement_raw,
        trampoline_raw,
        caller_raw
    };
    runtime::stack::StackNormalizeRequest collapse_request;
    collapse_request.collapse_hook_frames = true;
    std::vector<runtime::stack::NormalizedStackFrame> collapsed_frames;
    auto collapsed_normalize = normalizer.normalize(
        hook_support_raw,
        stack_registry.records(),
        collapse_request,
        &collapsed_frames
    );
    ok = expect_status(
        "stack normalization collapses hook support frames",
        collapsed_normalize.status,
        runtime::RuntimeStatus::Ok
    ) && ok;
    ok = expect_true(
        "stack normalization emits collapsed view",
        collapsed_frames.size() < hook_support_raw.size()
    ) && ok;
    ok = expect_false(
        "stack normalization hides collapsed trampoline",
        has_stack_kind(collapsed_frames, runtime::stack::StackType::HookTrampoline)
    ) && ok;

    ok = expect_status(
        "stack hook registry marks manual range removed",
        stack_registry.mark_removed("runtime_doctor", "stack_manual_hook").status,
        runtime::RuntimeStatus::Ok
    ) && ok;
    std::vector<runtime::stack::NormalizedStackFrame> removed_frames;
    auto removed_normalize = normalizer.normalize(
        std::vector<runtime::stack::RawStackFrame>{replacement_raw, caller_raw},
        stack_registry.records(),
        &removed_frames
    );
    ok = expect_status(
        "stack normalization keeps removed hook raw",
        removed_normalize.status,
        runtime::RuntimeStatus::Ok
    ) && ok;
    ok = expect_false(
        "stack normalization ignores removed hook record",
        has_stack_kind(removed_frames, runtime::stack::StackType::HookReplacement)
    ) && ok;

    std::vector<stack::Frame> sdk_frames;
    auto sdk_capture = stack::Capture(&sdk_frames);
    ok = expect_sdk_status("sdk stack captures", sdk_capture.status, Status::Ok) && ok;
    ok = expect_true("sdk stack emits frames", !sdk_frames.empty()) && ok;

    auto sdk_invalid = stack::Capture(nullptr);
    ok = expect_sdk_status(
        "sdk stack validates output",
        sdk_invalid.status,
        Status::InvalidArgument
    ) && ok;

    return ok;
}

bool check_enabled_runtime() {
    bool ok = true;

    runtime::loader::NativeLibrary loader;
    runtime::loader::LoadHandle handle;
    auto load_result = loader.load(runtime::loader::LoadRequest{}, &handle);
    ok = expect_status("loader validates empty path", load_result.status, runtime::RuntimeStatus::InvalidArgument) && ok;

    runtime::loader::LoadHandle missing_handle;
    auto missing_result = loader.load(
        runtime::loader::LoadRequest{"libnyx_missing_runtime_doctor.so", 0},
        &missing_handle
    );
    ok = expect_status("loader reports missing library", missing_result.status, runtime::RuntimeStatus::NotFound) && ok;
    ok = expect_false("missing library produced no handle", missing_handle.valid()) && ok;

    runtime::loader::Symbol symbol;
    auto symbol_result = loader.find_symbol(runtime::loader::SymbolRequest{nullptr, ""}, &symbol);
    ok = expect_status("native library symbol validates args", symbol_result.status, runtime::RuntimeStatus::InvalidArgument) && ok;

#if NYX_DEBUG_MODE
    runtime::loader::LoadHandle probe_handle;
    auto probe_result = loader.load(runtime::loader::LoadRequest{NYX_RUNTIME_PROBE_LIBRARY, 0}, &probe_handle);
    ok = expect_status("loader opens runtime probe", probe_result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("runtime probe handle is valid", probe_handle.valid()) && ok;

    if (probe_handle.valid()) {
        runtime::loader::Symbol probe_symbol;
        auto find_result = loader.find_symbol(
            runtime::loader::SymbolRequest{probe_handle.handle, NYX_RUNTIME_PROBE_SYMBOL},
            &probe_symbol
        );
        ok = expect_status("native library symbol finds runtime probe", find_result.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_true("runtime probe symbol is valid", probe_symbol.found()) && ok;

        if (probe_symbol.found()) {
            using ProbeValue = int (*)();
            const auto probe_value = reinterpret_cast<ProbeValue>(probe_symbol.address);
            ok = expect_int("runtime probe value", probe_value(), NYX_RUNTIME_PROBE_EXPECTED) && ok;
        }

        runtime::loader::Symbol missing_symbol;
        auto missing_symbol_result = loader.find_symbol(
            runtime::loader::SymbolRequest{probe_handle.handle, "nyx_runtime_missing_symbol"},
            &missing_symbol
        );
        ok = expect_status(
            "native library symbol reports missing symbol",
            missing_symbol_result.status,
            runtime::RuntimeStatus::NotFound
        ) && ok;
        ok = expect_false("missing symbol produced no address", missing_symbol.found()) && ok;

        auto close_result = loader.close(&probe_handle);
        ok = expect_status("loader closes runtime probe", close_result.status, runtime::RuntimeStatus::Ok) && ok;
        ok = expect_false("runtime probe handle cleared", probe_handle.valid()) && ok;

        auto close_again = loader.close(&probe_handle);
        ok = expect_status("loader rejects repeated close", close_again.status, runtime::RuntimeStatus::InvalidArgument) && ok;
    }
#else
    NYX_LOGI("runtime doctor native loader probe skipped outside Debug build");
#endif

    ok = check_vfs_redirector() && ok;
    ok = check_sdk_vfs_contract() && ok;
    ok = check_vfs_hook() && ok;

    runtime::hook::HookRegistry registry;
    ok = expect_false("invalid hook rejected", registry.add(runtime::hook::HookRecord{})) && ok;
    ok = check_hook_registry_states() && ok;
    ok = check_sdk_hook() && ok;
    ok = check_sdk_hook_resolvers() && ok;
    ok = check_sdk_plt_hook() && ok;
    ok = check_inline_hook() && ok;
    ok = check_plt_backend() && ok;
    ok = check_unity_unavailable() && ok;
    ok = check_memory_diagnostics() && ok;
    ok = check_memory_normalization() && ok;
    ok = check_sdk_memory_contract() && ok;
    ok = check_stack_diagnostics() && ok;

    return ok;
}

} // namespace

bool CheckRuntime() {
    bool ok = check_enabled_runtime();

    NYX_LOGI("runtime doctor %s", ok ? "passed" : "failed");
    return ok;
}

} // namespace test
} // namespace sdk
} // namespace nyx
