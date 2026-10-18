#define main schedulerParityMain
#include "TrackSchedulerTests.cpp"
#undef main
namespace {
void settle(MainComponent& h){for(int i=0;i<5000&&MainComponentTestAccess::transportPending(h);++i){MainComponentTestAccess::pollTransport(h);juce::Thread::sleep(1);}require(!MainComponentTestAccess::transportPending(h),"transport job did not settle");}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 SchedulerSourceFile file;auto host=std::make_unique<MainComponent>(false);MainComponentTestAccess::helpers(*host,2);
 juce::AudioBuffer<float> pcm(2,65536);for(int i=0;i<pcm.getNumSamples();++i){pcm.setSample(0,i,.05f*std::sin(i*.1f));pcm.setSample(1,i,.04f*std::cos(i*.13f));}
 MainComponentTestAccess::setup(*host,file,pcm,false,0);MainComponentTestAccess::seek(*host,.1);settle(*host);
 for(int i=0;i<13;++i)require(std::abs(MainComponentTestAccess::position(*host,i)-.1)<1.e-9,"group seek differs");
 std::atomic<bool> held{true},entered{false};
 struct ReleaseHost {std::atomic<bool>& held;std::unique_ptr<MainComponent>& host;~ReleaseHost(){held=false;host.reset();}} cleanup{held,host};
 MainComponentTestAccess::readHook(*host,0,[&]{entered=true;while(held.load())juce::Thread::sleep(1);});
 MainComponentTestAccess::seek(*host,.2);for(int i=0;i<1000&&!entered.load();++i)juce::Thread::sleep(1);
 const bool didEnter=entered.load();auto start=juce::Time::getHighResolutionTicks();MainComponentTestAccess::cancelTransport(*host);MainComponentTestAccess::changeGeneration(*host);
 const double cancelMs=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-start)*1000;
 held=false;settle(*host);MainComponentTestAccess::readHook(*host,0,{});
 require(didEnter&&cancelMs<100,"cancel blocked on decoder");for(int i=0;i<13;++i)require(std::abs(MainComponentTestAccess::position(*host,i)-.1)<1.e-9,"late cancelled epoch published");
 std::cout<<"PASS asynchronous13-track seek; held-decoder cancellation returned "<<cancelMs<<"ms; generation-stale completion did not publish\n";
 held=true;entered=false;MainComponentTestAccess::readHook(*host,0,[&]{entered=true;while(held.load())juce::Thread::sleep(1);});
 MainComponentTestAccess::startAllForTest(*host);juce::AudioBuffer<float> audio(2,256);bool coordinated=true;int callbacks=0;
 for(;callbacks<1024&&!MainComponentTestAccess::faulted(*host);++callbacks){std::array<double,13> before{};for(int i=0;i<13;++i)before[size_t(i)]=MainComponentTestAccess::position(*host,i);MainComponentTestAccess::callback(*host,audio,256);
  if(MainComponentTestAccess::faulted(*host)){for(int i=0;i<13;++i)coordinated=coordinated&&before[size_t(i)]==MainComponentTestAccess::position(*host,i);coordinated=coordinated&&audio.getMagnitude(0,256)==0;}}
 const bool fault=MainComponentTestAccess::faulted(*host);const auto dropped=MainComponentTestAccess::silences(*host),underruns=MainComponentTestAccess::underruns(*host);held=false;MainComponentTestAccess::readHook(*host,0,{});
 require(fault&&coordinated&&dropped==1&&underruns==1,"underrun advanced only part of group or counted silence as success");
 require(MainComponentTestAccess::completed(*host)==uint64_t(callbacks-1),"delivered count includes dropout");
 std::cout<<"PASS held decoder exhausts bounded ring: entire callback silent, all13 cursors held, explicit fault/dropout counters; no partial stem advance\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
