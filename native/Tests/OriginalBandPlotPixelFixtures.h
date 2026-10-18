#pragma once
// Deliberately synthetic display calibration, injected only by this test friend.
// Actual-audio telemetry/calibration remains covered by the original fixtures.
namespace hem {
struct OriginalBandPlotFixture {
    static void inject(OriginalDynamicEqEditor& e,const bands::VisualFrame& f){e.frame=f;e.stale=false;}
    static void inject(OriginalBandEditorBase& e,const bands::VisualFrame& f){e.frame=f;e.stale=false;}
};
}
struct PlotCaptionAudit {
    static inline void (*previous)(const hem::ui::BandTextRun&)=nullptr;
    static inline int clipped=0,calibrated=0;
    static void observe(const hem::ui::BandTextRun& run){if(previous)previous(run);clipped+=run.text=="VIEW CLIPPED";calibrated+=run.text=="X:S Y:M +/-1 PRE SOLO";}
    PlotCaptionAudit(){previous=hem::ui::bandTextObserver;hem::ui::bandTextObserver=&observe;}
    ~PlotCaptionAudit(){hem::ui::bandTextObserver=previous;}
};
static void bandPlotPixels(const juce::File& evidence){
    PlotCaptionAudit captionAudit;
    int assertions=0;
    for(int module:{0,1,3}){auto p=hem::PluginRegistry::create(ids[size_t(module)]);p->prepareToPlay(48000,257);juce::AudioBuffer<float> priming(2,257);priming.clear();juce::MidiBuffer primingMidi;p->processBlock(priming,primingMidi);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        hem::bands::VisualFrame frame;frame.processed=true;frame.rate=48000;frame.topology=1;frame.midParticipation=frame.sideParticipation=1;frame.width=100;
        for(auto& c:frame.coefficients){c.fill(0);c[0]=c[5]=1;}
        for(int width:{1000,1672})for(int dpi:{1,2}){
            editor->setSize(width,int(std::round(width*941./1672.)));const float sx=width/1672.f*dpi,sy=editor->getHeight()/941.f*dpi;
            const int cases=module==3?10:7;
            for(int sample=0;sample<cases;++sample){
                const double dbs[]{20.,18.,-18.,24.,-24.,0.,-20.};
                const double mids[]{.8,1.,-1.,0.,0.,1.,-1.,1.,-1.,1.2};
                const double sides[]{0.,0.,0.,1.,-1.,1.,1.,-1.,-1.,0.};
                if(module==3){frame.scopeCount=1;for(auto& b:frame.scopes){b[0][0]=float(mids[sample]+sides[sample]);b[0][1]=float(mids[sample]-sides[sample]);}}
                else{const double gain=std::pow(10.,dbs[sample]/20.);frame.coefficients[0][0]=frame.coefficients[0][5]=gain;
                    if(module==1)frame.coefficients[5][0]=frame.coefficients[5][5]=gain;}
                if(auto* dynamic=dynamic_cast<hem::OriginalDynamicEqEditor*>(editor.get()))hem::OriginalBandPlotFixture::inject(*dynamic,frame);
                else hem::OriginalBandPlotFixture::inject(*dynamic_cast<hem::OriginalBandEditorBase*>(editor.get()),frame);
                PlotCaptionAudit::clipped=PlotCaptionAudit::calibrated=0;
                juce::Image pixels(juce::Image::ARGB,width*dpi,editor->getHeight()*dpi,true);{juce::Graphics g(pixels);g.addTransform(juce::AffineTransform::scale(float(dpi)));editor->paintEntireComponent(g,true);}
                if(module==3)require(PlotCaptionAudit::clipped==(sample==9?4:0)&&PlotCaptionAudit::calibrated==(sample==9?0:4),"scope clipping indication or axis caption disagrees with injected data");
                auto visible=[&](float x,float y,bool orange){bool found=false;const int px=int(std::round(x*sx)),py=int(std::round(y*sy)),radius=std::max(2,int(std::ceil(2*sx)));
                    for(int yy=py-radius;yy<=py+radius;++yy)for(int xx=px-radius;xx<=px+radius;++xx){const auto c=pixels.getPixelAt(xx,yy);
                        found|=orange?(c.getRed()>170&&c.getGreen()>80&&c.getBlue()<110):(c.getRed()<110&&c.getGreen()>140&&c.getBlue()>160);}
                    if(!found)std::cerr<<"PLOT PIXEL module="<<ids[size_t(module)]<<" case="<<sample<<" width="<<width<<" dpi="<<dpi<<" xy="<<x<<','<<y<<'\n';
                    require(found,"in-range plot data hidden/miscalibrated at expected pixel");++assertions;};
                if(module==0){const float y=248.5f-float(dbs[sample])*129.f/48.f;for(float x:{246.f,858.5f,1471.f})visible(x,y,false);}
                if(module==1){const float y=224.5f-float(dbs[sample])*53.f/48.f;for(int lane:{0,1})for(float x:{249.f,526.f,803.f})visible(x+643.f*lane,y,lane==1);}
                if(module==3){for(int band=0;band<4;++band){const float x=386.5f+299.f*band+float(std::clamp(sides[sample],-1.,1.))*42.5f,y=568.5f-float(std::clamp(mids[sample],-1.,1.))*42.5f;visible(x,y,false);}}
                // The reported formerly-hidden values are retained as full-editor PNGs.
                if(sample==(module==1?1:0)){juce::MemoryOutputStream png;require(juce::PNGImageFormat().writeImageToStream(pixels,png),"plot calibration PNG encode");
                    require(evidence.getChildFile("plot-calibration-"+juce::String(ids[size_t(module)])+"-"+juce::String(width)+"-"+juce::String(dpi)+"x.png").replaceWithData(png.getData(),png.getDataSize()),"plot calibration PNG write");}
            }
        }
    }
    // Release must immediately blank live plots/GR even if the last published
    // Band visual frame still reports processed=true and has not aged out.
    for(int module=0;module<4;++module){auto p=hem::PluginRegistry::create(ids[size_t(module)]);p->prepareToPlay(48000,257);juce::AudioBuffer<float> priming(2,257);priming.clear();juce::MidiBuffer primingMidi;p->processBlock(priming,primingMidi);std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());editor->setSize(1672,941);
        hem::bands::VisualFrame f;f.processed=true;f.rate=48000;f.topology=1;f.midParticipation=f.sideParticipation=1;f.width=100;f.scopeCount=1;f.bandCuts.fill(12);f.revision=1;f.samples=257;
        for(auto& c:f.coefficients){c.fill(0);c[0]=c[5]=1;}f.coefficients[0][0]=f.coefficients[0][5]=2;f.coefficients[5][0]=f.coefficients[5][5]=2;for(auto& scope:f.scopes){scope[0][0]=.8f;scope[0][1]=.2f;}
        if(auto* e=dynamic_cast<hem::OriginalDynamicEqEditor*>(editor.get()))hem::OriginalBandPlotFixture::inject(*e,f);else hem::OriginalBandPlotFixture::inject(*dynamic_cast<hem::OriginalBandEditorBase*>(editor.get()),f);
        const juce::Rectangle<int> area=module==0?juce::Rectangle<int>(250,187,1215,122):module==1?juce::Rectangle<int>(252,196,550,53):module==2?juce::Rectangle<int>(1296,280,90,82):juce::Rectangle<int>(342,524,90,89);
        auto count=[&]{juce::Image pixels(juce::Image::ARGB,1672,941,true);{juce::Graphics g(pixels);editor->paintEntireComponent(g,true);}int n=0;for(int y=area.getY();y<area.getBottom();++y)for(int x=area.getX();x<area.getRight();++x){auto c=pixels.getPixelAt(x,y);n+=(c.getRed()<110&&c.getGreen()>140&&c.getBlue()>160)||(c.getRed()>170&&c.getGreen()>80&&c.getBlue()<110);}return n;};
        require(count()>0,"release plot test must first observe live data");p->releaseResources();require(!p->processingReady(),"released Band processor unavailable");require(count()==0,"released Band editor retains live curve/scope/GR");
        p->prepareToPlay(48000,257);require(p->processingReady()&&!p->meterSnapshot().hasProcessedAudio,"reprepared processor waits for new audio");require(count()==0,"reprepared Band editor presents cached plot before new audio");
    }
    std::cout<<"PASS immediate release and reprepare-before-audio blank Dynamic/MidSide curves, Dynamics GR and Imager scopes without clearing DSP history\n";
    std::cout<<"PASS caption-free plot calibration: "<<assertions<<" actual full-editor data pixel checks; synthetic +20/+18/-18/+24/-24/0/-20 dB, M=.8 and both axes endpoints/corners/clipping; widths1000/1672 at1x/2x\n";
}
