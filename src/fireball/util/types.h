#pragma once

#include <algorithm>
#include <cstdint>
#include <set>

struct Range_Allocation {
    uint32_t base = 0;
    uint32_t count = 0;

    bool valid() const { return count != 0; }
};

class Range_Allocator {
public:
    Range_Allocator(uint32_t cap) : capacity(cap), max_allocated(0) {
        m_free.insert({ 0, capacity });
    }

    Range_Allocation allocate(uint32_t count) {
        for (auto it = m_free.begin(); it != m_free.end(); ++it) {
            if (it->count >= count) {
                Range_Allocation alloc{ it->base, count };

                Free_Range remaining{
                    it->base + count,
                    it->count - count
                };

                m_free.erase(it);
                if (remaining.count > 0)
                    m_free.insert(remaining);

                max_allocated = std::max(max_allocated, alloc.base + alloc.count);

                return alloc;
            }
        }

        return {}; // assert false?
    }

    void free(Range_Allocation alloc) {
        if (!alloc.valid())
            return;

        Free_Range freed{ alloc.base, alloc.count };

        auto it = m_free.lower_bound(freed);

        // merge left
        if (it != m_free.begin()) {
            auto prev = std::prev(it);
            if (prev->base + prev->count == freed.base) {
                freed.base = prev->base;
                freed.count += prev->count;
                m_free.erase(prev);
            }
        }

        // merge right
        if (it != m_free.end()) {
            if (freed.base + freed.count == it->base) {
                freed.count += it->count;
                m_free.erase(it);
            }
        }

        m_free.insert(freed);
    }

    uint32_t max_allocated;

private:
    struct Free_Range {
        uint32_t base;
        uint32_t count;
    };

    struct Compare {
        bool operator()(const Free_Range& a, const Free_Range& b) const {
            return a.base < b.base;
        }
    };

    void try_shrink_max_used() {
        if (m_free.empty())
            return;

        auto last = std::prev(m_free.end());

        if (last->base + last->count == max_allocated) {
            max_allocated = last->base;
        }
    }

    uint32_t capacity;
    std::set<Free_Range, Compare> m_free;
};
