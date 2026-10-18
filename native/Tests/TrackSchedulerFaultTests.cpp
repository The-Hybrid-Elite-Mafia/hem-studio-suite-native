#define main schedulerParityMain
#include "TrackSchedulerTests.cpp"
#undef main
namespace {
std::atomic<int> calls{0}, entered{0};std::atomic<bool> releaseHelpers{false};int skipCalls=0;
void holdHelper()noexcept{if(calls.fetch_add(1)>=skipCalls){++entered;while(!releaseHelpers.load())juce::Thread::sleep(1);}}
bool zero(const juce::AudioBuffer<float>& b){for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<b.getNumSamples();++i)if(b.getSample(c,i)!=0)return false;return true;}
void fill(juce::AudioBuffer<float>& b){for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<b.getNumSamples();++i)b.setSample(c,i,.25f);}
struct ReleaseOnExit{~ReleaseOnExit(){releaseHelpers=true;}};
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 auto host=std::make_unique<MainComponent>(false);ReleaseOnExit safety;
 require(MainComponentTestAccess::helpers(*host,2),"startup");MainComponentTestAccess::rate(*host,96000,256);
 // The first 256-frame chunk completes. Only helpers in the second chunk stall.
 skipCalls=2;calls=0;entered=0;releaseHelpers=false;MainComponentTestAccess::stall(*host,holdHelper);
 juce::AudioBuffer<float> audio(2,512);fill(audio);const auto begin=juce::Time::getHighResolutionTicks();
 MainComponentTestAccess::callback(*host,audio,512);
 const double ms=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-begin)*1000;
 const bool first=zero(audio)&&MainComponentTestAccess::completed(*host)==0&&MainComponentTestAccess::faults(*host)==1&&MainComponentTestAccess::silences(*host)==1;
 for(int i=0;i<100&&entered.load()<2;++i)juce::Thread::sleep(1);
 const int before=calls.load();fill(audio);MainComponentTestAccess::callback(*host,audio,512);
 const bool sticky=zero(audio)&&MainComponentTestAccess::completed(*host)==0&&MainComponentTestAccess::silences(*host)==2&&calls.load()==before;
 std::atomic<bool> replaced{false};std::thread control([&]{MainComponentTestAccess::replaceBank(*host,replaced);});
 juce::Thread::sleep(20);const bool retained=!replaced.load();const bool closed=MainComponentTestAccess::callbackAdmissionClosed(*host);
 fill(audio);MainComponentTestAccess::callback(*host,audio,512);const bool continuing=zero(audio)&&MainComponentTestAccess::silences(*host)==3;
 releaseHelpers=true;control.join();MainComponentTestAccess::recover(*host);MainComponentTestAccess::play(*host);
 require(first&&sticky&&retained&&closed&&continuing&&replaced,"fault delivery, epoch retention or control exclusion");
 require(MainComponentTestAccess::faulted(*host),"recovery/rejected Play unexpectedly resumed audio");
 require(ms<100,"forced helper timeout failed bounded-return diagnostic (100ms scheduling tolerance)");
 std::cout<<"PASS second-chunk timeout clears whole callback; delivered=0, fault=1, dropped=3; callback_ms="<<ms<<"; graph retained until ack; closed admission returns callback silence without waiting; rejected Play stays latched\n";
 host.reset();
 auto teardown=std::make_unique<MainComponent>(false);require(MainComponentTestAccess::helpers(*teardown,2),"teardown startup");MainComponentTestAccess::rate(*teardown,96000,256);
 skipCalls=0;calls=0;entered=0;releaseHelpers=false;MainComponentTestAccess::stall(*teardown,holdHelper);
 MainComponentTestAccess::callback(*teardown,audio,256);
 std::thread releaser([]{juce::Thread::sleep(20);releaseHelpers=true;});teardown.reset();releaser.join();
 std::cout<<"PASS teardown waits off callback for late helper acknowledgements before destroying graph\n";return 0;
 }catch(const std::exception& e){releaseHelpers=true;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
