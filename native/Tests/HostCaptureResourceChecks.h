#pragma once
// Deterministic loader pause uses the existing dedicated convolution-test friend.
// It never changes production loading/capture code or opens a native chooser.
namespace hem {
struct ConvolutionReverbTestAccess {
 static void stop(ConvolutionReverbProcessor& p){p.signalThreadShouldExit();p.notify();p.stopThread(-1);}
 static std::uint64_t generation(ConvolutionReverbProcessor& p){const std::lock_guard<std::mutex> lock(p.resourceMutex);return p.requestGeneration;}
};
}
namespace hostCaptureChecks {
inline int index(const char* id){for(size_t n=0;n<hem::PluginRegistry::entries().size();++n)if(juce::String(hem::PluginRegistry::entries()[n].id)==id)return int(n);throw std::runtime_error("Required host resource/tempo processor missing");}
inline hem::ConvolutionReverbProcessor& convolution(MainComponent& host){auto* p=dynamic_cast<hem::ConvolutionReverbProcessor*>(MainComponentTestAccess::bank(host).tracks[0].chain.activeProcessor(0));require(p!=nullptr,"Actual host convolution missing");return *p;}
inline void writeIR(const juce::File& file,float left=.5f,float right=.25f){
 require(!file.exists()||file.deleteFile(),"Could not replace owned temporary IR");
 std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();juce::WavAudioFormat format;
 auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
 require(writer!=nullptr,"IR writer failed");juce::AudioBuffer<float> data(2,1024);data.clear();data.setSample(0,0,left);data.setSample(1,0,right);data.setSample(0,157,-.125f);data.setSample(1,251,.0625f);
 require(writer->writeFromAudioSampleBuffer(data,0,data.getNumSamples()),"Stereo IR fixture write failed");
}
inline juce::MemoryBlock bytes(const juce::File& file){juce::MemoryBlock result;require(file.loadFileAsData(result),"Fixture byte capture failed: "+file.getFileName());return result;}
inline juce::AudioBuffer<float> read(const juce::File& file){
 juce::WavAudioFormat format;auto stream=file.createInputStream();require(stream!=nullptr,"Export input missing");std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(),true));
 require(reader&&reader->numChannels==2&&reader->sampleRate==48000&&reader->usesFloatingPointData,"Host capture export format mismatch");
 juce::AudioBuffer<float> result(2,int(reader->lengthInSamples));require(reader->read(&result,0,result.getNumSamples(),0,true,true),"Export sample read failed");return result;
}
inline void drySafe(const juce::ValueTree& captured,const char* diagnostic){
 hem::ConvolutionReverbProcessor copy;hem::SessionState::restoreParameters(copy,captured);
 require(!copy.resourceInfo().ready&&copy.resourceInfo().status.containsIgnoreCase(diagnostic),"Referenced IR did not report changed/missing status");
 copy.prepareToPlay(48000,128);hostParameterChecks::setRequired(copy,"mix",100);hostParameterChecks::setRequired(copy,"soloWet",1);hostParameterChecks::setRequired(copy,"output",0);
 juce::AudioBuffer<float> signal(2,512);signal.clear();signal.setSample(0,0,.2f);signal.setSample(1,0,-.1f);juce::MidiBuffer midi;copy.processBlock(signal,midi);
 const auto latency=copy.getLatencySamples();require(latency>0&&latency<signal.getNumSamples(),"Convolution dry-safe latency invalid");
 for(int n=0;n<signal.getNumSamples();++n)require(signal.getSample(0,n)==(n==latency?.2f:0.f)&&signal.getSample(1,n)==(n==latency?-.1f:0.f),"Changed/missing IR violated latency-aligned stereo dry-safe behavior");
}
inline void run(MainComponent& host,const juce::File& directory){
 const auto source=directory.getChildFile("capture-source.wav"),ir=directory.getChildFile("capture-ir.wav"),other=directory.getChildFile("queued-ir.wav");
 fixture(source,4800);writeIR(ir);writeIR(other,.375f,.1875f);const auto sourceBytes=bytes(source),irBytes=bytes(ir);
 applyImport(host,{source.getFullPathName()});MainComponentTestAccess::slot(host,0);MainComponentTestAccess::insertMenu(host,index("convolution-reverb"));auto& p=convolution(host);
 require(p.loadIRFileOffThread(ir).wasOk(),"Actual host IR loading failed");hostParameterChecks::setRequired(p,"mix",62.5f);hostParameterChecks::setRequired(p,"output",-3);
 MainComponentTestAccess::slot(host,1);MainComponentTestAccess::insertMenu(host,index("tape-delay"));auto* delay=MainComponentTestAccess::bank(host).tracks[0].chain.activeProcessor(1);require(delay!=nullptr,"Host synced delay missing");
 hostParameterChecks::setRequired(*delay,"sync",1);hostParameterChecks::setRequired(*delay,"division",9);hostParameterChecks::setRequired(*delay,"mix",35);hostParameterChecks::setRequired(*delay,"feedback",0);
 MainComponentTestAccess::tempo(host).setValue(97.5,juce::sendNotificationSync);const auto a=MainComponentTestAccess::snapshot(host);const auto params=a.tracks[0].slots[0].parameters.createCopy();
 const std::array<const char*,6> keys{{"irResourceVersion","irPath","irSha256","irSampleRate","irChannels","irFrames"}};
 for(const auto* key:keys)require(params.hasProperty(key),juce::String("Host capture lost resource property ")+key);
 require(params.getNumProperties()==6&&params["irPath"].toString()==ir.getFullPathName()&&params["irSha256"].toString()==juce::SHA256(irBytes).toHexString()&&int(params["irFrames"])==1024&&int(params["irChannels"])==2&&double(params["irSampleRate"])==48000&&a.tempoBpm==97.5,"Actual host capture resource/BPM identity incorrect");
 // Keep a real queued request pending deterministically, without racing a tiny file.
 hem::ConvolutionReverbTestAccess::stop(p);const auto beforeRequest=hem::ConvolutionReverbTestAccess::generation(p);p.requestIRFile(other);const auto pending=p.resourceInfo();
 require(pending.loading&&pending.ready&&pending.sha256==params["irSha256"].toString()&&hem::ConvolutionReverbTestAccess::generation(p)>beforeRequest,"Pending IR lost previous valid identity/request generation");
 const auto rejectedSession=directory.getChildFile("pending.xml"),rejectedExport=directory.getChildFile("pending.wav");
 const auto save=MainComponentTestAccess::save(host,rejectedSession),render=MainComponentTestAccess::render(host,rejectedExport);
 require(save.failed()&&render.failed()&&save.getErrorMessage().containsIgnoreCase("IR loading")&&render.getErrorMessage().containsIgnoreCase("IR loading"),"Host save/export admitted pending IR or omitted diagnostic");
 require(!MainComponentTestAccess::sessionBusy(host)&&!MainComponentTestAccess::exportBusy(host)&&!rejectedSession.exists()&&!rejectedExport.exists(),"Pending resource published output/session or retained busy worker");
 require(p.resourceInfo().loading&&p.resourceInfo().sha256==params["irSha256"].toString(),"Rejected capture mutated pending resource");
 require(p.loadIRFileOffThread(ir).wasOk()&&!p.resourceInfo().loading&&hem::ConvolutionReverbTestAccess::generation(p)>beforeRequest+1,"IR recovery failed to supersede queued request");
 require(params.isEquivalentTo(MainComponentTestAccess::snapshot(host).tracks[0].slots[0].parameters),"Recovered host resource capture differs from A");
 const auto session=directory.getChildFile("capture.xml");require(MainComponentTestAccess::save(host,session).wasOk(),"Host resource save rejected");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Host resource save timeout");require(session.existsAsFile(),"Host resource save did not publish");
 // Actual host export owns capture A before returning. Mutating live B immediately
 // afterwards must not affect its frozen resource metadata, parameters or BPM.
 hem::OfflineMixOptions options;options.blockSize=128;options.tailSeconds=.8;options.allowTailTruncation=true;
 const auto output=directory.getChildFile("host-frozen-a.wav");require(MainComponentTestAccess::renderOptions(host,output,options).wasOk(),"Host convolution+sync-delay export rejected");
 require(p.loadIRFileOffThread(other).wasOk(),"Live B IR replacement failed");hostParameterChecks::setRequired(p,"mix",11);hostParameterChecks::setRequired(p,"output",-7);hostParameterChecks::setRequired(*delay,"mix",72);MainComponentTestAccess::tempo(host).setValue(143.25,juce::sendNotificationSync);
 const auto b=MainComponentTestAccess::snapshot(host);require(params.isEquivalentTo(a.tracks[0].slots[0].parameters)&&!params.isEquivalentTo(b.tracks[0].slots[0].parameters)&&a.tempoBpm==97.5&&b.tempoBpm==143.25&&b.tracks[0].slots[0].parameters["irPath"].toString()==other.getFullPathName()&&a.tracks[0].slots[0].parameters["irSha256"]==params["irSha256"],"Captured A aliased live B");
 until([&]{return !MainComponentTestAccess::exportBusy(host);},"Frozen host resource export timeout");require(output.existsAsFile()&&MainComponentTestAccess::exportResult(host).startsWith("Saved"),"Host frozen resource export failed");
 const auto reference=directory.getChildFile("private-captured-a.wav");const auto result=hem::OfflineMixRenderer::render(a,reference,options);require(result.status.wasOk(),"Private captured-A renderer failed");
 const auto hostAudio=read(output),referenceAudio=read(reference);require(hostAudio.getNumSamples()==referenceAudio.getNumSamples(),"Frozen host export frame count differs from captured A");float error=0,signal=0;
 for(int channel=0;channel<2;++channel)for(int n=0;n<hostAudio.getNumSamples();++n){error=std::max(error,std::abs(hostAudio.getSample(channel,n)-referenceAudio.getSample(channel,n)));signal=std::max(signal,std::abs(referenceAudio.getSample(channel,n)));}
 require(signal>1.e-6f&&error<2.e-6f,"Actual host frozen convolution/tempo export differs from independent captured-A render");
 require(b.tracks[0].slots[0].parameters.isEquivalentTo(MainComponentTestAccess::snapshot(host).tracks[0].slots[0].parameters)&&MainComponentTestAccess::bpm(host)==143.25,"Private exporter mutated live host parameters/BPM");
 require(bytes(source)==sourceBytes&&bytes(ir)==irBytes,"Host capture/export modified source or IR bytes");
 // Resource metadata references a path/hash. IR bytes are deliberately not embedded.
 writeIR(ir,.125f,.75f);drySafe(params,"changed");require(ir.deleteFile(),"Could not delete owned temporary IR");drySafe(params,"missing");require(ir.replaceWithData(irBytes.getData(),irBytes.getSize()),"Could not restore owned temporary IR bytes");
 require(MainComponentTestAccess::load(host,session).wasOk(),"Host captured-A recall rejected");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Host captured-A recall timeout");
 require(params.isEquivalentTo(MainComponentTestAccess::snapshot(host).tracks[0].slots[0].parameters)&&MainComponentTestAccess::bpm(host)==97.5&&convolution(host).resourceInfo().ready,"Host recall lost complete canonical resource state or BPM");
 const auto occupied=directory.getChildFile("occupied.wav");require(occupied.replaceWithText("existing destination sentinel"),"Destination sentinel creation failed");const auto sentinel=bytes(occupied);
 require(MainComponentTestAccess::renderOptions(host,occupied,options).wasOk(),"Destination failure worker did not start");until([&]{return !MainComponentTestAccess::exportBusy(host);},"Destination failure left worker busy");require(!MainComponentTestAccess::exportResult(host).startsWith("Saved")&&bytes(occupied)==sentinel,"Export failure overwrote existing destination");
 const auto cancelled=directory.getChildFile("cancelled-resource.wav");require(MainComponentTestAccess::slowExport(host,cancelled).wasOk(),"Resource cancellation worker did not start");MainComponentTestAccess::cancelExport(host);until([&]{return !MainComponentTestAccess::exportBusy(host);},"Resource cancellation left worker busy");
 require(!cancelled.exists()&&MainComponentTestAccess::exportResult(host).containsIgnoreCase("cancel")&&bytes(occupied)==sentinel,"Cancelled resource export published output or changed previous destination");
 const auto recovered=directory.getChildFile("recovered-resource.wav");require(MainComponentTestAccess::renderOptions(host,recovered,options).wasOk(),"Export did not recover after failure/cancel");until([&]{return !MainComponentTestAccess::exportBusy(host);},"Recovery export timeout");
 require(recovered.existsAsFile()&&MainComponentTestAccess::exportResult(host).startsWith("Saved")&&bytes(source)==sourceBytes&&bytes(ir)==irBytes,"Final recovery/source conservation failed");
 std::cout<<"PASS actual host resource capture: six IR fields, pending rejection, frozen convolution+sync-delay export vs captured A (max error "<<error<<"), changed/missing referenced-IR dry safety, failure/cancel/recovery and source-byte conservation\n";
}
}
