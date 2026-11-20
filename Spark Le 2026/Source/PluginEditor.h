#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SparkLEEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit SparkLEEditor(SparkLEAudioProcessor&);
    ~SparkLEEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Browser;

    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> loadResource(const juce::String& url) const;
    void onControl(const juce::var& payload);

    SparkLEAudioProcessor& processor;
    std::unique_ptr<Browser> browser;
    juce::Label fallback;
    bool pageReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SparkLEEditor)
};
