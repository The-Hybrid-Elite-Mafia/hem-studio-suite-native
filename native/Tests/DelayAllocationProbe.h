#pragma once
#include <atomic>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
namespace allocationProbe { inline std::atomic<bool> enabled{false}; inline std::atomic<unsigned long long> count{0};
inline thread_local bool testThread=false; inline thread_local std::size_t failAboveBytes=0;
inline std::atomic<unsigned long long> otherThreadCount{0};
// The callback is synchronous on the marked test thread. Background JUCE
// timers remain visible separately, but are not audio-callback allocations.
inline void record(){if(enabled.load(std::memory_order_relaxed))(testThread?count:otherThreadCount).fetch_add(1,std::memory_order_relaxed);}
}
void* operator new(std::size_t n){if(allocationProbe::failAboveBytes&&n>=allocationProbe::failAboveBytes){allocationProbe::failAboveBytes=0;throw std::bad_alloc();}allocationProbe::record();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {std::free(p);}
void* operator new(std::size_t n,std::align_val_t alignment){allocationProbe::record();
#ifdef _WIN32
 if(auto p=_aligned_malloc(n?n:1,size_t(alignment)))return p;
#else
 const auto a=size_t(alignment);if(auto p=std::aligned_alloc(a,((n+a-1)/a)*a))return p;
#endif
 throw std::bad_alloc();}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {
#ifdef _WIN32
 _aligned_free(p);
#else
 std::free(p);
#endif
}
void operator delete[](void* p,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete(void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete[](void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&) noexcept {::operator delete(p,a);}
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&) noexcept {::operator delete(p,a);}

namespace allocationProbe {
// Coverage: linked C++ global scalar/array, aligned and nothrow new overloads.
// Direct malloc/calloc/realloc, Windows heap APIs and allocations in a separate
// DLL/CRT are NOT intercepted. This is not a universal heap-allocation detector.
inline bool calibrate(){
 const auto before=count.load();testThread=true;enabled=true;
 void* p1=::operator new(17);void* p2=::operator new[](19);
 void* p3=::operator new(23,std::nothrow);void* p4=::operator new[](29,std::nothrow);
 constexpr auto a=std::align_val_t(64);
 void* p5=::operator new(64,a);void* p6=::operator new[](128,a);
 void* p7=::operator new(64,a,std::nothrow);void* p8=::operator new[](128,a,std::nothrow);
 enabled=false;const bool detected=count.load()==before+8;
 ::operator delete(p1);::operator delete[](p2);::operator delete(p3,std::nothrow);::operator delete[](p4,std::nothrow);
 ::operator delete(p5,a);::operator delete[](p6,a);::operator delete(p7,a,std::nothrow);::operator delete[](p8,a,std::nothrow);
 count=before;return detected;
}
}

