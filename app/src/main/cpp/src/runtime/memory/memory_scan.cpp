#include "src/runtime/memory/memory_scan.h"

#include "src/runtime/memory/memory_area.h"
#include "src/runtime/memory/memory_map.h"
#include "src/runtime/memory/memory_reader.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 扫描分块大小
constexpr std::size_t kChunkSize = 64 * 1024;

// 判断 maps 条目是否应该参与扫描
bool should_scan(const MemoryMapEntry& entry, const ScanRequest& request) {
    if (!entry.readable()) {
        return false;
    }
    if (!request.custom_area.empty()) {
        return matches_custom_area(entry, request.custom_area);
    }
    return matches_area(entry, request.area);
}

// 扫描单个 VMA，overlap 用于避免跨块漏匹配
RuntimeResult scan_entry(
    const MemoryReader& reader,
    const MemoryMapEntry& entry,
    const SearchTerm& term,
    std::size_t max_results,
    std::vector<std::uintptr_t>* out
) {
    const std::size_t value_size = type_size(term.type);
    if (value_size == 0 || entry.size() < value_size) {
        return RuntimeResult{};
    }

    const std::size_t overlap = value_size > 0 ? value_size - 1 : 0;
    std::vector<std::uint8_t> buffer(kChunkSize + overlap);

    std::uintptr_t cursor = entry.start;
    while (cursor < entry.end) {
        const auto left = entry.end - cursor;
        const std::size_t base_read = static_cast<std::size_t>(
            std::min<std::uintptr_t>(left, static_cast<std::uintptr_t>(kChunkSize))
        );
        const std::size_t extra = static_cast<std::size_t>(
            std::min<std::uintptr_t>(entry.end - cursor - base_read, static_cast<std::uintptr_t>(overlap))
        );
        const std::size_t read_size = base_read + extra;

        auto read = reader.read(cursor, buffer.data(), read_size);
        if (!read.ok()) {
            cursor += static_cast<std::uintptr_t>(base_read);
            continue;
        }

        const std::size_t scan_limit = std::min(base_read, read_size >= value_size ? read_size - value_size + 1 : 0);
        for (std::size_t i = 0; i < scan_limit; ++i) {
            if (matches_value(buffer.data() + i, term)) {
                out->push_back(cursor + static_cast<std::uintptr_t>(i));
                if (max_results != 0 && out->size() >= max_results) {
                    return RuntimeResult{};
                }
            }
        }

        cursor += static_cast<std::uintptr_t>(base_read);
    }

    return RuntimeResult{};
}

// 计算联合搜索跨度结束位置
std::uintptr_t span_end(std::uintptr_t base, std::size_t span, std::uintptr_t entry_end) {
    std::uintptr_t end = 0;
    if (span > static_cast<std::size_t>(std::numeric_limits<std::uintptr_t>::max() - base)) {
        end = std::numeric_limits<std::uintptr_t>::max();
    } else {
        end = base + static_cast<std::uintptr_t>(span);
    }
    return std::min(end, entry_end);
}

// 在指定范围内寻找后续搜索项
bool find_term_after(
    const MemoryReader& reader,
    const MemoryMapEntry& entry,
    const SearchTerm& term,
    std::uintptr_t start,
    std::uintptr_t end,
    std::uintptr_t* found
) {
    const std::size_t size = type_size(term.type);
    if (found == nullptr || size == 0 || start >= end || end > entry.end) {
        return false;
    }

    std::array<std::uint8_t, 8> buffer{};
    for (std::uintptr_t cursor = start; cursor < end; ++cursor) {
        if (cursor > entry.end - static_cast<std::uintptr_t>(size)) {
            return false;
        }
        auto read = reader.read(cursor, buffer.data(), size);
        if (!read.ok()) {
            continue;
        }
        if (matches_value(buffer.data(), term)) {
            *found = cursor;
            return true;
        }
    }

    return false;
}

// 判断 base 位置是否满足完整联合搜索
bool matches_united_at(
    const MemoryReader& reader,
    const MemoryMapEntry& entry,
    const UnitedSearch& united,
    std::uintptr_t base
) {
    if (united.terms.size() < 2 || united.span == 0) {
        return false;
    }

    std::uintptr_t cursor = base + static_cast<std::uintptr_t>(type_size(united.terms.front().type));
    const std::uintptr_t end = span_end(base, united.span, entry.end);
    for (std::size_t i = 1; i < united.terms.size(); ++i) {
        std::uintptr_t found = 0;
        if (!find_term_after(reader, entry, united.terms[i], cursor, end, &found)) {
            return false;
        }
        cursor = found + static_cast<std::uintptr_t>(type_size(united.terms[i].type));
        if (cursor > end) {
            return i + 1 == united.terms.size();
        }
    }

    return true;
}

// 扫描单个 VMA 的联合搜索结果
RuntimeResult scan_united_entry(
    const MemoryReader& reader,
    const MemoryMapEntry& entry,
    const UnitedSearch& united,
    std::size_t max_results,
    std::vector<std::uintptr_t>* out
) {
    if (united.terms.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing united search terms"};
    }

    const SearchTerm& first = united.terms.front();
    const std::size_t value_size = type_size(first.type);
    if (value_size == 0 || entry.size() < value_size) {
        return RuntimeResult{};
    }

    const std::size_t overlap = value_size > 0 ? value_size - 1 : 0;
    std::vector<std::uint8_t> buffer(kChunkSize + overlap);

    std::uintptr_t cursor = entry.start;
    while (cursor < entry.end) {
        const auto left = entry.end - cursor;
        const std::size_t base_read = static_cast<std::size_t>(
            std::min<std::uintptr_t>(left, static_cast<std::uintptr_t>(kChunkSize))
        );
        const std::size_t extra = static_cast<std::size_t>(
            std::min<std::uintptr_t>(entry.end - cursor - base_read, static_cast<std::uintptr_t>(overlap))
        );
        const std::size_t read_size = base_read + extra;

        auto read = reader.read(cursor, buffer.data(), read_size);
        if (!read.ok()) {
            cursor += static_cast<std::uintptr_t>(base_read);
            continue;
        }

        const std::size_t scan_limit = std::min(base_read, read_size >= value_size ? read_size - value_size + 1 : 0);
        for (std::size_t i = 0; i < scan_limit; ++i) {
            const std::uintptr_t base = cursor + static_cast<std::uintptr_t>(i);
            if (matches_value(buffer.data() + i, first) && matches_united_at(reader, entry, united, base)) {
                out->push_back(base);
                if (max_results != 0 && out->size() >= max_results) {
                    return RuntimeResult{};
                }
            }
        }

        cursor += static_cast<std::uintptr_t>(base_read);
    }

    return RuntimeResult{};
}

} // namespace

// 创建指定进程的扫描器
MemoryScanner::MemoryScanner(MemProcess process)
    : process_(std::move(process)) {}

// 执行单值或范围搜索
RuntimeResult MemoryScanner::search(const ScanRequest& request, std::vector<std::uintptr_t>* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing memory search output"};
    }
    out->clear();
    if (!is_type_id(request.term.type)) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid memory search type"};
    }
    if (request.custom_area.empty() && !is_area_id(request.area)) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid memory search area"};
    }

    MemoryMap map(process_);
    std::vector<MemoryMapEntry> entries;
    auto maps = map.current(&entries);
    if (!maps.ok()) {
        return maps;
    }

    MemoryReader reader(process_);
    for (const auto& entry : entries) {
        if (!should_scan(entry, request)) {
            continue;
        }

        auto scanned = scan_entry(reader, entry, request.term, request.max_results, out);
        if (!scanned.ok()) {
            return scanned;
        }
        if (request.max_results != 0 && out->size() >= request.max_results) {
            break;
        }
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::NotFound, "memory search found no results"};
    }
    return RuntimeResult{};
}

// 执行联合搜索
RuntimeResult MemoryScanner::search_united(
    const ScanRequest& request,
    const UnitedSearch& united,
    std::vector<std::uintptr_t>* out
) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing united memory search output"};
    }
    out->clear();
    if (united.terms.size() < 2) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "united memory search needs multiple terms"};
    }
    if (request.custom_area.empty() && !is_area_id(request.area)) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid united memory search area"};
    }

    MemoryMap map(process_);
    std::vector<MemoryMapEntry> entries;
    auto maps = map.current(&entries);
    if (!maps.ok()) {
        return maps;
    }

    MemoryReader reader(process_);
    for (const auto& entry : entries) {
        if (!should_scan(entry, request)) {
            continue;
        }

        auto scanned = scan_united_entry(reader, entry, united, request.max_results, out);
        if (!scanned.ok()) {
            return scanned;
        }
        if (request.max_results != 0 && out->size() >= request.max_results) {
            break;
        }
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::NotFound, "united memory search found no results"};
    }
    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
