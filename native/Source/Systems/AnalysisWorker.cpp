#include "AnalysisWorker.h"
namespace hem {
AnalysisWorker::AnalysisWorker():juce::Thread("HEM spectrum and waveform"){
    for(int i=0;i<fftSize;++i){window[size_t(i)]=float(.5-.5*std::cos(juce::MathConstants<double>::twoPi*i/(fftSize-1)));windowSum+=window[size_t(i)];}
    latest.leftDb.fill(-160);latest.rightDb.fill(-160);startThread();
}
AnalysisWorker::~AnalysisWorker(){signalThreadShouldExit();notify();waitForThreadToExit(-1);}
void AnalysisWorker::prepare(double rate) noexcept {producerRate=rate;producerFrame=0;++producerEpoch;}
void AnalysisWorker::push(const float* l,const float* r,int count) noexcept {
    for(int offset=0;offset<count;offset+=packetSize){const int n=std::min(packetSize,count-offset);int a,n1,b,n2;fifo.prepareToWrite(1,a,n1,b,n2);
        if(n1+n2==0){drops.fetch_add(unsigned(n),std::memory_order_relaxed);producerFrame+=unsigned(n);continue;}
        auto& p=packets[size_t(n1?a:b)];p.size=n;p.rate=producerRate;p.epoch=producerEpoch;p.first=producerFrame;
        std::copy_n(l+offset,n,p.left.data());std::copy_n(r+offset,n,p.right.data());producerFrame+=unsigned(n);fifo.finishedWrite(1);
    }
}
bool AnalysisWorker::copyLatest(Snapshot& output) const {
    const juce::ScopedTryLock lock(snapshotLock);if(!lock.isLocked())return false;output=latest;return true;
}
void AnalysisWorker::resetHistory(){ringL.fill(0);ringR.fill(0);filled=write=hop=0;peakCount=peakWrite=bucketFill=0;accumulator={};}
void AnalysisWorker::consume(const Packet& p){
    if(p.epoch!=workerEpoch||p.first!=expectedFrame){resetHistory();gap=true;workerEpoch=p.epoch;}
    workerRate=p.rate;
    for(int i=0;i<p.size;++i){const float l=std::isfinite(p.left[size_t(i)])?p.left[size_t(i)]:0.f,r=std::isfinite(p.right[size_t(i)])?p.right[size_t(i)]:0.f;
        ringL[size_t(write)]=l;ringR[size_t(write)]=r;write=(write+1)%fftSize;filled=std::min(fftSize,filled+1);
        if(bucketFill==0)accumulator={l,l,r,r};
        else {accumulator.lowL=std::min(accumulator.lowL,l);accumulator.highL=std::max(accumulator.highL,l);accumulator.lowR=std::min(accumulator.lowR,r);accumulator.highR=std::max(accumulator.highR,r);}
        if(++bucketFill==bucketSize){peaks[size_t(peakWrite)]=accumulator;peakWrite=(peakWrite+1)%columns;peakCount=std::min(columns,peakCount+1);bucketFill=0;}
        expectedFrame=p.first+unsigned(i)+1;
        if(++hop>=fftSize/4&&filled==fftSize){hop=0;publish();}
    }
    expectedFrame=p.first+unsigned(p.size);
}
void AnalysisWorker::publish(){
    fftL.fill(0);fftR.fill(0);
    for(int i=0;i<fftSize;++i){const auto ix=size_t((write+i)%fftSize);fftL[size_t(i)]=ringL[ix]*window[size_t(i)];fftR[size_t(i)]=ringR[ix]*window[size_t(i)];}
    fft.performFrequencyOnlyForwardTransform(fftL.data(),true);fft.performFrequencyOnlyForwardTransform(fftR.data(),true);
    for(int i=0;i<bins;++i){const double scale=(i==0||i==bins-1?1.:2.)/windowSum;
        work.leftDb[size_t(i)]=float(20*std::log10(std::max(1.e-8,double(fftL[size_t(i)])*scale)));
        work.rightDb[size_t(i)]=float(20*std::log10(std::max(1.e-8,double(fftR[size_t(i)])*scale)));}
    work.validColumns=peakCount;for(int i=0;i<peakCount;++i)work.waveform[size_t(i)]=peaks[size_t((peakWrite-peakCount+i+columns)%columns)];
    work.sampleRate=workerRate;work.frameEnd=expectedFrame;work.sequence=++sequence;work.droppedFrames=drops.load();work.discontinuity=gap;gap=false;
    const juce::ScopedLock lock(snapshotLock);latest=work;
}
void AnalysisWorker::run(){
    while(!threadShouldExit()){
        int a,n1,b,n2;fifo.prepareToRead(1,a,n1,b,n2);
        if(n1+n2==0){wait(3);continue;}
        consume(packets[size_t(n1?a:b)]);fifo.finishedRead(1);
    }
}
}
