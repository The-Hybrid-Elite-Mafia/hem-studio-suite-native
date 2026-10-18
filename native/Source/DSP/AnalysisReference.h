#pragma once
#include <cstdint>
namespace hem::analysis {
struct ReferenceBlock {const float*left=nullptr;const float*right=nullptr;int samples=0;double sampleRate=0;std::uint64_t timeline=0;bool valid=false;};
// Optional host capability, with no dependency on any concrete plugin/editor.
// The host borrows independent reference pointers for the immediately following
// audio callback only. Reference and processed data must have identical timeline.
class ReferenceConsumer {public:virtual ~ReferenceConsumer()=default;virtual void setReferenceBlock(ReferenceBlock)noexcept=0;};
}
