#include "../Source/DSP/DrumReplacerCore.h"
#include <thread>
#include <iostream>
#include <stdexcept>
static void check(bool c,const char* m){if(!c)throw std::runtime_error(m);}
int main(){try{
 using namespace hem::drum;Core core;check(core.prepare(48000,{}),"prepare");DetectorHistorySnapshot snapshot;check(core.detectorHistory(snapshot)&&snapshot.count==0,"empty history");
 std::array<float,480> l{},r{};l[11]=.5f;r[13]=-.75f;l[250]=.25f;Parameters p;p.retriggerMs=1;
 core.process(l.data(),r.data(),480,p);check(core.detectorHistory(snapshot)&&snapshot.count==2,"two bins");
 check(snapshot.bins[0].minimum==-.75f&&snapshot.bins[0].maximum==.5f&&snapshot.bins[0].peak==.75f,"measured stereo extrema");
 check(snapshot.bins[0].hits==1&&snapshot.bins[0].firstOnsetSample==11&&snapshot.bins[1].firstOnsetSample==250,"exact accepted onset");
 check(snapshot.totalSamples==480&&snapshot.samplesPerBin==240&&snapshot.acceptedHits==2,"coherent range");
 const auto oldEpoch=snapshot.epoch;core.reset();check(core.detectorHistory(snapshot)&&snapshot.count==0&&snapshot.epoch>oldEpoch,"reset epoch");
 l.fill(0);r.fill(0);core.process(l.data(),r.data(),17,p);p.sensitivity=.2f;core.process(l.data(),r.data(),240,p);
 check(core.detectorHistory(snapshot)&&snapshot.count==2&&snapshot.bins[0].endSample==17&&snapshot.bins[1].firstSample==17,"threshold boundary flush");
 check(snapshot.bins[0].thresholdDb==-33&&std::abs(snapshot.bins[1].thresholdDb-(-16.8f))<.001f,"historical thresholds");
 std::atomic<bool> done{false};std::thread writer([&]{for(int i=0;i<20000;++i)core.process(l.data(),r.data(),480,p);done.store(true);});
 bool coherent=true;size_t reads=0;while(!done.load()){DetectorHistorySnapshot s;if(!core.detectorHistory(s))continue;++reads;if(s.count<0||s.count>256)coherent=false;for(int k=1;k<s.count;++k)if(s.bins[k-1].endSample!=s.bins[k].firstSample)coherent=false;if(s.count&&s.bins[s.count-1].endSample!=s.totalSamples)coherent=false;}writer.join();check(coherent&&reads>0,"concurrent coherent ring snapshot");
 check(core.detectorHistory(snapshot)&&snapshot.count==256,"bounded ring wrap");std::cout<<"PASS Drum history: real stereo extrema/onsets, thresholds, reset epochs, bounded ring and concurrent coherent snapshots\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
