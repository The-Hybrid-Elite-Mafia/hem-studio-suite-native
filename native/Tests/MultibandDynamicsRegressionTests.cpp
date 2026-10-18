// Reuse the existing independent physics/control fixtures unchanged. This
// focused entry point avoids unrelated EQ timing campaigns while qualifying
// the Dynamics caller of the stereo SIMD crossover implementation.
#define main existingBandModuleMain
#include "BandModuleTests.cpp"
#undef main
int main(){try{
    BandTestNoDenormals guard;allocationProbe::testThread=true;
    const int hooks=platformProbe::install();
    allocationProbe::enabled=true;void* proof=::operator new(37);::operator delete(proof);allocationProbe::enabled=false;
    require(allocationProbe::count>0,"allocation probe positive control");allocationProbe::count=0;platformProbe::count=0;
    for(double rate:{44100.,48000.,96000.}){
        expandedBankProcessingReview<Kind::Dynamics>(rate);
        expandedControls<Kind::Dynamics>(rate);
        expandedPartitions<Kind::Dynamics>(rate);
        std::cout<<"PASS Dynamics physics/controls/partitions rate="<<rate<<'\n';
    }
    require(allocationProbe::count==0&&platformProbe::count==0,"Dynamics callback allocation");
    std::cout<<"PASS Dynamics focused regression; callback allocations=0; executable import hooks="<<hooks<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
