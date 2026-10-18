#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace hem::reference_match {
constexpr size_t bins = 96, sections = 16;
enum class Status { ready, invalidInput, insufficientSignal, discontinuity };
struct Profile {
    std::array<double, bins> hz{}, power{};
    double sampleRate = 0, meanSquare = 0;
    uint64_t frames = 0, generation = 0, startFrame = 0;
    Status status = Status::insufficientSignal;
};
// Worker-only. Caller supplies contiguous PRE-MATCH PCM; channel powers are
// accumulated independently (anti-phase stereo does not cancel). Hard 30 s cap.
Profile analyze(const float* const* channels, size_t channelCount, size_t frames,
                double sampleRate, uint64_t generation = 0, uint64_t lostFrames = 0);
struct Coefficients { double b0=1,b1=0,b2=0,a1=0,a2=0; };
struct Solution {
    std::array<Coefficients, sections> filters{};
    std::array<double, sections> hz{}, gainDb{};
    std::array<double, bins> targetDb{};
    double sampleRate=0, amount=0, smoothingOctaves=0;
    double tailSeconds=0; // Conservative cascade impulse l1 tail below 1e-9.
    bool valid=false;
    double responseDb(double frequency) const noexcept;
};
// Worker-only fit. Independently normalizes each profile by measured broadband
// power, interpolates in physical Hz, gates unobserved bins, regularizes fit.
Solution fit(const Profile& source, const Profile& reference, double outputRate,
             double amount = 1, double smoothingOctaves = 0.5);
// Callback-owned state, no allocation, locks, I/O, or shared ownership.
// publish must be called by its owner on the callback or under callback exclusion.
// New immutable solution copied in, old/new states crossfaded for 256 samples.
class Processor {
public:
    void reset() noexcept;
    void publish(const Solution&) noexcept;
    void process(float* left, float* right, size_t frames, float outputDb=0, bool bypass=false) noexcept;
private:
    struct Bank {
        Solution solution{};
        std::array<std::array<std::array<double,2>,sections>,2> state{};
        double tick(double, size_t) noexcept;
    } current{}, previous{};
    unsigned remaining=0;
    double outputGain=1, bypassMix=0;
};
}
