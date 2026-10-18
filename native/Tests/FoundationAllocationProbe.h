#pragma once
#include <atomic>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
namespace allocationProbe { inline std::atomic<bool> enabled{false}; inline std::atomic<unsigned long long> count{0};
inline thread_local bool testThread=false;
inline std::atomic<unsigned long long> otherThreadCount{0};
// The callback is synchronous on the marked test thread. Background JUCE
// timers remain visible separately, but are not audio-callback allocations.
inline void record(){if(enabled.load(std::memory_order_relaxed))(testThread?count:otherThreadCount).fetch_add(1,std::memory_order_relaxed);}
}
void* operator new(std::size_t n){allocationProbe::record();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
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
