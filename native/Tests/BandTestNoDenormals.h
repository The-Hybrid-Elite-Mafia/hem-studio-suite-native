#pragma once
#include <xmmintrin.h>
// Windows x64 standalone-test equivalent of JUCE ScopedNoDenormals:
// save MXCSR, enable FTZ+DAZ, restore on scope exit. No history truncation.
struct BandTestNoDenormals {
    unsigned saved=_mm_getcsr();
    BandTestNoDenormals() noexcept {_mm_setcsr(saved|0x8040u);}
    ~BandTestNoDenormals() noexcept {_mm_setcsr(saved);}
    BandTestNoDenormals(const BandTestNoDenormals&)=delete;
    BandTestNoDenormals& operator=(const BandTestNoDenormals&)=delete;
};
