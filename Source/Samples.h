// Sample pool: the 24 factory sounds (same synthesis as the web panel) + user samples from the panel.
#pragma once
#include <JuceHeader.h>
#include <memory>
#include <vector>

namespace dt2
{
constexpr double SAMPLE_RATE = 48000.0; // all pool samples are 48 kHz stereo
constexpr int FACTORY_COUNT = 24;

struct Sample
{
    juce::String id, name;
    std::vector<float> L, R;
    int size() const { return (int) L.size(); }
};

using SamplePtr = std::shared_ptr<const Sample>;
using Pool = std::vector<SamplePtr>;

/** synthesizes the factory sounds (identical algorithms to dt-samples.ts) */
Pool makeFactoryPool();
} // namespace dt2
