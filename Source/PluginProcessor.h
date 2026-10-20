#pragma once
#include <JuceHeader.h>
#include "Engine.h"
#include "Samples.h"

class Digitakt2Processor : public juce::AudioProcessor
{
public:
    Digitakt2Processor();
    ~Digitakt2Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- called by the editor (message thread) ----
    void setStateJson (const juce::String& json);
    juce::String getStateJson() const { return stateJson; }
    void addUserSample (const juce::String& id, const juce::String& name, std::vector<float> L, std::vector<float> R);
    std::vector<dt2::SamplePtr> getUserSamples() const;
    dt2::Engine& getEngine() { return engine; }
    std::atomic<int> stateVersion { 0 }; // bumped when the DAW restores a project

private:
    static juce::AudioProcessor::BusesProperties makeBuses()
    {
        auto b = juce::AudioProcessor::BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
        for (int t = 1; t <= 16; ++t)
            b = b.withOutput ("Track " + juce::String (t), juce::AudioChannelSet::stereo(), false);
        return b;
    }

    dt2::Engine engine;
    dt2::Pool factory;
    std::shared_ptr<const dt2::Pool> pool;
    juce::String stateJson;
    int blockSize = 512;
    juce::
