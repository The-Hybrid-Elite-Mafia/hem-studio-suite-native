#include "../Source/Systems/ReductionHistory.h"
#include <cstdlib>
#include <new>
#include <malloc.h>
#include <iostream>
#include <stdexcept>
#include <thread>
static thread_local bool probe=false;
static std::atomic<unsigned> allocations{0},frees{0};
void* operator new(size_t n){if(probe)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(probe&&p)++frees;std::free(p);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
void* operator new(size_t n,std::align_val_t a){if(probe)++allocations;if(auto* p=_aligned_malloc(n?n:1,size_t(a)))return p;throw std::bad_alloc();}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{if(probe&&p)++frees;_aligned_free(p);}
void operator delete[](void* p,std::align_val_t a)noexcept{::operator delete(p,a);}
void operator delete(void* p,size_t,std::align_val_t a)noexcept{::operator delete(p,a);}
void operator delete[](void* p,size_t,std::align_val_t a)noexcept{::operator delete(p,a);}
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    using H=hem::ReductionHistory;static_assert(sizeof(H)<32768,"bounded history storage");
    unsigned cases=0;
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})for(unsigned size:{1u,32u,64u,127u,256u,511u,1024u,4096u,16384u,0u}){
        H h;h.prepare(rate,7);std::uint64_t total=0,serial=0,previous=0,start=0;float peak=0,aux=0;unsigned block=0;
        const unsigned varied[]={1,256,127,4096,32,511,64,1024};
        while(total<std::uint64_t(rate*8)){
            const unsigned n=size?size:varied[block%8];const float v=block%17==0?19.f:2.f,a=block%23==0?23.f:1.f;
            probe=true;serial=h.push(n,v,a);probe=false;
            if(serial!=previous){start=total;peak=v;aux=a;}else{peak=std::max(peak,v);aux=std::max(aux,a);}total+=n;previous=serial;++block;
            H::Point p;check(h.read(serial,7,p),"current bucket read");
            check(p.startSample==start&&p.endSample==total&&p.value==peak&&p.auxiliary==aux,"actual interval or independent peak aggregation");
        }
        check(serial>0&&serial<=801,"bucket count independent of callback frequency");
        const auto count=std::min<std::uint64_t>(H::capacity,serial);H::Point oldest,newest;
        check(h.read(serial-count+1,7,oldest)&&h.read(serial,7,newest),"retained endpoints");
        check(double(newest.endSample-oldest.startSample)>=rate*4,"full four-second steady-state retention");
        float first=2,last=0;unsigned visible=0;
        for(auto i=serial-count+1;i<=serial;++i){H::Point p;check(h.read(i,7,p),"retained ring slot");float l,r;
            if(H::interval(p,newest.endSample,rate,l,r)){check(l>=0&&r<=1&&l<r,"viewport bounds");if(visible)check(std::abs(l-last)<1e-6,"contiguous measured intervals");else first=l;last=r;++visible;}}
        check(first==0&&last==1&&visible<=H::capacity,"renderer covers exact fixed four seconds");
        if(serial>H::capacity)check(!h.read(serial-H::capacity,7,oldest),"overwritten serial rejected");
        h.prepare(rate,8);check(!h.read(serial,8,newest),"old epoch rejected");serial=h.push(1,5,9);check(serial==1&&h.read(1,8,newest),"reset serial");
        float l,r;check(H::interval(newest,1,rate,l,r)&&l>.99f&&r==1,"startup must not fabricate four seconds");++cases;
    }
    H h;h.prepare(48000,9);std::atomic<bool> done{false},bad{false};std::atomic<std::uint64_t> latest{0};
    std::thread reader([&]{while(!done){H::Point p;const auto s=latest.load();if(h.read(s,9,p)&&(p.auxiliary!=2*p.value||p.startSample>=p.endSample))bad=true;}});
    probe=true;for(unsigned i=1;i<=200000;++i)latest=h.push(1,float(i),2.f*i);probe=false;done=true;reader.join();
    check(!bad,"torn same-bucket concurrent publication");
    h.prepare(0,10);check(h.push(256,2,3)==0,"invalid sample rate");
    check(allocations==0&&frees==0,"callback allocated or freed memory");
    std::cout<<"PASS "<<cases<<" rate/block cases: 8k/44.1k/48k/96k/192k/384k; 1..16384 and variable blocks; exact 4s viewport, startup, ring wrap, epochs, independent VCA/limiter peaks; concurrent same-bucket publication; callback new/delete (ordinary+aligned)=0/0; storage="<<sizeof(H)<<" bytes\n";
    return 0;
}catch(const std::exception& e){probe=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
