#pragma once
#include <atomic>
#include <cmath>
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#include <immintrin.h>
#endif
namespace hem::bands::accurate {
// Constant initialization: processing before prepare remains on the portable path.
// No CPUID, OS queries, initialization guard, allocation or locks in multiplyAdd.
inline std::atomic<bool> hardwareFmaReady{false};
inline bool queryHardwareFma() noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    int registers[4]{};__cpuid(registers,1);
    constexpr int required=(1<<12)|(1<<27)|(1<<28); // FMA, OSXSAVE, AVX
    if((registers[2]&required)!=required)return false;
    return (_xgetbv(0)&6)==6; // OS saves XMM and YMM state.
#else
    return false;
#endif
}
// Preparation thread only. The one-time guard is never reached by the callback.
inline void prepareFusedMultiplyAdd() noexcept {
    static const bool supported=queryHardwareFma();
    hardwareFmaReady.store(supported,std::memory_order_relaxed);
}
inline double multiplyAdd(double a,double b,double c) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    if(hardwareFmaReady.load(std::memory_order_relaxed))
        return _mm_cvtsd_f64(_mm_fmadd_sd(_mm_set_sd(a),_mm_set_sd(b),_mm_set_sd(c)));
#endif
    return std::fma(a,b,c);
}
static_assert(std::atomic<bool>::is_always_lock_free);
}
