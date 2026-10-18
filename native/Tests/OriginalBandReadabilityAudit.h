#pragma once
// Observe the actual live text drawing calls, not a separate approximation of
// their chosen fonts. All callbacks here run on the fixture's message thread.
static void auditOriginalBandText(const hem::ui::BandTextRun& run){
    const auto glyphs=juce::GlyphArrangement::getStringBounds(run.font,run.text);
    const bool minimum=run.font.getHeight()*run.screenScale>=hem::ui::minimumBandTextPixels-.01f;
    const bool fits=glyphs.getWidth()<=run.bounds.getWidth()+1.f/run.screenScale&&glyphs.getHeight()<=run.bounds.getHeight()+1.f/run.screenScale;
    if(!minimum||!fits){std::cerr<<"LIVE TEXT BOUNDS: '"<<run.text<<"' pixels="<<run.font.getHeight()*run.screenScale<<" glyph="<<glyphs.getWidth()<<','<<glyphs.getHeight()<<" box="<<run.bounds.getWidth()<<','<<run.bounds.getHeight()<<" scale="<<run.screenScale<<'\n';}
    require(minimum&&fits,"live condensed text below screen minimum or outside its bounds");
}
struct OriginalBandReadabilityAudit {
    OriginalBandReadabilityAudit(){hem::ui::bandTextObserver=&auditOriginalBandText;}
    ~OriginalBandReadabilityAudit(){hem::ui::bandTextObserver=nullptr;}
};

static void auditBandControl(juce::Component& control){
    auto pixels=control.createComponentSnapshot(control.getLocalBounds(),true,1.f);
    require(pixels.isValid(),"control extreme-value raster audit");
}
static void auditBandHit(juce::Component& editor,juce::Component& control){
    if(!control.isVisible())return;
    // Offline component: no native peer. JUCE hit testing requires visibleFlag.
    editor.setVisible(true);
    auto check=[&](juce::Point<int> local){auto* hit=editor.getComponentAt(editor.getLocalPoint(&control,local));
        const bool valid=hit==&control||(hit&&control.isParentOf(hit));
        if(!valid)std::cerr<<"HIT PATH: "<<control.getComponentID()<<" blocked by "<<(hit?hit->getComponentID():"null")<<'\n';
        require(valid,"visible physical control root hit path blocked");};
    if(auto* slider=dynamic_cast<hem::ui::ReferenceSlider*>(&control)){
        check(slider->dialBounds.getCentre());if(!slider->textBounds.isEmpty())check(slider->textBounds.getCentre());
    }else check(control.getLocalBounds().getCentre());
}
