// Keep test checks enabled in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../Source/UI/SegmentedMeterModel.h"
#include <cassert>
#include <iostream>
using namespace hem::ui;
static void near(float a, float b) { assert(std::abs(a-b) < 0.0001f); }
static void inside(MeterRect r, MeterRect lane) {
    assert(usableMeterRect(r));
    assert(r.x >= lane.x-0.001f && r.y >= lane.y-0.001f);
    assert(r.x+r.width <= lane.x+lane.width+0.001f);
    assert(r.y+r.height <= lane.y+lane.height+0.001f);
}
int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    near(meterDbPosition(-60),0); near(meterDbPosition(-30),0.5f);
    near(meterDbPosition(0),1); near(meterDbPosition(6),1); near(meterDbPosition(-90),0);
    for(float bad : {nan, inf, -inf}) { near(meterDbPosition(bad),0); near(meterAmplitudePosition(bad),0); }
    near(meterAmplitudePosition(0),0); near(meterAmplitudePosition(-1),0);
    near(meterAmplitudePosition(0.001f),0); near(meterAmplitudePosition(1),1);
    near(meterAmplitudePosition(2),1); near(meterAmplitudePosition(std::pow(10.f,-1.5f)),0.5f);
    for(float width : {0.25f, 1.f, 3.f, 8.f, 26.f})
      for(float height : {0.25f, 1.f, 2.f, 3.f, 7.f, 37.f, 149.f, 300.f})
       for(int count : {-99,0,1,10,24,256,1000000})
        for(bool reduction : {false,true}) {
            const MeterRect lane{2,3,width,height};
            auto full = meterSegments(lane,1,true,reduction,count);
            assert(full.count >= 1 && full.count <= 256);
            float lastY = full.segment(0).y;
            for(int i=0;i<full.count;++i) {
                inside(full.segment(i),lane); assert(full.lit(i));
                if(i) assert(reduction ? full.segment(i).y > lastY : full.segment(i).y < lastY);
                lastY = full.segment(i).y;
            }
            assert(!full.lit(-1) && !full.lit(full.count));
            assert(!usableMeterRect(full.segment(-1)));
            for(float position : {-1.f,0.f,nan,inf,-inf}) {
                auto empty = meterSegments(lane,position,true,reduction,count);
                for(int i=0;i<empty.count;++i) assert(!empty.lit(i));
            }
            auto invalid = meterSegments(lane,1,false,reduction,count);
            for(int i=0;i<invalid.count;++i) assert(!invalid.lit(i));
            for(float p : {0.01f,0.5f,1.f,2.f}) {
                auto rms = meterMarks(lane,p,true,MeterMarkRole::rms,1.5f);
                auto hold = meterMarks(lane,p,true,MeterMarkRole::peakHold,1.f);
                assert(rms.visible && hold.visible);
                inside(rms.first,lane); inside(rms.highlight,lane);
                inside(hold.first,lane); inside(hold.second,lane);
                assert(hold.first.x+hold.first.width < rms.first.x);
                assert(rms.first.x+rms.first.width < hold.second.x);
            }
        }
    const MeterRect lane{0,0,20,200};
    for(float p : {0.f,-1.f,nan,inf,-inf}) assert(!meterMarks(lane,p,true,MeterMarkRole::rms).visible);
    assert(!meterMarks(lane,1,false,MeterMarkRole::rms).visible);
    assert(!meterMarks(lane,1,true,MeterMarkRole::rms,nan).visible);
    for(MeterRect bad : {MeterRect{0,0,0,10},{0,0,10,-1},{nan,0,10,10},{0,inf,10,10},{0,0,inf,10}}) {
        assert(meterSegments(bad,1,true).count==0);
        assert(!meterMarks(bad,1,true,MeterMarkRole::rms).visible);
    }
    auto half = meterSegments(lane,0.5f,true,false,24);
    int lit=0; for(int i=0;i<half.count;++i) lit+=half.lit(i); assert(lit==12);
    std::cout << "PASS: calibration, endpoints, invalid/nonfinite samples, segment geometry, compact lanes, coincident marks\n";
}
