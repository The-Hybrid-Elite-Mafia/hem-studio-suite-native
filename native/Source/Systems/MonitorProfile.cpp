#include "MonitorProfile.h"
#include <stdexcept>
namespace hem {
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
double numeric(const juce::var& v,double low,double high){require(v.isInt()||v.isInt64()||v.isDouble(),"Calibration parameter must be numeric");double n=v;require(std::isfinite(n)&&n>=low&&n<=high,"Calibration parameter outside supported range");return n;}
const char* filterName(MonitorProfile::Filter t){return t==MonitorProfile::Filter::Bell?"bell":t==MonitorProfile::Filter::LowShelf?"lowShelf":"highShelf";}
}
MonitorProfile MonitorProfile::load(const juce::File& f){
    require(f.existsAsFile()&&f.getSize()>0&&f.getSize()<=65536,"Calibration file missing or larger than 64 KB");
    juce::var root;require(juce::JSON::parse(f.loadFileAsString(),root).wasOk()&&root.isObject(),"Invalid calibration JSON");
    require(numeric(root["version"],1,1)==1,"Unsupported calibration version");MonitorProfile p;
    p.name=root["name"].toString();p.device=root["device"].toString();p.connection=root["connection"].toString();p.deviceMode=root["deviceMode"].toString();p.measurementSource=root["measurementSource"].toString();
    require(p.name.isNotEmpty()&&p.device.isNotEmpty()&&p.connection.isNotEmpty()&&p.deviceMode.isNotEmpty()&&p.measurementSource.isNotEmpty(),"Calibration must identify device, connection, mode and measurement source");
    p.preampDb=float(numeric(root["preampDb"],-36,0));auto list=root["bands"];auto* bands=list.getArray();require(bands&&bands->size()<=MonitoringRouter::maxBands,"Calibration supports up to twelve bands");
    for(const auto& b:*bands){require(b.isObject(),"Invalid calibration band");auto type=b["type"].toString();require(type=="bell"||type=="lowShelf"||type=="highShelf","Unsupported calibration filter");
        p.bands.push_back({type=="bell"?Filter::Bell:type=="lowShelf"?Filter::LowShelf:Filter::HighShelf,float(numeric(b["hz"],10,40000)),float(numeric(b["q"],.1,10)),float(numeric(b["db"],-18,12))});}
    return p;
}
MonitoringRouter::Curve MonitorProfile::prepare(double sampleRate) const {
    require(std::isfinite(sampleRate)&&sampleRate>=8000&&sampleRate<=384000,"Unsupported calibration sample rate");
    require(bands.size()<=MonitoringRouter::maxBands&&std::isfinite(preampDb)&&preampDb>=-36&&preampDb<=0,"Invalid calibration headroom");
    MonitoringRouter::Curve curve;curve.count=int(bands.size());curve.preamp=gain(preampDb);
    for(size_t i=0;i<bands.size();++i){const auto& b=bands[i];require(std::isfinite(b.hz)&&std::isfinite(b.q)&&std::isfinite(b.db)&&b.hz>=10&&b.hz<sampleRate*.49&&b.q>=.1&&b.q<=10&&b.db>=-18&&b.db<=12,"Calibration band invalid for current sample rate");auto& f=curve.filters[i];
        require(b.hz<=40000&&(b.type==Filter::Bell||b.type==Filter::LowShelf||b.type==Filter::HighShelf),"Invalid calibration filter type/frequency");
        // Compute directly: no legacy frequency clamping or fixed shelf Q.
        const double a=std::pow(10.,b.db/40.),w=2*pi*b.hz/sampleRate,c=std::cos(w),alpha=std::sin(w)/(2*b.q),r=2*std::sqrt(a)*alpha;
        double a0=1;
        if(b.type==Filter::Bell){
            f.b0=1+alpha*a;f.b1=-2*c;f.b2=1-alpha*a;a0=1+alpha/a;f.a1=-2*c;f.a2=1-alpha/a;
        }else if(b.type==Filter::LowShelf){
            f.b0=a*((a+1)-(a-1)*c+r);f.b1=2*a*((a-1)-(a+1)*c);f.b2=a*((a+1)-(a-1)*c-r);
            a0=(a+1)+(a-1)*c+r;f.a1=-2*((a-1)+(a+1)*c);f.a2=(a+1)+(a-1)*c-r;
        }else{
            f.b0=a*((a+1)+(a-1)*c+r);f.b1=-2*a*((a-1)+(a+1)*c);f.b2=a*((a+1)+(a-1)*c-r);
            a0=(a+1)-(a-1)*c+r;f.a1=2*((a-1)-(a+1)*c);f.a2=(a+1)-(a-1)*c-r;
        }
        f.b0/=a0;f.b1/=a0;f.b2/=a0;f.a1/=a0;f.a2/=a0;
    }
    return curve;
}
void MonitorProfile::save(const MonitorProfile& p,const juce::File& file){
    p.prepare(96000);auto* object=new juce::DynamicObject();juce::var root(object);object->setProperty("version",1);
    object->setProperty("name",p.name);object->setProperty("device",p.device);object->setProperty("connection",p.connection);object->setProperty("deviceMode",p.deviceMode);object->setProperty("measurementSource",p.measurementSource);object->setProperty("preampDb",p.preampDb);
    require(p.name.isNotEmpty()&&p.device.isNotEmpty()&&p.connection.isNotEmpty()&&p.deviceMode.isNotEmpty()&&p.measurementSource.isNotEmpty(),"Calibration metadata is incomplete");
    juce::Array<juce::var> bands;for(const auto& b:p.bands){auto* v=new juce::DynamicObject();v->setProperty("type",filterName(b.type));v->setProperty("hz",b.hz);v->setProperty("q",b.q);v->setProperty("db",b.db);bands.add(juce::var(v));}object->setProperty("bands",bands);
    juce::TemporaryFile temporary(file);require(temporary.getFile().replaceWithText(juce::JSON::toString(root,false))&&temporary.overwriteTargetFileWithTemporary(),"Cannot save calibration profile");
}
MonitorProfileLoader::MonitorProfileLoader(Ready r,Error e):juce::Thread("HEM calibration loader"),ready(std::move(r)),errorHandler(std::move(e)){startTimerHz(15);}
MonitorProfileLoader::~MonitorProfileLoader(){stopTimer();signalThreadShouldExit();waitForThreadToExit(-1);}
bool MonitorProfileLoader::load(const juce::File& f,double sr,unsigned gen){if(busy.load())return false;file=f;rate=sr;generation=gen;error.clear();done=false;busy=true;if(!startThread()){busy=false;errorHandler("Cannot start calibration worker");return false;}return true;}
void MonitorProfileLoader::run(){try{profile=MonitorProfile::load(file);curve=profile.prepare(rate);}catch(const std::exception& e){error=e.what();}done.store(true,std::memory_order_release);}
void MonitorProfileLoader::timerCallback(){if(!done.load(std::memory_order_acquire)||isThreadRunning())return;done=false;busy=false;if(error.isNotEmpty())errorHandler(error);else ready(profile,curve,generation);}
}
