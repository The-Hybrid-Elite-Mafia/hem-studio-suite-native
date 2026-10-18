#include "AnalysisProcessors.h"
#include "AnalysisEditors.h"
#include <cstdlib>
namespace hem {
void LoudnessAnalysisWorker::stop(){signalThreadShouldExit();waitForThreadToExit(-1);}
void LoudnessAnalysisWorker::prepare(double sr){stop();rate=sr;transport.reset();core.prepare(sr);workerEpoch=0;expected=lastPublish=0;producerEpoch=0;producerDropBaseline=0;dropRevision=0;publishedDropEpoch=0;publishedEpochDrops=0;generation.fetch_add(1);{const juce::ScopedLock guard(lock);display={};}startThread();}
bool LoudnessAnalysisWorker::copy(LoudnessDisplay&d)const{const juce::ScopedTryLock guard(lock);if(!guard.isLocked()||display.epoch!=generation.load()||!display.measurement.frameEnd)return false;d=display;return true;}
void LoudnessAnalysisWorker::run(){
    std::uint64_t consecutiveSamples=0,observedDrops=0;
    bool hasDiscontinuity=false;
    while(!threadShouldExit()){
        const bool consumed=transport.consume([&](const auto& p){
            const auto epoch=generation.load();
            if(p.epoch!=epoch)return;
            if(workerEpoch!=epoch){
                core.reset();workerEpoch=epoch;expected=p.first;lastPublish=0;
                observedDrops=0;consecutiveSamples=0;hasDiscontinuity=false;
                const juce::ScopedLock guard(lock);display={};display.epoch=epoch;
            }
            if(p.first!=expected){consecutiveSamples=0;hasDiscontinuity=true;}
            observedDrops=std::max(observedDrops,readEpochDrops(epoch));
            if(p.first!=expected||observedDrops>0)core.discontinuity();
            core.push(p.audio[0].data(),p.audio[1].data(),int(p.count));
            consecutiveSamples+=p.count;expected=p.first+p.count;
            const auto end=core.latest().frameEnd;
            if(end-lastPublish>=std::uint64_t(rate*.1)){
                lastPublish=end;auto f=core.calculate();
                // Display validity only: after a capture gap, wait for complete
                // contiguous windows plus one 100 ms measurement update. Never
                // present a window spanning missing samples as a current M/S value.
                if(hasDiscontinuity){
                    f.momentaryReady=f.momentaryReady&&double(consecutiveSamples)>=rate*.5;
                    f.shortReady=f.shortReady&&double(consecutiveSamples)>=rate*3.1;
                }
                const float unavailable=std::numeric_limits<float>::quiet_NaN();
                const juce::ScopedLock guard(lock);display.measurement=f;
                display.dropped=observedDrops;
                const auto i=display.historyCursor;
                display.momentary[i]=f.momentaryReady?f.momentary:unavailable;
                display.shortTerm[i]=f.shortReady?f.shortTerm:unavailable;
                display.integrated[i]=f.integratedReady?f.integrated:unavailable;
                display.times[i]=double(f.frameEnd)/rate;
                display.historyCursor=(i+1)%600;
                display.historyCount=std::min(600u,display.historyCount+1);
            }
        });
        // A producer may overrun AFTER the final accepted packet, or finish its
        // publication after this consumer drained the queue. Poll epoch-stamped
        // drops even when idle: completeness must never depend on another packet.
        if(workerEpoch==generation.load(std::memory_order_acquire)){
            observedDrops=std::max(observedDrops,readEpochDrops(workerEpoch));
            if(observedDrops>0){
                core.discontinuity();
                const juce::ScopedLock guard(lock);
                display.dropped=observedDrops;display.measurement.complete=false;
                display.measurement.integratedReady=display.measurement.lraReady=false;
                if(display.historyCount)display.integrated[(display.historyCursor+599)%600]=std::numeric_limits<float>::quiet_NaN();
            }
        }
        if(!consumed)wait(3);
    }
}
void SpectrumAnalysisWorker::stop(){signalThreadShouldExit();waitForThreadToExit(-1);}
void SpectrumAnalysisWorker::prepare(double sr){stop();rate=sr;transport.reset();core.prepare(sr);workerEpoch=0;expected=0;lastColumn=0;generation.fetch_add(1);{const juce::ScopedLock guard(lock);display={};}startThread();}
void SpectrumAnalysisWorker::settings(const analysis::SpectrumSettings&s)noexcept{const std::array<float,10>v{float(s.order),float(s.window),float(s.channel),s.averagingMs,s.historySeconds,s.rangeDb,s.rms?1.f:0.f,s.maxHold?1.f:0.f,s.freeze?1.f:0.f,float(s.view)};for(size_t i=0;i<v.size();++i)controls[i].store(v[i],std::memory_order_relaxed);}
bool SpectrumAnalysisWorker::copy(SpectrumDisplay&d)const{const juce::ScopedTryLock guard(lock);if(!guard.isLocked()||display.epoch!=generation.load()||!display.measurement.valid)return false;d=display;return true;}
void SpectrumAnalysisWorker::run(){while(!threadShouldExit()){bool consumed=transport.consume([&](const auto&p){const auto epoch=generation.load();if(p.epoch!=epoch)return;if(workerEpoch!=epoch||p.first!=expected){core.reset();workerEpoch=epoch;timelineOrigin=p.first;lastColumn=0;const juce::ScopedLock guard(lock);display={};display.epoch=epoch;}analysis::SpectrumSettings s;s.order=int(controls[0].load());s.window=int(controls[1].load());s.channel=int(controls[2].load());s.averagingMs=controls[3].load();s.historySeconds=controls[4].load();s.rangeDb=controls[5].load();s.rms=controls[6].load()>.5f;s.maxHold=controls[7].load()>.5f;s.freeze=controls[8].load()>.5f;s.view=int(controls[9].load());core.configure(s);core.push(p.audio[0].data(),p.audio[1].data(),int(p.count),[this](const auto&f){publish(f);});expected=p.first+p.count;});if(!consumed)wait(3);}}
void SpectrumAnalysisWorker::publish(const analysis::SpectrumFrame&incoming){auto f=incoming;f.frameEnd+=timelineOrigin;f.timeSeconds=double(f.frameEnd)/rate;const juce::ScopedLock guard(lock);if(display.measurement.valid&&(display.measurement.fftSize!=f.fftSize||display.measurement.settings.channel!=f.settings.channel||display.measurement.settings.window!=f.settings.window||display.measurement.settings.rms!=f.settings.rms)){display.count=display.cursor=0;lastColumn=0;}display.measurement=f;display.dropped=transport.dropped();if(f.timeSeconds-lastColumn<.099)return;lastColumn=f.timeSeconds;const auto column=display.cursor;for(int row=0;row<SpectrumDisplay::rows;++row){const double hz=20*std::pow(std::max(20.,rate*.5)/20,double(row)/(SpectrumDisplay::rows-1));const int bin=std::clamp(int(std::llround(hz*f.fftSize/rate)),0,f.bins-1);display.spectrogram[column][size_t(row)]=f.db[size_t(bin)];}display.times[column]=f.timeSeconds;display.cursor=(column+1)%SpectrumDisplay::columns;display.count=std::min(unsigned(SpectrumDisplay::columns),display.count+1);}
void DeltaAnalysisWorker::stop(){signalThreadShouldExit();waitForThreadToExit(-1);}
void DeltaAnalysisWorker::prepare(double sr){stop();rate=sr;transport.reset();fill=decimation=0;minimum={};maximum={};expected=0;alignment=0;valid=false;alignRequested=false;serial=0;{const juce::ScopedLock guard(lock);display={};display.sampleRate=sr;}startThread();}
bool DeltaAnalysisWorker::copy(DeltaDisplay&d)const{const juce::ScopedTryLock guard(lock);if(!guard.isLocked()||!display.count)return false;d=display;return true;}
void DeltaAnalysisWorker::run(){while(!threadShouldExit()){const bool consumed=transport.consume([&](const auto&p){if(p.first!=expected){fill=decimation=0;valid=false;const juce::ScopedLock guard(lock);display.count=display.cursor=0;}for(unsigned i=0;i<p.count;++i){dry[size_t(fill)]=p.audio[0][i];processed[size_t(fill)]=p.audio[2][i];dryRight[size_t(fill)]=p.audio[1][i];processedRight[size_t(fill)]=p.audio[3][i];++fill;for(size_t c=0;c<3;++c){const float lo=std::min(p.audio[4+c*2][i],p.audio[5+c*2][i]),hi=std::max(p.audio[4+c*2][i],p.audio[5+c*2][i]);minimum[c]=decimation?std::min(minimum[c],lo):lo;maximum[c]=decimation?std::max(maximum[c],hi):hi;}if(++decimation==64){const juce::ScopedLock guard(lock);for(size_t c=0;c<3;++c){display.minimum[c][display.cursor]=minimum[c];display.maximum[c][display.cursor]=maximum[c];}display.cursor=(display.cursor+1)%DeltaDisplay::columns;display.count=std::min(unsigned(DeltaDisplay::columns),display.count+1);display.frameEnd=p.first+i+1;decimation=0;}if(fill==4096){if(alignRequested.exchange(false)){double leftEnergy=0,rightEnergy=0;for(size_t k=0;k<4096;++k){leftEnergy+=double(dry[k])*dry[k];rightEnergy+=double(dryRight[k])*dryRight[k];}auto result=analysis::DeltaCore::align(rightEnergy>leftEnergy?dryRight.data():dry.data(),rightEnergy>leftEnergy?processedRight.data():processed.data(),4096,int(rate*.003));valid.store(result.valid,std::memory_order_release);if(result.valid)alignment.store(result.lag);const juce::ScopedLock guard(lock);display.alignment=result;serial.fetch_add(1,std::memory_order_release);}fill=0;}}expected=p.first+p.count;});if(!consumed)wait(3);}}
juce::ValueTree AnalysisProcessor::migrateParameterState(const juce::ValueTree&t)const{
// Sessions transport parameters in PARAMETERS; direct plugin state retains its
// distinct APVTS root. Preserve the caller's validated root without relabelling.
const bool sessionTransport=t.hasType("PARAMETERS");
if((!sessionTransport&&!t.hasType(state.state.getType()))||(sessionTransport&&t.getNumProperties()!=0)||t.getNumChildren()!=int(parameterSpecs().size()))return {};std::vector<bool>seen(parameterSpecs().size(),false);for(const auto child:t){if(!child.hasType("PARAM")||child.getNumChildren()!=0||child.getNumProperties()!=2||!child.hasProperty("id")||!child.hasProperty("value"))return {};size_t i=0;while(i<parameterSpecs().size()&&child["id"].toString()!=parameterSpecs()[i].id)++i;if(i==seen.size()||seen[i])return {};seen[i]=true;auto text=child["value"].toString().toStdString();char*end=nullptr;double number=std::strtod(text.c_str(),&end);const auto&s=parameterSpecs()[i];if(end==text.c_str()||*end||!std::isfinite(number)||number<double(s.minimum)-1.e-6||number>double(s.maximum)+1.e-6||(s.step>=1&&number!=std::round(number)))return {};}return t.createCopy();}
void AnalysisProcessor::setStateInformation(const void*data,int bytes){if(!data||bytes<=0||bytes>1024*1024)return;auto xml=getXmlFromBinary(data,bytes);if(!xml)return;auto original=juce::ValueTree::fromXml(*xml);if(!original.hasType(state.state.getType()))return;auto candidate=migrateParameterState(original);if(candidate.isValid())state.replaceState(candidate);}
void AnalysisProcessor::publishPeak(const juce::AudioBuffer<float>&b)noexcept{float p=0;for(int c=0;c<std::min(2,b.getNumChannels());++c)for(int i=0;i<b.getNumSamples();++i){const float v=b.getSample(c,i);if(std::isfinite(v))p=std::max(p,std::abs(v));}peak.store(p,std::memory_order_relaxed);publishMeterBlock(b);}
LoudnessLabProcessor::LoudnessLabProcessor():AnalysisProcessor("LOUDNESS_LAB","Loudness / Metering Lab",{{"meterView","METER VIEW",0,2,0,1,""},{"peakHold","PEAK HOLD",0,1,0,1,"",true},{"pause","PAUSE",0,1,0,1,"",true}}){}
void LoudnessLabProcessor::prepareToPlay(double rate,int){ready=false;resetMeterTelemetry();worker.prepare(rate);actualRate=rate;setLatencySamples(0);publishMeterPreparation();ready=true;}
void LoudnessLabProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){if(!processingReady()||b.getNumChannels()<2||b.getNumSamples()<=0)return;if(value(2)<.5f)worker.push(b.getReadPointer(0),b.getReadPointer(1),b.getNumSamples());publishPeak(b);}
void LoudnessLabProcessor::releaseResources(){ready=false;worker.stop();ModuleProcessor::releaseResources();}
SpectrumSpectrogramProcessor::SpectrumSpectrogramProcessor():AnalysisProcessor("SPECTRUM_SPECTROGRAM","Spectrum / Spectrogram",{{"fftSize","FFT SIZE",0,4,2,1,""},{"windowType","WINDOW TYPE",0,2,1,1,""},{"channel","CHANNEL",0,4,0,1,""},{"averaging","AVERAGING",0,2000,100,1,"ms"},{"history","HISTORY",1,60,10,.1f,"s"},{"displayRange","DISPLAY RANGE",24,160,100,1,"dB"},{"rms","RMS",0,1,0,1,"",true},{"maxHold","MAX HOLD",0,1,0,1,"",true},{"freeze","FREEZE",0,1,0,1,"",true},{"depthView","DEPTH VIEW",0,1,0,1,"",true}}){}
void SpectrumSpectrogramProcessor::prepareToPlay(double rate,int){ready=false;resetMeterTelemetry();worker.prepare(rate);actualRate=rate;setLatencySamples(0);publishMeterPreparation();ready=true;}
void SpectrumSpectrogramProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){if(!processingReady()||b.getNumChannels()<2||b.getNumSamples()<=0)return;analysis::SpectrumSettings s;s.order=int(value(0))+9;s.window=int(value(1));s.channel=int(value(2));s.averagingMs=value(3);s.historySeconds=value(4);s.rangeDb=value(5);s.rms=value(6)>.5f;s.maxHold=value(7)>.5f;s.freeze=value(8)>.5f;s.view=int(value(9));worker.settings(s);worker.push(b.getReadPointer(0),b.getReadPointer(1),b.getNumSamples());publishPeak(b);}
void SpectrumSpectrogramProcessor::releaseResources(){ready=false;worker.stop();ModuleProcessor::releaseResources();}
DeltaMonitorProcessor::DeltaMonitorProcessor():AnalysisProcessor("DELTA_MONITOR","Delta Monitor",{{"monitorGain","MONITOR GAIN",-24,24,0,.1f,"dB"},{"soloMode","SOLO",0,2,0,1,""},{"delayTrim","DELAY TRIM",-3,3,0,.01f,"ms"},{"drySource","DRY SOURCE",0,1,1,1,""}}){}
void DeltaMonitorProcessor::prepareToPlay(double rate,int block){ready=false;resetMeterTelemetry();if(!std::isfinite(rate)||rate<8000||rate>384000)throw std::invalid_argument("Delta sample rate must be 8–384 kHz");blockLimit=std::max(1,block);for(auto&v:scratch)v.assign(size_t(blockLimit),0);core.prepare(rate);worker.prepare(rate);reference={};haveTimeline=false;awaitingAlignment=false;requestedSerial=0;publishDelta({});actualRate=rate;setLatencySamples(core.latency());publishMeterPreparation();ready=true;}
void DeltaMonitorProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){const auto incoming=reference;reference={};if(!processingReady()||b.getNumChannels()<2||b.getNumSamples()<=0)return;const int total=b.getNumSamples();bool valid=incoming.valid&&incoming.left&&incoming.right&&incoming.samples==total&&incoming.sampleRate==actualRate.load()&&value(3)>.5f;if(valid){if(haveTimeline&&incoming.timeline!=expectedTimeline)valid=false;expectedTimeline=incoming.timeline+std::uint64_t(total);haveTimeline=true;}else haveTimeline=false;analysis::DeltaSettings s;s.mode=int(value(1));s.monitorGainDb=value(0);s.delayTrimMs=value(2);
 analysis::DeltaFrame aggregate;for(int offset=0;offset<total;offset+=blockLimit){const int n=std::min(blockLimit,total-offset);analysis::ReferenceBlock ref{valid?incoming.left+offset:nullptr,valid?incoming.right+offset:nullptr,n,actualRate.load(),incoming.timeline+std::uint64_t(offset),valid};int i=0;core.process(b.getWritePointer(0)+offset,b.getWritePointer(1)+offset,n,ref,s,[&](const auto&rawDry,const auto&rawProcessed,const auto&dry,const auto&processed,const auto&delta){const std::array<std::array<float,2>,5>values{rawDry,rawProcessed,dry,processed,delta};for(size_t c=0;c<10;++c)scratch[c][size_t(i)]=values[c/2][c%2];++i;});if(valid){std::array<const float*,10>p{};for(size_t c=0;c<10;++c)p[c]=scratch[c].data();worker.push(p,n);}else worker.skip(n);const auto f=core.latest();for(size_t c=0;c<2;++c){aggregate.dryPeak[c]=std::max(aggregate.dryPeak[c],f.dryPeak[c]);aggregate.processedPeak[c]=std::max(aggregate.processedPeak[c],f.processedPeak[c]);aggregate.deltaPeak[c]=std::max(aggregate.deltaPeak[c],f.deltaPeak[c]);}aggregate.referenceValid=f.referenceValid;aggregate.correlationValid=f.correlationValid;aggregate.correlation=f.correlation;aggregate.alignmentSamples=f.alignmentSamples;aggregate.frameEnd=f.frameEnd;}publishDelta(aggregate);publishPeak(b);}
void DeltaMonitorProcessor::publishDelta(const analysis::DeltaFrame&f)noexcept{revision.fetch_add(1);for(size_t c=0;c<2;++c){published[c].store(f.dryPeak[c]);published[2+c].store(f.processedPeak[c]);published[4+c].store(f.deltaPeak[c]);}published[6].store(f.correlation);flags.store((f.referenceValid?1u:0u)|(f.correlationValid?2u:0u));alignment.store(f.alignmentSamples);frameEnd.store(f.frameEnd);revision.fetch_add(1);}
analysis::DeltaFrame DeltaMonitorProcessor::deltaSnapshot()const noexcept{for(int attempt=0;attempt<3;++attempt){auto before=revision.load();if(before&1u)continue;analysis::DeltaFrame f;for(size_t c=0;c<2;++c){f.dryPeak[c]=published[c].load();f.processedPeak[c]=published[2+c].load();f.deltaPeak[c]=published[4+c].load();}f.correlation=published[6].load();auto flagsValue=flags.load();f.referenceValid=flagsValue&1;f.correlationValid=flagsValue&2;f.alignmentSamples=alignment.load();f.frameEnd=frameEnd.load();if(before==revision.load())return f;}return {};}
bool DeltaMonitorProcessor::applyPendingAlignment(){if(!awaitingAlignment||worker.resultSerial()<requestedSerial)return false;awaitingAlignment=false;if(!worker.alignmentValid()||!deltaSnapshot().referenceValid)return false;if(auto*p=state.getParameter("delayTrim")){const float trim=std::clamp(float(worker.alignmentSamples()/actualRate.load()*1000),-3.f,3.f);p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(trim));p->endChangeGesture();return true;}return false;}
void DeltaMonitorProcessor::releaseResources(){ready=false;reference={};worker.stop();ModuleProcessor::releaseResources();}
juce::AudioProcessorEditor*LoudnessLabProcessor::createEditor(){return new AnalysisEditor(*this,29);}juce::AudioProcessorEditor*SpectrumSpectrogramProcessor::createEditor(){return new AnalysisEditor(*this,30);}juce::AudioProcessorEditor*DeltaMonitorProcessor::createEditor(){return new AnalysisEditor(*this,31);}
} // namespace hem
