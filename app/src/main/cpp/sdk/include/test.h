#pragma once

#include <cstdint>

#include "sdk/include/utils.h"

#ifndef NYX_DEBUG_MODE
#define NYX_DEBUG_MODE 0
#endif

#ifndef NYX_ENABLE_NATIVE_TESTS
#define NYX_ENABLE_NATIVE_TESTS 0
#endif

#ifndef NYX_ENABLE_INTEGRATION_GATES
#define NYX_ENABLE_INTEGRATION_GATES 0
#endif

#ifndef NYX_ENABLE_BENCHMARKS
#define NYX_ENABLE_BENCHMARKS 0
#endif

#ifndef NYX_BENCHMARK_LOG_MIN_US
#define NYX_BENCHMARK_LOG_MIN_US 3000
#endif

namespace nyx {
namespace sdk {
namespace test {

#if NYX_ENABLE_BENCHMARKS
// 作用域计时器：析构时按阈值输出耗时日志
class NYX_EXPORT Timer {
public:
    // 记录计时名称和开始时间
    explicit Timer(const char* name);
    // 结束计时并输出结果
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

private:
    // 计时名称
    const char* name_;
    // 开始时间（微秒）
    std::uint64_t start_micros_;
};

// 获取当前运行时间（微秒）
NYX_EXPORT std::uint64_t Now();
#else
// benchmark 关闭时的空计时器
class Timer {
public:
    explicit Timer(const char*) {}
    ~Timer() = default;

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
};

// benchmark 关闭时固定返回 0
inline std::uint64_t Now() {
    return 0;
}
#endif

#if NYX_ENABLE_NATIVE_TESTS
// 检查运行环境
NYX_EXPORT bool CheckEnv();
// 检查字符串工具
NYX_EXPORT bool CheckString();
// 检查加密工具
NYX_EXPORT bool CheckCrypto();
// 检查网络工具
NYX_EXPORT bool CheckNet();
// 检查授权流程
NYX_EXPORT bool CheckAuth();
// 检查模块管理
NYX_EXPORT bool CheckModule();
// 检查运行时能力
NYX_EXPORT bool CheckRuntime();
// 检查游戏引擎绑定
NYX_EXPORT bool CheckEngine();
#else
// native tests 关闭时不执行环境检查
inline bool CheckEnv() {
    return false;
}

// native tests 关闭时不执行字符串检查
inline bool CheckString() {
    return false;
}

// native tests 关闭时不执行加密检查
inline bool CheckCrypto() {
    return false;
}

// native tests 关闭时不执行网络检查
inline bool CheckNet() {
    return false;
}

// native tests 关闭时不执行授权检查
inline bool CheckAuth() {
    return false;
}

// native tests 关闭时不执行模块检查
inline bool CheckModule() {
    return false;
}

// native tests 关闭时不执行运行时检查
inline bool CheckRuntime() {
    return false;
}

// native tests 关闭时不执行引擎检查
inline bool CheckEngine() {
    return false;
}
#endif

#if NYX_ENABLE_INTEGRATION_GATES
// 检查发布前集成门禁
NYX_EXPORT bool CheckRelease();
#else
// 集成门禁关闭时固定返回 false
inline bool CheckRelease() {
    return false;
}
#endif

} // namespace test
} // namespace sdk
} // namespace nyx

#if NYX_ENABLE_BENCHMARKS
#define NYX_BENCHMARK_JOIN_INNER(a, b) a##b
#define NYX_BENCHMARK_JOIN(a, b) NYX_BENCHMARK_JOIN_INNER(a, b)
#define BENCHMARK(name) \
    ::nyx::sdk::test::Timer NYX_BENCHMARK_JOIN(nyx_scope_timer_, __LINE__)(name)
#else
#define BENCHMARK(name) static_cast<void>(0)
#endif
