#include "ClipEditState.h"
#include "BoundedStateInput.h"
#include <juce_cryptography/juce_cryptography.h>
#include <charconv>
#include <filesystem>
#include <set>
#include <stdexcept>
namespace hem::clipEdit {
namespace {
void need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F> juce::Result attempt(F&& f){try{f();return juce::Result::ok();}catch(const std::exception& e){return juce::Result::fail(e.what());}catch(...){return juce::Result::fail("Clip edit validation failed");}}
void checked(const juce::Result& r){if(r.failed())throw std::runtime_error(r.getErrorMessage().toStdString());}
bool hashValid(const juce::String& s){return s.length()==64&&s.containsOnly("0123456789abcdef");}
bool rateValid(double n){return std::isfinite(n)&&n>=8000&&n<=192000;}
void identity(const MediaIdentity& m){need(m.path.isNotEmpty()&&m.path.getNumBytesAsUTF8()<=4096&&juce::File::isAbsolutePath(m.path),"Invalid clip media path");
 need(hashValid(m.sha256)&&m.bytes>0&&m.bytes<=maxFileBytes&&rateValid(m.rate)&&m.channels>=1&&m.channels<=2&&m.frames>0&&m.frames<=juce::int64(m.rate*1209600),"Invalid clip media identity bounds");}
double number(const juce::var& v){double n=0;if(v.isString()){need(v.toString().getNumBytesAsUTF8()<=64,"Oversized clip numeric value");const auto s=v.toString().toStdString();const auto r=std::from_chars(s.data(),s.data()+s.size(),n);need(r.ec==std::errc{}&&r.ptr==s.data()+s.size(),"Malformed numeric clip value");}else{need(v.isInt()||v.isInt64()||v.isDouble(),"Non-numeric clip value");n=double(v);}need(std::isfinite(n),"Nonfinite clip value");return n;}
juce::int64 integer(const juce::var& v){if(v.isString()){const auto text=v.toString();need(text.getNumBytesAsUTF8()<=16,"Oversized clip integer");const auto bytes=text.toStdString();juce::int64 exactValue=0;const auto parsed=std::from_chars(bytes.data(),bytes.data()+bytes.size(),exactValue);need(parsed.ec==std::errc{}&&parsed.ptr==bytes.data()+bytes.size()&&exactValue>=0&&exactValue<=9007199254740991LL,"Invalid exact clip integer");return exactValue;}const auto n=number(v);need(n>=0&&n<=9007199254740991.&&std::floor(n)==n,"Invalid clip integer");return juce::int64(n);}
bool boolean(const juce::var& v){if(v.isBool())return bool(v);const auto n=integer(v);need(n<=1,"Invalid clip boolean");return n==1;}
void properties(const juce::ValueTree& t,std::initializer_list<const char*> keys){need(t.getNumProperties()==int(keys.size()),"Unexpected clip attributes");for(auto key:keys)need(t.hasProperty(key),"Missing clip attribute");}
void recipeValid(const Recipe& r){const auto& s=r.settings;
 need(r.backend==pitchtime::backendIdentity&&r.sourceGeneration>=0&&r.sourceGeneration<=9007199254740991LL,"Unsupported clip backend/generation");
 need(rateValid(s.sampleRate)&&s.channels>=1&&s.channels<=2&&std::isfinite(s.durationRatio)&&s.durationRatio>=.5&&s.durationRatio<=2&&
 std::isfinite(s.pitchSemitones)&&std::abs(s.pitchSemitones)<=24&&std::isfinite(s.formantSemitones)&&std::abs(s.formantSemitones)<=12&&
 std::isfinite(s.formantBaseHz)&&s.formantBaseHz>=0&&s.formantBaseHz<=2000&&std::isfinite(s.wetMix)&&s.wetMix>=0&&s.wetMix<=1&&
 (s.wetMix==1||s.durationRatio==1)&&std::isfinite(s.outputDb)&&s.outputDb>=-24&&s.outputDb<=12&&int(s.strategy)>=0&&int(s.strategy)<=2&&s.correction.valid()&&
 std::isfinite(r.tempoBpm)&&r.tempoBpm>=20&&r.tempoBpm<=400,"Invalid clip recipe controls");}
juce::var exact(const juce::var& value){if(!value.isDouble())return value;char text[64];const auto result=std::to_chars(text,text+sizeof(text),double(value),std::chars_format::general,std::numeric_limits<double>::max_digits10);need(result.ec==std::errc{},"Cannot encode clip number");return juce::String::fromUTF8(text,int(result.ptr-text));}
juce::ValueTree recipeTree(const Recipe& r){juce::ValueTree t("RECIPE");const auto& s=r.settings;
 auto set=[&](const char* k,const juce::var& v){t.setProperty(k,exact(v),nullptr);};
 set("backend",r.backend);set("sourceGeneration",r.sourceGeneration);set("sampleRate",s.sampleRate);set("channels",s.channels);set("durationRatio",s.durationRatio);set("pitchSemitones",s.pitchSemitones);
 set("formantSemitones",s.formantSemitones);set("formantBaseHz",s.formantBaseHz);set("wetMix",s.wetMix);set("outputDb",s.outputDb);set("preserveFormants",s.preserveFormants);set("strategy",int(s.strategy));set("tempoBpm",r.tempoBpm);
 set("correctionEnabled",s.correction.enabled);set("key",s.correction.key);set("scale",int(s.correction.scale));set("amount",s.correction.amount);set("speedMs",s.correction.speedMs);return t;}
Recipe readRecipe(const juce::ValueTree& t){need(t.hasType("RECIPE")&&t.getNumChildren()==0,"Invalid clip recipe node");properties(t,{"backend","sourceGeneration","sampleRate","channels","durationRatio","pitchSemitones","formantSemitones","formantBaseHz","wetMix","outputDb","preserveFormants","strategy","tempoBpm","correctionEnabled","key","scale","amount","speedMs"});
 need(t["backend"].isString(),"Invalid backend field");Recipe r;auto& s=r.settings;r.backend=t["backend"].toString();r.sourceGeneration=integer(t["sourceGeneration"]);s.sampleRate=number(t["sampleRate"]);
 const auto channels=integer(t["channels"]),strategy=integer(t["strategy"]),key=integer(t["key"]),scale=integer(t["scale"]);need(channels<=2&&strategy<=2&&key<=11&&scale<=2,"Clip enum out of range");
 s.channels=int(channels);s.strategy=pitchtime::Strategy(strategy);s.correction.key=int(key);s.correction.scale=pitchtime::Scale(scale);
 s.durationRatio=number(t["durationRatio"]);s.pitchSemitones=number(t["pitchSemitones"]);s.formantSemitones=number(t["formantSemitones"]);s.formantBaseHz=number(t["formantBaseHz"]);s.wetMix=number(t["wetMix"]);s.outputDb=number(t["outputDb"]);
 s.preserveFormants=boolean(t["preserveFormants"]);r.tempoBpm=number(t["tempoBpm"]);s.correction.enabled=boolean(t["correctionEnabled"]);s.correction.amount=number(t["amount"]);s.correction.speedMs=number(t["speedMs"]);recipeValid(r);return r;}
juce::ValueTree mediaTree(const MediaIdentity& m,const char* role){juce::ValueTree t("MEDIA");t.setProperty("role",role,nullptr);t.setProperty("path",m.path,nullptr);t.setProperty("sha256",m.sha256,nullptr);t.setProperty("bytes",m.bytes,nullptr);t.setProperty("frames",m.frames,nullptr);t.setProperty("rate",exact(m.rate),nullptr);t.setProperty("channels",m.channels,nullptr);return t;}
MediaIdentity readMedia(const juce::ValueTree& t,const char* role){need(t.hasType("MEDIA")&&t.getNumChildren()==0&&t["role"].toString()==role,"Invalid clip media node/order");properties(t,{"role","path","sha256","bytes","frames","rate","channels"});need(t["path"].isString()&&t["sha256"].isString(),"Invalid media text fields");const auto channels=integer(t["channels"]);need(channels<=2,"Invalid clip channels");MediaIdentity m{t["path"].toString(),t["sha256"].toString(),integer(t["bytes"]),integer(t["frames"]),number(t["rate"]),int(channels)};identity(m);return m;}
std::filesystem::path nativePath(const juce::String& s){return std::filesystem::path(s.toWideCharPointer());}
void verify(const MediaIdentity& m,const pitchtime::Cancel& cancel){
 auto checkCancel=[&]{need(!cancel||!cancel(),"Clip media verification cancelled");};checkCancel();juce::File file(m.path);need(file.existsAsFile()&&file.getSize()==m.bytes,"Clip media missing or size changed");
 // Stream owns one fixed32KiB scratch buffer; no whole-file allocation.
 class CheckedStream final:public juce::InputStream{public:juce::FileInputStream input;const pitchtime::Cancel& cancel;
 CheckedStream(const juce::File& f,const pitchtime::Cancel& c):input(f),cancel(c){need(input.openedOk(),"Cannot open clip media");}
 juce::int64 getTotalLength()override{return input.getTotalLength();}juce::int64 getPosition()override{return input.getPosition();}bool setPosition(juce::int64 p)override{return input.setPosition(p);}bool isExhausted()override{return input.isExhausted();}
 int read(void* b,int n)override{need(!cancel||!cancel(),"Clip verification cancelled");auto count=input.read(b,n);need(input.getStatus().wasOk(),"Clip media read failed");return count;}};
 auto hashFile=[&]{CheckedStream input(file,cancel);const auto hash=juce::SHA256(input).toHexString();need(input.getPosition()==m.bytes&&file.getSize()==m.bytes&&hash==m.sha256,"Clip media hash changed");};
 hashFile();checkCancel();juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
 need(reader&&reader->lengthInSamples==m.frames&&reader->sampleRate==m.rate&&int(reader->numChannels)==m.channels,"Clip media format changed");reader.reset();hashFile();}
}
juce::Result validate(const State& s){return attempt([&]{need(s.trackIndex>=0&&s.trackIndex<13,"Whole-stem track index out of range");identity(s.original);identity(s.rendered);recipeValid(s.recipe);
 need(!juce::File(s.original.path).getFullPathName().equalsIgnoreCase(juce::File(s.rendered.path).getFullPathName()),"Clip render must not overwrite original");
 need(s.original.frames<=juce::int64(s.original.rate*604800)&&s.original.rate==s.rendered.rate&&s.original.channels==s.rendered.channels&&s.original.rate==s.recipe.settings.sampleRate&&s.original.channels==s.recipe.settings.channels,"Clip media/recipe mismatch");
 need(s.rendered.frames==juce::int64(std::llround(double(s.original.frames)*s.recipe.settings.durationRatio))&&s.rendered.frames*s.rendered.channels*4<=0xff000000LL,"Clip rendered length/capacity mismatch");});}
bool sameIdentity(const MediaIdentity& a,const MediaIdentity& b) noexcept{return a.path==b.path&&a.sha256==b.sha256&&a.bytes==b.bytes&&a.frames==b.frames&&a.rate==b.rate&&a.channels==b.channels;}
const MediaIdentity& activeMedia(const State& s) noexcept{return s.renderedActive?s.rendered:s.original;}
juce::Result captureMetadata(const pitchtime::ClipRenderMetadata& m,int track,State& out){return attempt([&]{
 need(m.startFrame==0&&m.frameCount==m.sourceFrames,"CLIP_EDIT supports whole-stem edits only");need(m.recipeJson.getNumBytesAsUTF8()<=8192&&hashValid(m.recipeSha256),"Invalid recipe bounds/hash");
 const auto utf8=m.recipeJson.toStdString();checked(stateinput::preflightJson(utf8,{8192,4,32,4}));need(juce::SHA256(utf8.data(),utf8.size()).toHexString()==m.recipeSha256,"Recipe identity mismatch");
 const auto parsed=juce::JSON::parse(m.recipeJson);auto* object=parsed.getDynamicObject();need(object!=nullptr&&object->getProperties().size()==22,"Invalid render recipe JSON schema");
 need(integer(object->getProperty("version"))==1&&object->getProperty("sourceSha256").toString()==m.sourceSha256&&integer(object->getProperty("sourceGeneration"))==m.sourceGeneration&&integer(object->getProperty("startFrame"))==0&&integer(object->getProperty("frameCount"))==m.sourceFrames,"Render recipe scope/identity mismatch");
 juce::ValueTree t("RECIPE");for(const auto& pair:object->getProperties())if(pair.name.toString()!="version"&&pair.name.toString()!="sourceSha256"&&pair.name.toString()!="startFrame"&&pair.name.toString()!="frameCount")t.setProperty(pair.name,pair.value,nullptr);
 State s;s.trackIndex=track;s.original={m.source.getFullPathName(),m.sourceSha256,m.sourceBytes,m.sourceFrames,m.sampleRate,m.channels};s.rendered={m.rendered.getFullPathName(),m.renderedSha256,m.renderedBytes,m.renderedFrames,m.sampleRate,m.channels};s.recipe=readRecipe(t);checked(validate(s));out=std::move(s);});}
juce::Result encode(const State& s,juce::ValueTree& out){return attempt([&]{checked(validate(s));juce::ValueTree t("CLIP_EDIT");t.setProperty("version",1,nullptr);t.setProperty("scope","whole-stem",nullptr);t.setProperty("trackIndex",s.trackIndex,nullptr);t.setProperty("renderedActive",s.renderedActive,nullptr);t.addChild(mediaTree(s.original,"original"),-1,nullptr);t.addChild(mediaTree(s.rendered,"rendered"),-1,nullptr);t.addChild(recipeTree(s.recipe),-1,nullptr);need(t.toXmlString().getNumBytesAsUTF8()<=maxSerializedBytes,"Clip state too large");out=t;});}
juce::Result decode(const juce::ValueTree& t,State& out){return attempt([&]{need(t.hasType("CLIP_EDIT")&&t.getNumChildren()==3,"Invalid CLIP_EDIT root");properties(t,{"version","scope","trackIndex","renderedActive"});need(integer(t["version"])==1&&t["scope"].toString()=="whole-stem","Unsupported clip schema/scope");const auto index=integer(t["trackIndex"]);need(index<13,"Invalid clip track index");State s;s.trackIndex=int(index);s.renderedActive=boolean(t["renderedActive"]);s.original=readMedia(t.getChild(0),"original");s.rendered=readMedia(t.getChild(1),"rendered");s.recipe=readRecipe(t.getChild(2));checked(validate(s));out=std::move(s);});}
juce::Result verifyFiles(const State& s,const pitchtime::Cancel& cancel){return attempt([&]{checked(validate(s));need(!cancel||!cancel(),"Clip media verification cancelled");std::error_code ec;const auto equivalent=std::filesystem::equivalent(nativePath(s.original.path),nativePath(s.rendered.path),ec);need(!ec&&!equivalent,"Clip paths missing or alias the same file");verify(s.original,cancel);verify(s.rendered,cancel);});}
juce::Result stageToggle(const State& s,bool rendered,const MediaIdentity& current,State& next,MediaIdentity& target,const pitchtime::Cancel& cancel){return attempt([&]{checked(validate(s));need(rendered!=s.renderedActive,"No further whole-stem Undo/Redo step");need(sameIdentity(current,activeMedia(s)),"Track source changed since clip edit");checked(verifyFiles(s,cancel));State candidate=s;candidate.renderedActive=rendered;MediaIdentity chosen=activeMedia(candidate);next=std::move(candidate);target=std::move(chosen);});}
}



