#include "src/runtime/memory/memory_improve.h"

#include "src/runtime/memory/memory_reader.h"

#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 地址加偏移并检查溢出
bool add_offset(std::uintptr_t addr, std::intptr_t offset, std::uintptr_t* out) {
    if (out == nullptr) {
        return false;
    }

    if (offset >= 0) {
        const auto positive = static_cast<std::uintptr_t>(offset);
        if (positive > std::numeric_limits<std::uintptr_t>::max() - addr) {
            return false;
        }
        *out = addr + positive;
        return *out != 0;
    }

    const auto negative = static_cast<std::uintptr_t>(-(offset + 1)) + 1;
    if (negative > addr) {
        return false;
    }
    *out = addr - negative;
    return *out != 0;
}

// 计算联合搜索跨度结束位置
std::uintptr_t span_end(std::uintptr_t base, std::size_t span) {
    if (span > static_cast<std::size_t>(std::numeric_limits<std::uintptr_t>::max() - base)) {
        return std::numeric_limits<std::uintptr_t>::max();
    }
    return base + static_cast<std::uintptr_t>(span);
}

// 在指定范围内寻找后续搜索项
bool find_term_after(
    const MemoryReader& reader,
    const SearchTerm& term,
    std::uintptr_t start,
    std::uintptr_t end,
    std::uintptr_t* found
) {
    const std::size_t size = type_size(term.type);
    if (found == nullptr || size == 0 || start >= end) {
        return false;
    }

    // 先整段读取，避免逐字节 pread64
    const std::size_t span = static_cast<std::size_t>(end - start);
    std::vector<std::uint8_t> buffer(span);
    auto read = reader.read(start, buffer.data(), span);
    if (!read.ok()) {
        // 整段读取失败时退回逐字节读取
        std::array<std::uint8_t, 8> single{};
        for (std::uintptr_t cursor = start; cursor < end; ++cursor) {
            auto single_read = reader.read(cursor, single.data(), size);
            if (!single_read.ok()) {
                continue;
            }
            if (matches_value(single.data(), term)) {
                *found = cursor;
                return true;
            }
        }
        return false;
    }

    for (std::size_t i = 0; i + size <= span; ++i) {
        if (matches_value(buffer.data() + i, term)) {
            *found = start + i;
            return true;
        }
    }

    return false;
}

// 判断 base 位置是否满足完整联合搜索
bool matches_united_at(const MemoryReader& reader, const UnitedSearch& united, std::uintptr_t base) {
    if (united.terms.size() < 2 || united.span == 0) {
        return false;
    }

    std::array<std::uint8_t, 8> buffer{};
    const SearchTerm& first = united.terms.front();
    const std::size_t first_size = type_size(first.type);
    auto read = reader.read(base, buffer.data(), first_size);
    if (!read.ok() || !matches_value(buffer.data(), first)) {
        return false;
    }

    std::uintptr_t cursor = base + static_cast<std::uintptr_t>(first_size);
    const std::uintptr_t end = span_end(base, united.span);
    for (std::size_t i = 1; i < united.terms.size(); ++i) {
        std::uintptr_t found = 0;
        if (!find_term_after(reader, united.terms[i], cursor, end, &found)) {
            return false;
        }
        cursor = found + static_cast<std::uintptr_t>(type_size(united.terms[i].type));
    }

    return true;
}

} // namespace

// 创建指定进程的筛选器
MemoryImprover::MemoryImprover(MemProcess process)
    : process_(std::move(process)) {}

// 按 offset 后的单值或范围条件筛选结果
RuntimeResult MemoryImprover::filter(
    const std::vector<std::uintptr_t>& input,
    const SearchTerm& term,
    std::intptr_t offset,
    std::vector<std::uintptr_t>* out
) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing memory improve output"};
    }
    out->clear();
    if (input.empty()) {
        return RuntimeResult{RuntimeStatus::NotFound, "memory improve has no input results"};
    }

    const std::size_t size = type_size(term.type);
    if (size == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid memory improve type"};
    }

    MemoryReader reader(process_);
    std::array<std::uint8_t, 8> buffer{};
    for (std::uintptr_t addr : input) {
        std::uintptr_t target = 0;
        if (!add_offset(addr, offset, &target)) {
            continue;
        }

        auto read = reader.read(target, buffer.data(), size);
        if (!read.ok()) {
            continue;
        }
        if (matches_value(buffer.data(), term)) {
            out->push_back(addr);
        }
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::NotFound, "memory improve found no results"};
    }
    return RuntimeResult{};
}

// 按 offset 后的联合条件筛选结果
RuntimeResult MemoryImprover::filter_united(
    const std::vector<std::uintptr_t>& input,
    const UnitedSearch& united,
    std::intptr_t offset,
    std::vector<std::uintptr_t>* out
) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing united memory improve output"};
    }
    out->clear();
    if (input.empty()) {
        return RuntimeResult{RuntimeStatus::NotFound, "united memory improve has no input results"};
    }
    if (united.terms.size() < 2) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "united memory improve needs multiple terms"};
    }

    MemoryReader reader(process_);
    for (std::uintptr_t addr : input) {
        std::uintptr_t target = 0;
        if (!add_offset(addr, offset, &target)) {
            continue;
        }
        if (matches_united_at(reader, united, target)) {
            out->push_back(addr);
        }
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::NotFound, "united memory improve found no results"};
    }
    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
