#include "Engine.h"
#if JUCE_INTEL
#include <emmintrin.h>
#endif
namespace hem {
namespace {
float approach(float x,float target,float step) noexcept{return x+std::clamp(target-x,-step,step);}
float clean(float x)noexcept{return std::isfinite(x)?x:0;}
float normalizedTrimDb(float db) noexcept{
 db=std::clamp(clean(db),-24.f,24.f);
 return std::round(db*10.f)*.1f;
}
float amplitude(float db){return std::pow(10.0f,db*.05f);}
void copy(juce::AudioBuffer<float>& dst,const juce::AudioBuffer<float>& src,int n) noexcept{for(int c=0;c<dst.getNumChannels();++c)dst.copyFrom(c,0,src,c,0,n);}
}
ClipReport OfflineWorkerUtility::condition(Clip& clip,float headroomDb,float attenuationDb){
 auto& b=clip.audio;ClipReport r;int n=b.getNumSamples(),ch=b.getNumChannels();
 if(n<=0||ch<=0)throw std::invalid_argument("Empty clip");
 if(!std::isfinite(headroomDb)||headroomDb>0||!std::isfinite(attenuationDb)||attenuationDb> -3||attenuationDb< -6)throw std::invalid_argument("Invalid headroom/attenuation");
 r.mean.resize(size_t(ch));double energy=0;uint64_t nearFull=0;
 for(int c=0;c<ch;++c){double sum=0;auto* x=b.getWritePointer(c);for(int i=0;i<n;++i){if(!std::isfinite(x[i]))throw std::invalid_argument("Nonfinite clip");sum+=x[i];}r.mean[size_t(c)]=sum/n;for(int i=0;i<n;++i){x[i]-=float(r.mean[size_t(c)]);float a=std::abs(x[i]);r.peak=std::max(r.peak,a);energy+=double(x[i])*x[i];nearFull+=a>=.999f;}}
 r.rms=float(std::sqrt(energy/(double(n)*ch)));r.crestDb=r.rms>0?20*std::log10(r.peak/r.rms):0;
 r.suspectedClipping=nearFull>0;r.trimDb=(r.suspectedClipping||r.peak>amplitude(headroomDb))?attenuationDb:0;
 b.applyGain(amplitude(r.trimDb));return r;
}
std::unique_ptr<Clip> OfflineWorkerUtility::loadWav(const juce::File& file,double targetRate){
 if(!(targetRate>0)||!std::isfinite(targetRate)||!file.hasFileExtension("wav"))throw std::invalid_argument("WAV/rate required");
 juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(file.createInputStream().release(),true));
 if(!reader||reader->lengthInSamples<=0||reader->lengthInSamples>INT_MAX||reader->numChannels<1||reader->numChannels>2||!(reader->sampleRate>0))throw std::runtime_error("Unsupported or unreadable WAV");
 juce::AudioBuffer<float> input(int(reader->numChannels),int(reader->lengthInSamples));if(!reader->read(&input,0,input.getNumSamples(),0,true,true))throw std::runtime_error("WAV decode failed");
 double ratio=reader->sampleRate/targetRate;double count=std::ceil(input.getNumSamples()/ratio);if(count>INT_MAX)throw std::length_error("Clip exceeds AudioBuffer sample indexing; use segmented streaming");
 auto result=std::make_unique<Clip>();result->sampleRate=targetRate;result->audio.setSize(input.getNumChannels(),int(count));
 // Offline windowed-sinc resampler, 64 taps, anti-alias cutoff reduced for downsampling.
 double cutoff=.94*std::min(1.0,1.0/ratio);
 for(int c=0;c<input.getNumChannels();++c)for(int i=0;i<int(count);++i){double t=i*ratio,sum=0,norm=0;int centre=int(std::floor(t));for(int k=-31;k<=32;++k){int j=centre+k;double d=t-j;if(std::abs(d)>32)continue;double v=cutoff*d;double sinc=std::abs(v)<1e-12?1:std::sin(juce::MathConstants<double>::pi*v)/(juce::MathConstants<double>::pi*v);double window=.5+.5*std::cos(juce::MathConstants<double>::pi*d/32);double w=cutoff*sinc*window;norm+=w;if(j>=0&&j<input.getNumSamples())sum+=w*input.getSample(c,j);}result->audio.setSample(c,i,float(sum/norm));}
 return result;
}
void ClipPlayback::render(juce::AudioBuffer<float>& out,int n) noexcept{
 if(!previous&&publication.canRetire())if(auto* next=publication.take()){previous=active;active=next;fade=0;}
 for(int i=0;i<n;++i){float t=previous?std::min(1.f,float(++fade)/fadeLength):1;
  for(int c=0;c<out.getNumChannels();++c){auto sample=[&](Clip* p){return p&&position<p->audio.getNumSamples()?p->audio.getSample(std::min(c,p->audio.getNumChannels()-1),int(position)):0.f;};out.setSample(c,i,(1-t)*sample(previous)+t*sample(active));}++position;
  if(previous&&fade>=fadeLength){publication.retire(previous);previous=nullptr;}}
}
std::unique_ptr<CurveTable> VectorizedDynamics::makeCurve(const std::vector<std::pair<float,float>>& points){
 if(points.size()<2||points.front().first!= -60||points.back().first!=0)throw std::invalid_argument("Curve must cover -60 to 0 dBFS");
 for(size_t i=0;i<points.size();++i)if(!std::isfinite(points[i].first)||!std::isfinite(points[i].second)||points[i].second>points[i].first||points[i].second< -120||(i&&points[i].first<=points[i-1].first))throw std::invalid_argument("Invalid downward dynamics points");
 std::vector<float> slope(points.size());for(size_t i=0;i<points.size();++i){if(i==0)slope[i]=(points[1].second-points[0].second)/(points[1].first-points[0].first);else if(i+1==points.size())slope[i]=(points[i].second-points[i-1].second)/(points[i].first-points[i-1].first);else{float a=(points[i].second-points[i-1].second)/(points[i].first-points[i-1].first),b=(points[i+1].second-points[i].second)/(points[i+1].first-points[i].first);slope[i]=a*b<=0?0:2*a*b/(a+b);}}
 auto table=std::make_unique<CurveTable>();size_t segment=0;
 // Linear amplitude indexing avoids log() in the callback; curve itself is defined in dB.
 for(size_t i=0;i<1024;++i){float a=std::max(.001f,float(i)/1023),x=20*std::log10(a);while(segment+2<points.size()&&x>points[segment+1].first)++segment;auto [x0,y0]=points[segment];auto [x1,y1]=points[segment+1];float h=x1-x0,t=std::clamp((x-x0)/h,0.f,1.f);float y=(2*t*t*t-3*t*t+1)*y0+(t*t*t-2*t*t+t)*h*slope[segment]+(-2*t*t*t+3*t*t)*y1+(t*t*t-t*t)*h*slope[segment+1];table->gain[i]=amplitude(std::min(0.f,y-x));}
 return table;
}
void VectorizedDynamics::configure(const std::vector<std::pair<float,float>>& points,float a,float r,bool mode){
 if(!(a>0&&r>0)||!std::isfinite(a)||!std::isfinite(r))throw std::invalid_argument("Invalid detector times");
 auto table=makeCurve(points);cached=juce::ValueTree("Dynamics");cached.setProperty("attackMs",a,nullptr);cached.setProperty("releaseMs",r,nullptr);cached.setProperty("rms",mode,nullptr);for(auto [x,y]:points){juce::ValueTree p("Point");p.setProperty("x",x,nullptr);p.setProperty("y",y,nullptr);cached.addChild(p,-1,nullptr);}rms=mode;attack=std::exp(-1.f/float(sr*a*.001));release=std::exp(-1.f/float(sr*r*.001));tables.publish(std::move(table));
}
void VectorizedDynamics::prepare(double rate,int,int){sr=rate;env=0;std::vector<std::pair<float,float>> points;for(int i=0;i<cached.getNumChildren();++i){auto p=cached.getChild(i);points.emplace_back(float(p["x"]),float(p["y"]));}if(points.empty())points={{-60,-60},{0,0}};configure(points,float(cached.getProperty("attackMs",10)),float(cached.getProperty("releaseMs",100)),bool(cached.getProperty("rms",false)));}
void VectorizedDynamics::process(juce::AudioBuffer<float>& buffer,int numSamples) noexcept{
 juce::ScopedNoDenormals guard;int channels=buffer.getNumChannels();if(numSamples<=0||channels<=0)return;if(numSamples>buffer.getNumSamples()){buffer.clear();return;}
 auto* table=tables.acquire();if(!table)return;constexpr int chunkSize=128;
 alignas(16) std::array<float,chunkSize> detector{},channelValues{},gainValues{};
 for(int offset=0;offset<numSamples;offset+=chunkSize){int count=std::min(chunkSize,numSamples-offset);juce::FloatVectorOperations::clear(detector.data(),count);
  for(int c=0;c<channels;++c){float* input=buffer.getWritePointer(c,offset);for(int i=0;i<count;++i)input[i]=clean(input[i]);
   if(rms){juce::FloatVectorOperations::multiply(channelValues.data(),input,input,count);juce::FloatVectorOperations::add(detector.data(),channelValues.data(),count);}
   else{juce::FloatVectorOperations::abs(channelValues.data(),input,count);juce::FloatVectorOperations::max(detector.data(),detector.data(),channelValues.data(),count);}}
  if(rms)juce::FloatVectorOperations::multiply(detector.data(),1.f/channels,count);
  // Recursive envelope and indexed LUT lookup remain sequential: JUCE FVO
  // has no recursive scan/gather primitive. Detector prep/application are SIMD-capable.
  for(int i=0;i<count;++i){float target=detector[size_t(i)],coefficient=target>env?attack:release;env=coefficient*env+(1-coefficient)*target;
   float envelope=rms?std::sqrt(std::max(0.f,env)):env;float index=std::clamp(envelope*1023,0.f,1023.f);int lower=int(index),upper=std::min(lower+1,1023);float fraction=index-lower;
   float a=table->gain[size_t(lower)],b=table->gain[size_t(upper)];gainValues[size_t(i)]=a+fraction*(b-a);}
  for(int c=0;c<channels;++c)juce::FloatVectorOperations::multiply(buffer.getWritePointer(c,offset),gainValues.data(),count);
 }
}
void ModularInsertSlot::Pair::prepare(double rate,int block,int channels,int budget){
 for(size_t k=0;k<2;++k){int latency=0;if(processor[k]){processor[k]->prepare(rate,block,channels);latency=processor[k]->latency();if(latency<0||latency>budget)throw std::invalid_argument("Processor exceeds slot latency budget");auto s=processor[k]->state();if(k==0)State_A=s;else State_B=s;}align[k].prepare(channels,budget-latency);}
}
void ModularInsertSlot::prepare(double rate,int size,int ch,int latencyBudget){if(size<1||ch<1||latencyBudget<0)throw std::invalid_argument("Slot preparation");sampleRate=rate;budget=latencyBudget;block=size;channels=ch;step=1.f/float(std::max(1,int(std::lround(rate*.01))));for(auto* b:{&dryDelayBuffer,&wetA,&wetB,&oldWet}){b->setSize(ch,size);b->clear();}dryDelay.prepare(ch,budget);ab=bypassMix=deltaMix=0;auto pair=std::make_unique<Pair>();pair->prepare(rate,size,ch,budget);pairs.publish(std::move(pair));}
void ModularInsertSlot::install(std::unique_ptr<Pair> pair,double rate){if(!pair)throw std::invalid_argument("Null pair");pair->prepare(rate,block,channels,budget);pairs.publish(std::move(pair));}
void ModularInsertSlot::process(juce::AudioBuffer<float>& b,int n) noexcept{
 if(!previous&&pairs.canRetire())if(auto* p=pairs.take()){previous=active;active=p;replacement=0;warm=budget;}
 copy(dryDelayBuffer,b,n);dryDelay.process(dryDelayBuffer,n);
 auto render=[&](Pair* p){copy(wetA,b,n);copy(wetB,b,n);if(p)for(size_t k=0;k<2;++k){auto& w=k?wetB:wetA;if(p->processor[k])p->processor[k]->process(w,n);p->align[k].process(w,n);}};
 // Preserve per-sample A/B ramp for both paths through a replacement.
 float startAB=ab;if(previous){render(previous);float t=startAB;for(int i=0;i<n;++i){t=approach(t,selectB.load(std::memory_order_relaxed)?1.f:0.f,step);for(int c=0;c<channels;++c)oldWet.setSample(c,i,(1-t)*wetA.getSample(c,i)+t*wetB.getSample(c,i));}}
 render(active);bool targetB=selectB.load(std::memory_order_relaxed),targetBypass=bypass.load(std::memory_order_relaxed),targetDelta=delta.load(std::memory_order_relaxed);
 for(int i=0;i<n;++i){ab=approach(ab,targetB?1.f:0.f,step);bypassMix=approach(bypassMix,targetBypass?1.f:0.f,step);deltaMix=approach(deltaMix,targetDelta?1.f:0.f,step);float replace=1;if(previous){if(warm>0){--warm;replace=0;}else replace=std::min(1.f,float(++replacement)*step);}
 for(int c=0;c<channels;++c){float wet=(1-ab)*wetA.getSample(c,i)+ab*wetB.getSample(c,i);if(previous)wet=(1-replace)*oldWet.getSample(c,i)+replace*wet;float dry=dryDelayBuffer.getSample(c,i);float audited=(1-deltaMix)*wet+deltaMix*(dry-wet);b.setSample(c,i,(1-bypassMix)*audited+bypassMix*dry);}
 // Retirement occurs after the block so oldWet remains valid for its full block.
 }
 if(previous&&warm==0&&float(replacement)*step>=1){pairs.retire(previous);previous=nullptr;}
}
void MasterTruePeakLimiter::prepare(double rate,int block,int channels){
 if(!std::isfinite(rate)||rate<=0||rate*.004>double(std::numeric_limits<int>::max()/4)-.5||block<1||block>std::numeric_limits<int>::max()/4||channels<1||channels==std::numeric_limits<int>::max())throw std::invalid_argument("Invalid limiter preparation");
 maxBlock=block;delay=4*std::max(1,int(std::lround(rate*.004)));
 const int attackSamples=std::clamp(int(std::lround(double(delay)*.125)),1,delay);
 attackShape.resize(size_t(attackSamples)+1);
 for(int k=0;k<=attackSamples;++k){const float t=float(k)/attackSamples;attackShape[size_t(k)]=t*t*(3-2*t);}
 ring.setSize(channels+1,delay);ring.clear();juce::FloatVectorOperations::fill(ring.getWritePointer(channels),1.f,delay);
 pos=0;gain=1;ceiling=std::pow(10.f,-1.f/20.f);counter=head=tail=0;deque.clear();
 releaseFast=std::exp(-1/(rate*4*.080));releaseSlow=std::exp(-1/(rate*4*.300));
 oversampler=std::make_unique<juce::dsp::Oversampling<float>>(size_t(channels),2,juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,true,true);
 oversampler->initProcessing(size_t(block));oversampler->reset();
 guardAudio.setSize(channels,guardTaps*2);guardAudio.clear();guardBudgets.fill(1);
 guardPosition=0;guardGain=1;guardRelease=std::exp(-1/(rate*.080));
 // Normalized signed reconstruction kernels for two finite interpolation domains.
 // Short and long kernels catch different intersample excursions. The detector
 // schedules linked attenuation over pending samples; release is 80 ms.
 constexpr double guardPi=3.14159265358979323846;
 for(int phase=0;phase<48;++phase){std::array<double,guardTaps> coefficients{};double sum=0;const int taps=phase>=32?128:256;const double half=taps/2.;const double fraction=phase>=32?double(phase-32)/16:double(phase)/32;
  for(int k=0;k<taps;++k){const double x=fraction-(k-(taps/2-1));
   const double window=std::abs(x)<half?.42+.5*std::cos(guardPi*x/half)+.08*std::cos(2*guardPi*x/half):0;
   coefficients[size_t(k)]=(std::abs(x)<1.e-14?1:std::sin(guardPi*x)/(guardPi*x))*window;sum+=coefficients[size_t(k)];}
  for(int k=0;k<guardTaps;++k)guardCoefficients[size_t(phase)][size_t(k)]=float(coefficients[size_t(k)]/sum);}
}
void MasterTruePeakLimiter::process(juce::AudioBuffer<float>& buffer,int numSamples) noexcept{
 juce::ScopedNoDenormals guard;const int channels=buffer.getNumChannels();
 if(!oversampler||numSamples<0||numSamples>maxBlock||numSamples>buffer.getNumSamples()||channels!=ring.getNumChannels()-1){buffer.clear();return;}if(numSamples==0)return;
 // Prevent a single invalid input from contaminating interpolation FIR history.
 for(int c=0;c<channels;++c){auto* input=buffer.getWritePointer(c);for(int i=0;i<numSamples;++i)input[i]=clean(input[i]);}
 juce::dsp::AudioBlock<float> whole(buffer);auto block=whole.getSubBlock(0,size_t(numSamples));auto up=oversampler->processSamplesUp(block);
 float* scheduledGain=ring.getWritePointer(channels);const int attackSamples=std::clamp(int(std::lround(double(delay)*.125)),1,delay);
 for(size_t i=0;i<up.getNumSamples();++i){float peak=0;
  for(int c=0;c<channels;++c){float x=clean(up.getSample(c,int(i)));up.setSample(c,int(i),x);peak=std::max(peak,std::abs(x));}
  const double coefficient=gain<.5?releaseSlow:releaseFast;const double released=1-coefficient*(1-gain);gain=std::min(released,double(scheduledGain[pos]));
  for(int c=0;c<channels;++c){float* audioDelay=ring.getWritePointer(c);float delayed=audioDelay[pos];audioDelay[pos]=up.getSample(c,int(i));up.setSample(c,int(i),delayed*gain);}
  scheduledGain[pos]=1;
  if(peak>ceiling){float target=ceiling/peak;int j=(pos+delay-attackSamples)%delay;
   // Schedule cubic smoothstep BEFORE the delayed peak; overlap uses minimum.
   // Bounded O(attackSamples) overload cost requires real-time benchmarking.
   for(int k=0;k<=attackSamples;++k){float constraint=1+(target-1)*attackShape[size_t(k)];scheduledGain[j]=std::min(scheduledGain[j],constraint);if(++j==delay)j=0;}}
  if(++pos==delay)pos=0;
 }
 oversampler->processSamplesDown(block);
 // Final SAMPLE ceiling only: not certified true-peak/codec protection.
 for(int c=0;c<channels;++c){auto* output=buffer.getWritePointer(c);for(int i=0;i<numSamples;++i)output[i]=clean(output[i]);juce::FloatVectorOperations::clip(output,output,-ceiling,ceiling,numSamples);}
 protectReconstruction(buffer,numSamples);
}
void MasterTruePeakLimiter::protectReconstruction(juce::AudioBuffer<float>& buffer,int frames) noexcept {
 const int channels=buffer.getNumChannels();const double limit=double(ceiling)*.999999;
 // A whole-call L-infinity/L1 bound can prove that every reconstruction
 // estimate will be below the limit. Include all old history and all incoming
 // samples: every future FIR window in this call is a subset of that union.
 // 0.1% margin exceeds the rounding error of the float SIMD dot products.
 // This only elides a provably inactive detector; old attenuation schedules,
 // release, ring writes and fixed latency still execute on every sample.
 double maximum=0,maxKernelNorm=0;
 for(int ch=0;ch<channels;++ch){
  for(int i=0;i<guardTaps*2;++i)maximum=std::max(maximum,std::abs(double(guardAudio.getSample(ch,i))));
  for(int i=0;i<frames;++i)maximum=std::max(maximum,std::abs(double(buffer.getSample(ch,i))));
 }
 for(const auto& coefficients:guardCoefficients){double norm=0;for(float coefficient:coefficients)norm+=std::abs(double(coefficient));maxKernelNorm=std::max(maxKernelNorm,norm);}
 const bool detectorInactive=maximum*maxKernelNorm*1.001<limit;
 for(int n=0;n<frames;++n){
  for(int ch=0;ch<channels;++ch){const auto value=buffer.getSample(ch,n);guardAudio.setSample(ch,guardPosition,value);guardAudio.setSample(ch,guardPosition+guardTaps,value);}
  guardBudgets[size_t(guardPosition)]=1;double bound=0;
    if(!detectorInactive)for(int ch=0;ch<channels;++ch){for(int phase=0;phase<48;++phase){const auto& coefficients=guardCoefficients[size_t(phase)];const auto* history=guardAudio.getReadPointer(ch)+guardPosition+1+(phase>=32?64:0);const int taps=phase>=32?128:256;double sum=0;
#if JUCE_INTEL
   __m128 a=_mm_setzero_ps(),b=a,c=a,d=a;
   for(int k=0;k<taps;k+=16){a=_mm_add_ps(a,_mm_mul_ps(_mm_loadu_ps(history+k),_mm_loadu_ps(coefficients.data()+k)));
    b=_mm_add_ps(b,_mm_mul_ps(_mm_loadu_ps(history+k+4),_mm_loadu_ps(coefficients.data()+k+4)));
    c=_mm_add_ps(c,_mm_mul_ps(_mm_loadu_ps(history+k+8),_mm_loadu_ps(coefficients.data()+k+8)));
    d=_mm_add_ps(d,_mm_mul_ps(_mm_loadu_ps(history+k+12),_mm_loadu_ps(coefficients.data()+k+12)));}
   float lanes[4];_mm_storeu_ps(lanes,_mm_add_ps(_mm_add_ps(a,b),_mm_add_ps(c,d)));for(float lane:lanes)sum+=lane;
#else
   for(int k=0;k<taps;++k)sum+=double(coefficients[size_t(k)])*history[k];
#endif
   bound=std::max(bound,std::abs(sum));}}
  const double budget=bound>limit?limit/bound:1;
  for(auto& value:guardBudgets)value=std::min(value,budget);
  const int read=(guardPosition+1)%guardTaps;
  guardGain=std::min(guardBudgets[size_t(read)],1-guardRelease*(1-guardGain));
  for(int ch=0;ch<channels;++ch)buffer.setSample(ch,n,float(double(guardAudio.getSample(ch,read))*guardGain));
  guardPosition=read;
 }
}
bool StemIngestManager::setInputTrimDb(size_t strip,float db) noexcept{
 if(strip>=trimDb.size())return false;
 trimDb[strip].store(normalizedTrimDb(db),std::memory_order_relaxed);return true;
}
StemIngestManager::StemIngestManager(){for(size_t s=0;s<13;++s){trimDb[s].store(0.f);gainDb[s].store(0);highPass[s].store(false);mute[s].store(false);solo[s].store(false);hardwareChannel[s]=int(s);}}
void StemIngestManager::prepare(double rate,int block){
 if(rate<=0||block<1)throw std::invalid_argument("Ingest preparation");maxBlock=block;step=1.f/float(std::max(1,int(rate*.01)));double k=std::tan(juce::MathConstants<double>::pi*30/rate),norm=1/(1+std::sqrt(2.)*k+k*k);
 for(size_t s=0;s<13;++s){scratch[s].setSize(2,block);scratch[s].clear();trim[s].reset(rate,.010);trim[s].setCurrentAndTargetValue(amplitude(normalizedTrimDb(trimDb[s].load())));mix[s]=1;hpMix[s]=0;for(auto& f:hp[s]){f={};f.b0=norm;f.b1=-2*norm;f.b2=norm;f.a1=2*(k*k-1)*norm;f.a2=(1-std::sqrt(2.)*k+k*k)*norm;}for(auto& slot:slots[s])slot.prepare(rate,block,2,512);}limiter.prepare(rate,block,2);
}
void StemIngestManager::processBlock(const juce::AudioBuffer<float>& in,juce::AudioBuffer<float>& out,int n) noexcept{
 juce::ScopedNoDenormals guard;if(n<0||n>maxBlock||out.getNumChannels()!=2||out.getNumSamples()<n){out.clear();return;}
 // Separate input and output buffers required: all physical inputs remain intact.
 out.clear();bool anySolo=false;for(auto& flag:solo)anySolo|=flag.load(std::memory_order_relaxed);
 for(size_t s=0;s<13;++s){auto& b=scratch[s];int channel=hardwareChannel[s];float requestedTrim=amplitude(normalizedTrimDb(trimDb[s].load(std::memory_order_relaxed)));float requestedGain=amplitude(std::clamp(clean(gainDb[s].load()),-60.f,12.f));bool enabled=!mute[s].load()&&(!anySolo||solo[s].load());float targetHP=highPass[s].load()?1.f:0.f;trim[s].setTargetValue(requestedTrim);
 for(int i=0;i<n;++i){const float inputTrim=trim[s].getNextValue();hpMix[s]=approach(hpMix[s],targetHP,step);float x=channel>=0&&channel<in.getNumChannels()?clean(in.getSample(channel,i))*inputTrim:0;for(int c=0;c<2;++c){float filtered=hp[s][size_t(c)].tick(x);b.setSample(c,i,x+hpMix[s]*(filtered-x));}}
 for(auto& slot:slots[s])slot.process(b,n);
 for(int i=0;i<n;++i){mix[s]=approach(mix[s],enabled?requestedGain:0.f,step);for(int c=0;c<2;++c)out.addSample(c,i,b.getSample(c,i)*mix[s]*(1.f/13.f));}
 }
 limiter.process(out,n);
}
}





