#pragma once
// Independent double recurrence reference. Does not call production DSP,
// access production coefficients, or use production ramps/detector helpers.
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>
namespace reference {
constexpr double pi=3.1415926535897932384626433832795;
using Settings=std::array<float,10>;
inline double gain(double d){return std::pow(10.,d/20.);}
struct Linear {
 float current=0,target=0,step=0;int left=0,length=1;
 void init(double fs,float v){current=target=v;left=0;length=std::max(1,int(fs*.01));}
 void set(float v){if(v!=target){target=v;left=length;step=(v-current)/float(length);}}
 float tick(){if(left){current+=step;--left;if(!left)current=target;}return current;}
};
struct Filter {
 std::array<std::array<double,4>,2> h{};double b0=1,b1=0,b2=0,a1=0,a2=0;
 void setup(int mode,double fs,double f,double q){
  const double angle=2*pi*std::clamp(f,1.,.45*fs)/fs,cosine=std::cos(angle),alpha=std::sin(angle)/(2*q),inv=1/(1+alpha);
  a1=-2*cosine*inv;a2=(1-alpha)*inv;
  if(mode==0){b0=(1+cosine)*.5*inv;b1=-(1+cosine)*inv;b2=b0;}
  else if(mode==1){b0=(1-cosine)*.5*inv;b1=(1-cosine)*inv;b2=b0;}
  else {b0=b2=inv;b1=-2*cosine*inv;}
 }
 double tick(double x,size_t channel){auto& s=h[channel];const double y=b0*x+s[0];const double next=b1*x-a1*y+s[1];s[1]=b2*x-a2*y;s[0]=next;return y;}
};
class Engine {
public:
 int kind,count,phase=0;double fs,env=0,peak=0,fast=0,slow=0;bool opened=false;
 uint64_t elapsed=0,below=0;Settings target{};
 std::array<Linear,10> controls{};std::array<Linear,16> active{};std::array<Filter,16> filters{};
 std::array<double,2> xPrevious{},yPrevious{},energy{};
 Engine(int k,double rate,Settings p):kind(k),count(k==1?10:(k==2||k==3?8:6)),fs(rate),target(p){
  for(int i=0;i<count;++i)controls[size_t(i)].init(fs,p[size_t(i)]);
  for(int i=0;i<16;++i)active[size_t(i)].init(fs,i<int(std::lround(p[2]))?1.f:0.f);
  coefficients(p);
 }
 void set(Settings p){target=p;for(int i=0;i<count;++i)if(!((kind==1&&i==5)||(kind==4&&i==2)))controls[size_t(i)].set(p[size_t(i)]);
  if(kind==4)for(int i=0;i<16;++i)active[size_t(i)].set(i<int(std::lround(p[2]))?1.f:0.f);}
 void coefficients(Settings p){
  if(kind==0){filters[0].setup(0,fs,p[2],1/std::sqrt(2.));filters[1].setup(0,fs,p[2],1/std::sqrt(2.));}
  if(kind==3)filters[0].setup(1,fs,p[0],1/std::sqrt(2.));
  if(kind==4)for(int i=0;i<16;++i)filters[size_t(i)].setup(2,fs,(i+1)*double(p[0]),p[1]);
 }
 std::array<float,2> tick(float l,float r){
  Settings p{};for(int i=0;i<count;++i)p[size_t(i)]=controls[size_t(i)].tick();
  if(phase==0)coefficients(p);phase=(phase+1)%16;
  std::array<double,2> x{l,r},y=x;
  for(size_t c=0;c<2;++c)if(!std::isfinite(x[c])){x[c]=y[c]=0;xPrevious[c]=yPrevious[c]=energy[c]=0;for(auto& f:filters)f.h[c]={};}
  if(kind==0){const double pole=std::exp(-2*pi*5/fs);for(size_t c=0;c<2;++c){
   const double dc=(1+pole)/2*(x[c]-xPrevious[c])+pole*yPrevious[c];xPrevious[c]=x[c];yPrevious[c]=dc;
   const double input=x[c]*(1-p[0])+dc*p[0],one=filters[0].tick(input,c),two=filters[1].tick(one,c);
   y[c]=input+p[1]*(one+p[3]*(two-one)-input);
  }}
  if(kind==1){peak=std::max({std::abs(x[0]),std::abs(x[1]),peak*std::exp(-1/(fs*.05))});
   const double d=20*std::log10(std::max(peak,1.e-12)),close=p[0]-p[1];
   if(d>=p[0]){opened=true;below=0;}else if(d<=close){++below;if(below>=std::max(1.,std::ceil(fs*target[5]/1000.)))opened=false;}else below=0;
   const double expansion=std::min(double(p[3]),(p[2]-1)*std::max(0.,close-d));
   const double requested=opened?0:expansion+(p[3]-expansion)*p[7],time=requested<env?p[4]:p[6];
   const double alpha=std::exp(-1000/(fs*time));env=alpha*env+(1-alpha)*requested;
   y={x[0]*gain(-env),x[1]*gain(-env)};
  }
  if(kind==2){const double alpha=std::exp(-10/fs);for(size_t c=0;c<2;++c)energy[c]=alpha*energy[c]+(1-alpha)*x[c]*x[c];
   const double level=10*std::log10(std::max({1.e-24,energy[0],energy[1]}));
   const double requested=elapsed<uint64_t(std::ceil(fs*.1))||level<=p[5]?0:std::clamp(double(p[0])-level,-double(p[2]),double(p[1]));
   const double a=std::exp(-1000/(fs*(requested>env?p[3]:p[4])));env=a*env+(1-a)*requested;y={x[0]*gain(env),x[1]*gain(env)};
  }
  if(kind==3){const std::array<double,2> low{filters[0].tick(x[0],0),filters[0].tick(x[1],1)};
   const double power=std::max(low[0]*low[0],low[1]*low[1]),af=std::exp(-200/fs),as=std::exp(-4/fs);
   fast=af*fast+(1-af)*power;slow=as*slow+(1-as)*power;
   const double lf=10*std::log10(std::max(1.e-24,fast)),ls=10*std::log10(std::max(1.e-24,slow));
   const double severity=std::clamp((lf-ls-p[2])/6.,0.,1.),request=std::min(double(p[3]),std::max(0.,lf-p[1]))*severity;
   const double a=std::exp(-1000/(fs*(request>env?p[4]:p[5])));env=a*env+(1-a)*request;
   y={x[0]-(1-gain(-env))*low[0],x[1]-(1-gain(-env))*low[1]};
  }
  if(kind==4){for(size_t i=0;i<16;++i){const double a=active[i].tick();for(size_t c=0;c<2;++c){const double f=filters[i].tick(y[c],c);y[c]=(1-a)*y[c]+a*f;}}
   for(size_t c=0;c<2;++c)y[c]=x[c]+(1-gain(-p[3]))*(y[c]-x[c]);
  }
  ++elapsed;const double mix=p[size_t(count-2)]/100.,out=gain(p[size_t(count-1)]);
  return {float(out*(x[0]+mix*(y[0]-x[0]))),float(out*(x[1]+mix*(y[1]-x[1])))};
 }
};
}
