#pragma once
// New faceplates use vertical input/output meters, not the pre-existing shared
// horizontal strip. Retain the original strip assertions for every old module
// and scan both complete new meter apertures for fictitious fill here.
inline bool bandNoBar(hem::ModuleProcessor& p,const juce::String& name,const juce::File& artifacts){
    auto* band=dynamic_cast<hem::BandModuleProcessor*>(&p);if(!band)return false;
    std::array<juce::Rectangle<int>,2> apertures;
    switch(band->bandKind){
        case hem::bands::Kind::Dynamic:apertures={juce::Rectangle<int>(65,175,69,184),juce::Rectangle<int>(1538,175,69,184)};break;
        case hem::bands::Kind::MidSide:apertures={juce::Rectangle<int>(64,137,86,165),juce::Rectangle<int>(1522,137,86,165)};break;
        case hem::bands::Kind::Dynamics:apertures={juce::Rectangle<int>(1491,212,95,119),juce::Rectangle<int>(1491,387,95,143)};break;
        default:apertures={juce::Rectangle<int>(78,144,100,324),juce::Rectangle<int>(1493,144,99,324)};break;
    }
    for(int scale:{1,2}){std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        require(editor->getWidth()==1280&&editor->getHeight()==720,"Original band default editor geometry differs from host policy");
        editor->setSize(1672,941); // The pixel oracle below uses native artwork coordinates.
        juce::Image image(juce::Image::ARGB,1672*scale,941*scale,true);{juce::Graphics g(image);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true);}
        for(const auto r:apertures)for(int y=r.getY()*scale;y<r.getBottom()*scale;++y)for(int x=r.getX()*scale;x<r.getRight()*scale;++x){const auto pixel=image.getPixelAt(x,y);
            require(pixel!=juce::Colour(0xff41d7c5)&&pixel!=juce::Colour(0xffffbd59)&&pixel!=juce::Colour(0xffff655b),name+" displays a fictitious original-faceplate stereo LED");}
        if(artifacts!=juce::File()){auto out=artifacts.getChildFile(name+"-"+juce::String(scale)+"x.png").createOutputStream();require(out!=nullptr,"Cannot write original band meter raster");require(juce::PNGImageFormat().writeImageToStream(image,*out),"Cannot encode original band meter raster");}
    }return true;
}
