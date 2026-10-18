#include "../Source/Systems/MonitorProfile.h"
#include <iostream>
#include <stdexcept>
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
 hem::MonitorProfile p;p.name="Numerical test curve";p.device="Synthetic test signal";p.connection="Test";p.deviceMode="Test";p.measurementSource="Generated verification case, not a headphone measurement";p.preampDb=-3;
 p.bands={{hem::MonitorProfile::Filter::Bell,1000,.7071f,-6},{hem::MonitorProfile::Filter::LowShelf,100,.7071f,3},{hem::MonitorProfile::Filter::HighShelf,10000,.7071f,-2}};
 auto f=juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("hem-profile",".json");hem::MonitorProfile::save(p,f);auto restored=hem::MonitorProfile::load(f);auto curve=restored.prepare(96000);
 require(curve.count==3&&std::abs(curve.preamp-hem::gain(-3))<1.e-6,"Profile round trip");
 require(std::abs(hem::decibels(curve.filters[0].magnitude(1000,96000))+6)<.01,"Bell coefficients");
 require(std::abs(hem::decibels(curve.filters[1].magnitude(1,96000))-3)<.01,"Low shelf coefficients");
 require(std::abs(hem::decibels(curve.filters[2].magnitude(47000,96000))+2)<.01,"High shelf coefficients");
 restored.bands[2].q=.4f;auto wide=restored.prepare(96000);restored.bands[2].q=1.5f;auto sharp=restored.prepare(96000);
 require(std::abs(wide.filters[2].magnitude(7000,96000)-sharp.filters[2].magnitude(7000,96000))>.01,"Shelf Q ignored");
 bool rejected=false;try{restored.prepare(8000);}catch(...){rejected=true;}require(rejected,"Above-Nyquist calibration accepted");
 f.replaceWithText("{bad json}");rejected=false;try{hem::MonitorProfile::load(f);}catch(...){rejected=true;}require(rejected,"Malformed profile accepted");f.deleteFile();
 std::cout<<"PASS: measured-profile schema round trip, coefficient responses, invalid-rate/file rejection\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
