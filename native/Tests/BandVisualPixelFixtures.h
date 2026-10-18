#pragma once
static void bandVisualPixels(const juce::File& evidence){
    const std::array<std::array<juce::Rectangle<int>,2>,4> areas{{
        {juce::Rectangle<int>(65,175,69,184),juce::Rectangle<int>(1538,175,69,184)},
        {juce::Rectangle<int>(64,137,86,165),juce::Rectangle<int>(1522,137,86,165)},
        {juce::Rectangle<int>(1491,212,95,119),juce::Rectangle<int>(1491,387,95,143)},
        {juce::Rectangle<int>(78,144,100,324),juce::Rectangle<int>(1493,144,99,324)}}};
    auto lit=[](juce::Colour c){return c.getGreen()>90||c.getRed()>100;};
    for(size_t module=0;module<ids.size();++module){auto p=hem::PluginRegistry::create(ids[module]);p->prepareToPlay(48000,257);
        auto* band=dynamic_cast<hem::BandModuleProcessor*>(p.get());require(band!=nullptr&&p->pluginTelemetry()!=nullptr,"band stereo telemetry support");std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());juce::AudioBuffer<float> signal(2,257);juce::MidiBuffer midi;
        float lastLeft=0,lastRight=0;auto process=[&](float left,float right){lastLeft=left;lastRight=right;for(int block=0;block<32;++block){for(int n=0;n<257;++n){signal.setSample(0,n,left);signal.setSample(1,n,right);}allocationProbe::enabled=true;p->processBlock(signal,midi);allocationProbe::enabled=false;}pumpBandMessages(60);};
        // Preserve the recovered max-L/R numerical telemetry contract separately.
        for(float amplitude:{0.f,.5f,2.f}){p->prepareToPlay(48000,257);process(amplitude,amplitude*.5f);hem::bands::VisualFrame frame;require(band->visualSnapshot(frame)&&frame.processed,"band actual visual measurement available");require(std::abs(frame.inputPeak-amplitude)<1e-7&&std::abs(frame.outputPeak-amplitude)<1e-7,"original max-L/R numeric peak calibration at dry neutral startup");}
        auto raster=[&](const juce::String& tag,bool expectedLive){
            hem::PluginTelemetry::Levels levels;p->pluginTelemetry()->read(levels);
            for(int width:{1000,1280,1672})for(int dpi:{1,2}){if(expectedLive)process(lastLeft,lastRight);const int height=int(std::round(width*941./1672.));editor->setSize(width,height);const float sx=width/1672.f*dpi,sy=height/941.f*dpi;
                juce::Image pixels(juce::Image::ARGB,width*dpi,height*dpi,true);{juce::Graphics g(pixels);g.addTransform(juce::AffineTransform::scale(float(dpi)));editor->paintEntireComponent(g,true);}
                for(int output=0;output<2;++output)for(int channel=0;channel<2;++channel){const auto aperture=areas[module][size_t(output)].toFloat(),lane=hem::ui::OriginalBandStereoMeters::lane(aperture,channel);const float amplitude=levels.peak[size_t(output*2+channel)];
                    const auto model=hem::ui::meterSegments(hem::ui::meterRect(lane),hem::ui::meterAmplitudePosition(amplitude),expectedLive);int actual=0,wanted=0;
                    for(int segment=0;segment<model.count;++segment){const auto r=hem::ui::juceMeterRect(model.segment(segment));const int x=int(r.getCentreX()*sx),y=int(r.getCentreY()*sy);const bool on=lit(pixels.getPixelAt(x,y));actual+=on;wanted+=model.lit(segment);
                        if(on!=model.lit(segment))std::cerr<<"stereo pixel module="<<ids[module]<<" tag="<<tag<<" width="<<width<<" dpi="<<dpi<<" output="<<output<<" channel="<<channel<<" segment="<<segment<<" value="<<amplitude<<'\n';require(on==model.lit(segment),"independent stereo segment disagrees with channel peak");}
                    require(actual==wanted,"stereo segment count");
                }
                if(width==1280&&dpi==1){juce::MemoryOutputStream png;require(juce::PNGImageFormat().writeImageToStream(pixels,png),"band stereo PNG encode");require(evidence.getChildFile("stereo-"+juce::String(ids[module])+"-"+tag+".png").replaceWithData(png.getData(),png.getDataSize()),"band stereo PNG write");}
            }
        };
        p->prepareToPlay(48000,257);raster("prepared",false);
        set(*p,"enabled",1);set(*p,"output",-6);process(.5f,.0316228f);hem::PluginTelemetry::Levels levels;require(p->pluginTelemetry()->read(levels)&&levels.valid,"coherent stereo levels");
        require(std::abs(levels.peak[0]-.5f)<1e-6&&std::abs(levels.peak[1]-.0316228f)<1e-6,"input channels independently captured");require(levels.peak[2]<levels.peak[0]*.6f&&levels.peak[3]<levels.peak[1]*.6f&&levels.peak[2]>levels.peak[3]*10,"output channel peaks independent from input and each other");raster("left-loud",true);
        process(.0158489f,.794328f);raster("right-loud",true);
        process(.5f,0);raster("right-silent",true);process(0,.5f);raster("left-silent",true);
        process(0,0);raster("silence",true);
        process(2.f,.00316228f);raster("overrange",true);
        pumpBandMessages(560);raster("stale",false);
        editor.reset();editor.reset(p->createEditor());pumpBandMessages(60);raster("reopened-frozen",false);
        process(.5f,.05f);raster("resumed",true);p->releaseResources();raster("released",false);
    }
    std::cout<<"PASS original Band4 independent stereo segmented meters: asymmetric input/output, silence/overrange, preparation/stale/release blanking, widths1000/1280/1672 at1x/2x; original max-peak numeric calibration retained\n";
}

