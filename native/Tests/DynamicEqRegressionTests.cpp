// Existing independent Dynamic EQ fixtures, unchanged tolerances and allocation
// probes. This entry point avoids unrelated module/timing campaigns.
#define main existingBandModuleMain
#include "BandModuleTests.cpp"
#undef main
int main(){try{
    BandTestNoDenormals guard;allocationProbe::testThread=true;
    const int hooks=platformProbe::install();
    allocationProbe::enabled=true;void* proof=::operator new(37);::operator delete(proof);allocationProbe::enabled=false;
    require(allocationProbe::count>0,"allocation probe positive control");allocationProbe::count=0;platformProbe::count=0;
    for(double rate:{44100.,48000.,96000.}){
        expandedBasics<Kind::Dynamic>(rate);
        expandedDynamicReview(rate);
        expandedControls<Kind::Dynamic>(rate);
        expandedPartitions<Kind::Dynamic>(rate);
        std::cout<<"PASS Dynamic EQ independent physics/controls/partitions rate="<<rate<<'\n';
    }
    require(allocationProbe::count==0&&platformProbe::count==0,"Dynamic EQ callback allocation");
    std::cout<<"PASS Dynamic EQ focused regression; callback allocations=0; executable import hooks="<<hooks<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
