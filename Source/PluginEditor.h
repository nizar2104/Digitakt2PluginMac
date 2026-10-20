// Plugin window: the Digitakt II panel (web UI bundled into the plugin) in a native web view.
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class Digitakt2Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Digitakt2Editor (Digitakt2Processor&);
    ~Digitakt2Editor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1c1c1e)); }

private:
    Digitakt2Processor& proc;
    std::unique_ptr<juce::WebBrowserComponent> web;
    bool pageReady = false;
    int seenStateVersion = -1;

    void timerCallback() override;
    void onPanelEvent (const juce::var& v);
    void sendHello();
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Digitakt2Editor)
};
