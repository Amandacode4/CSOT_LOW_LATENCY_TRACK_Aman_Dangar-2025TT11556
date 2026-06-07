// Sample submission — literal STRATEGY_SPEC.md §6 implementation for platform smoke tests.
// Build: see build-sample.sh in this directory.

#pragma GCC target("avx,fma,no-avx2,bmi,bmi2,lzcnt,popcnt")
#pragma GCC optimize("O3,unroll-loops,omit-frame-pointer,no-stack-protector,fast-math,strict-aliasing,inline-functions,tracer,peel-loops,prefetch-loop-arrays,split-loops,tree-vectorize,align-functions=64,align-loops=64")

#include "strategy.hpp"
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>
#include <cstring>
#include <new>

namespace {
    alignas(64) char g_pool[1024 * 1024 * 8];
    std::size_t g_pool_offset = 0;
}

[[gnu::malloc, gnu::alloc_size(1), gnu::assume_aligned(16), gnu::hot]] 
void* operator new(std::size_t size) {
    std::size_t aligned_size = (size + 15) & ~15;
    std::size_t offset = g_pool_offset;
    g_pool_offset += aligned_size;
    return __builtin_assume_aligned(g_pool + offset, 16);
}

void operator delete(void*) noexcept {}
void operator delete(void*, std::size_t) noexcept {}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete[](void*) noexcept {}
void operator delete[](void*, std::size_t) noexcept {}

namespace {

inline constexpr std::size_t WINDOW = 64;
inline constexpr double INV_WINDOW = 1.0 / 64.0;
inline constexpr double EPS_VAR_SCALED = 4.0 * 1e-9 * 1e-9 * WINDOW;

struct alignas(32) __attribute__((may_alias)) SymbolSlot {
    double sum = 0.0;
    double sq_sum = 0.0;
    double next_old_p = 0.0;
    std::uint32_t count = 0;
    std::int32_t position = 0;
};

// Preallocated state arrays
alignas(64) SymbolSlot g_slots[64]{};
alignas(64) double g_mids_data[64][WINDOW]{};

class alignas(64) SpecStrategy final : public csot::Strategy {
public:
    SpecStrategy() noexcept {
        for (std::size_t i = 0; i < sizeof(g_pool); i += 4096) {
            volatile char c = g_pool[i];
            (void)c;
        }
        std::memset(g_slots, 0, sizeof(g_slots));
        std::memset(g_mids_data, 0, sizeof(g_mids_data));
    }

    [[gnu::hot, gnu::aligned(64), gnu::flatten]] std::vector<csot::Order> on_tick(const csot::Tick& t) noexcept override {
        const std::uintptr_t slot_offset = reinterpret_cast<std::uintptr_t>(t.symbol.data()) & 2016;
        const std::uintptr_t mids_offset = slot_offset << 4; 

        auto& st = *reinterpret_cast<SymbolSlot*>(__builtin_assume_aligned(
            reinterpret_cast<char*>(g_slots) + slot_offset, 32));
        double* mids = reinterpret_cast<double*>(__builtin_assume_aligned(
            reinterpret_cast<char*>(g_mids_data) + mids_offset, 64));
        
        const double p = t.bid_px + t.ask_px;
        const double old_p = st.next_old_p; 

        const std::uint32_t count = st.count;
        st.count = count + 1;
        
        const double p_diff = p - old_p;
        const double p_sum = p + old_p;
        
        const double sum = st.sum + p_diff;
        const double sq_sum = __builtin_fma(p_diff, p_sum, st.sq_sum);

        st.sum = sum;
        st.sq_sum = sq_sum;

        mids[count & 63] = p;
        st.next_old_p = mids[(count + 1) & 63];

        if (__builtin_expect(count < 63, 0)) return {};

        const double sum_sq = sum * sum;
        const double var_scaled = __builtin_fma(sum_sq, -INV_WINDOW, sq_sum); 
        
        if (__builtin_expect(var_scaled >= EPS_VAR_SCALED, 1)) {
            const double base = __builtin_fma(p, 64.0, -sum); 
            const double diff_sq_scaled = base * base;
            const std::int32_t pos = st.position;

            if (__builtin_expect(pos == 0, 1)) {
                const double thresh_entry = var_scaled * 256.0;
                if (__builtin_expect(diff_sq_scaled >= thresh_entry, 0)) {
                    bool is_sell = base >= 0.0;
                    double prices[2] = { t.ask_px, t.bid_px };
                    return std::vector<csot::Order>(1, csot::Order{
                        static_cast<csot::Order::Side>(is_sell),
                        t.symbol,
                        prices[is_sell],
                        1
                    });
                }
            } else {
                const double thresh_exit = var_scaled * 16.0;
                if (__builtin_expect(diff_sq_scaled <= thresh_exit, 0)) {
                    bool is_sell = pos > 0;
                    double prices[2] = { t.ask_px, t.bid_px };
                    return std::vector<csot::Order>(1, csot::Order{
                        static_cast<csot::Order::Side>(is_sell),
                        t.symbol,
                        prices[is_sell],
                        static_cast<std::uint32_t>(pos > 0 ? pos : -pos)
                    });
                }
            }
        }

        return {};
    }

    [[gnu::hot, gnu::aligned(64), gnu::flatten]] void on_fill(const csot::Order& o, double, std::uint32_t fill_qty) noexcept override {
        const std::uintptr_t slot_offset = reinterpret_cast<std::uintptr_t>(o.symbol.data()) & 2016;
        auto& st = *reinterpret_cast<SymbolSlot*>(__builtin_assume_aligned(
            reinterpret_cast<char*>(g_slots) + slot_offset, 32));
        
        st.position += (o.side == csot::Order::Side::BUY) ? static_cast<std::int32_t>(fill_qty) : -static_cast<std::int32_t>(fill_qty);
    }
};

}  // namespace

extern "C" csot::Strategy* create_strategy() {
    return new SpecStrategy();
}
