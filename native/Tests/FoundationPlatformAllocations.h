#pragma once
// Test-executable-only import-table probes. No production code or other process
// is patched. Install before preparation; counters enabled only around callbacks.
// Coverage is executable imports, NOT internal calls made inside system DLLs.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#include <iostream>
namespace platformProbe {
inline std::atomic<unsigned long long> count{0};
inline std::atomic<unsigned long long> otherThreadCount{0};
inline void record(){if(allocationProbe::enabled.load(std::memory_order_relaxed))(allocationProbe::testThread?count:otherThreadCount).fetch_add(1,std::memory_order_relaxed);}
using Malloc=void*(__cdecl*)(size_t);using Calloc=void*(__cdecl*)(size_t,size_t);using Realloc=void*(__cdecl*)(void*,size_t);
using Aligned=void*(__cdecl*)(size_t,size_t);using AlignedRealloc=void*(__cdecl*)(void*,size_t,size_t);
inline Malloc realMalloc=nullptr;inline Calloc realCalloc=nullptr;inline Realloc realRealloc=nullptr;
inline Aligned realAligned=nullptr;inline AlignedRealloc realAlignedRealloc=nullptr;
inline decltype(&HeapAlloc) realHeap=nullptr;inline decltype(&HeapReAlloc) realHeapRealloc=nullptr;inline decltype(&VirtualAlloc) realVirtual=nullptr;
inline void* __cdecl mallocProbe(size_t n){record();return realMalloc(n);}
inline void* __cdecl callocProbe(size_t n,size_t s){record();return realCalloc(n,s);}
inline void* __cdecl reallocProbe(void* p,size_t n){record();return realRealloc(p,n);}
inline void* __cdecl alignedProbe(size_t n,size_t a){record();return realAligned(n,a);}
inline void* __cdecl alignedReallocProbe(void* p,size_t n,size_t a){record();return realAlignedRealloc(p,n,a);}
inline LPVOID WINAPI heapProbe(HANDLE h,DWORD f,SIZE_T n){record();return realHeap(h,f,n);}
inline LPVOID WINAPI heapReallocProbe(HANDLE h,DWORD f,LPVOID p,SIZE_T n){record();return realHeapRealloc(h,f,p,n);}
inline LPVOID WINAPI virtualProbe(LPVOID p,SIZE_T n,DWORD a,DWORD b){record();return realVirtual(p,n,a,b);}
template<class T> inline bool replace(ULONG_PTR* location,T replacement,T& original){
 DWORD flags=0;if(!VirtualProtect(location,sizeof(*location),PAGE_READWRITE,&flags))return false;
 original=reinterpret_cast<T>(*location);*location=reinterpret_cast<ULONG_PTR>(replacement);
 DWORD unused=0;const bool restored=VirtualProtect(location,sizeof(*location),flags,&unused)!=0;
 return restored;
}
inline int install(){
 auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
 auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
 const auto imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!imports.VirtualAddress)return 0;
 auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+imports.VirtualAddress);int hooked=0;
 for(;descriptor->Name;++descriptor){if(!descriptor->OriginalFirstThunk)continue;
  auto* names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+descriptor->OriginalFirstThunk);auto* addresses=reinterpret_cast<IMAGE_THUNK_DATA*>(base+descriptor->FirstThunk);
  for(;names->u1.AddressOfData;++names,++addresses){if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;
   const auto* symbol=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);const auto* name=reinterpret_cast<const char*>(symbol->Name);bool ok=false;
#define HEM_HOOK(label,probe,original) if(std::strcmp(name,label)==0)ok=replace(&addresses->u1.Function,&probe,original)
   HEM_HOOK("malloc",mallocProbe,realMalloc);else HEM_HOOK("calloc",callocProbe,realCalloc);else HEM_HOOK("realloc",reallocProbe,realRealloc);
   else HEM_HOOK("_aligned_malloc",alignedProbe,realAligned);else HEM_HOOK("_aligned_realloc",alignedReallocProbe,realAlignedRealloc);
   else HEM_HOOK("HeapAlloc",heapProbe,realHeap);else HEM_HOOK("HeapReAlloc",heapReallocProbe,realHeapRealloc);else HEM_HOOK("VirtualAlloc",virtualProbe,realVirtual);
#undef HEM_HOOK
   if(ok){++hooked;std::cout<<"instrumented executable import: "<<name<<'\n';}
  }
 }return hooked;
}
}
#else
namespace platformProbe {inline std::atomic<unsigned long long> count{0},otherThreadCount{0};inline int install(){return 0;}}
#endif
