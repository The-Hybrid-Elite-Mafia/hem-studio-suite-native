#include "../Source/CoreMasterStage.h"
#include <cstdlib>
#include <iostream>
#include <new>

static bool watchAllocations = false;
static size_t allocationCount = 0;
void* operator new(std::size_t bytes) {
    if (watchAllocations) ++allocationCount;
    if (auto* p = std::malloc(bytes == 0 ? 1 : bytes)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
static void require(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}
int main() {
    try {
        for (double rate : {44100., 48000., 96000.}) {
            constexpr int block = 257;
            hem::CoreMasterStage adapter;
            hem::MasterTruePeakLimiter reference;
            adapter.prepare(rate, block);
            reference.prepare(rate, block, 2);
            require(adapter.latencySamples() == reference.latency(), "Latency mismatch");
            if (rate == 48000.) require(adapter.latencySamples() == 253, "48k baseline latency changed");
            juce::AudioBuffer<float> expected(2, block), actual(2, block);
            int64_t time = 0;
            for (int k = 0; k < 180; ++k) {
                const int n = (k % 3 == 0 ? 1 : (k % 3 == 1 ? 127 : block));
                for (int i = 0; i < n; ++i, ++time) {
                    const float level = k < 10 ? .02f : 1.8f;
                    const float left = level * std::sin(float(2 * juce::MathConstants<double>::pi * 997 * time / rate));
                    const float right = -.63f * left;
                    expected.setSample(0, i, left); expected.setSample(1, i, right);
                    actual.setSample(0, i, left); actual.setSample(1, i, right);
                }
                if (k == 20) {
                    expected.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
                    actual.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
                }
                reference.process(expected, n);
                watchAllocations = true;
                adapter.process(actual.getWritePointer(0), actual.getWritePointer(1), n);
                watchAllocations = false;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        require(actual.getSample(ch, i) == expected.getSample(ch, i), "Adapter changed baseline samples");
                require(std::isfinite(adapter.samplePeakDb()), "Invalid sample-peak meter");
            }
            require(allocationCount == 0, "Adapter allocates in processing");
            juce::AudioBuffer<float> oversized(2, block + 1);
            oversized.clear(); oversized.setSample(0, 0, 1);
            adapter.process(oversized.getWritePointer(0), oversized.getWritePointer(1), block + 1);
            require(oversized.getMagnitude(0, block + 1) == 0, "Oversized block was not safely silenced");
            std::cout << "PASS " << rate << " Hz: exact Task2B output, actual latency "
                      << adapter.latencySamples() << ", irregular blocks, stereo, NaN, sample meter, zero new/new[]\n";
        }
        std::cout << "PASS: unchanged baseline integration only; this test does not certify reconstructed true peaks.\n";
        return 0;
    } catch (const std::exception& e) {
        watchAllocations = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
