#include "../Source/DSP/MonitoringRouter.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* t){if(!b)throw std::runtime_error(t);}
static double rms(const std::vector<float>& x){double s=0;for(size_t i=x.size()/2;i<x.size();++i)s+=double(x[i])*x[i];return std::sqrt(s/(x.size()-x.size()/2));}
int main(){try{
 for(double sr:{44100.,48000.,96000.}){
    hem::MonitoringRouter router;router.prepare(sr);hem::MonitoringRouter::Curve curve;curve.count=1;curve.preamp=hem::gain(-3);curve.filters[0].configure(hem::Biquad::Bell,sr,1000,.7071f,-6);
    require(router.installPrepared(curve),"Install failed");require(!router.installPrepared(curve),"Overlapping transition accepted");
    const int n=int(sr*.3);std::vector<float> l(n),r(n),a(n),b(n);for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=.25f*float(std::sin(2*hem::pi*1000*i/sr));const auto original=l;
    router.renderCalibration(l.data(),r.data(),a.data(),b.data(),n);router.mixRoute(l.data(),r.data(),a.data(),b.data(),n);require(a==l&&b==r,"Direct monitoring altered master");
    router.setHeadphoneMode(true);router.renderCalibration(l.data(),r.data(),a.data(),b.data(),n);router.mixRoute(l.data(),r.data(),a.data(),b.data(),n);
    const auto db=20*std::log10(rms(a)/rms(l));require(std::abs(db+9)<.02,"Calibration gain incorrect");require(l==original,"Calibration modified master source");
    router.setHeadphoneMode(false);router.renderCalibration(l.data(),r.data(),a.data(),b.data(),n);router.mixRoute(l.data(),r.data(),a.data(),b.data(),n);require(a.back()==l.back(),"Direct return did not reach unity");
    router.prepare(sr);require(!router.headphoneMode(),"Device restart retained correction");std::cout<<sr<<" Hz: direct exact / calibration "<<db<<" dB / source unchanged\n";
 }
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
