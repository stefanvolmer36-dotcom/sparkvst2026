#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "DrumEngine.h"

class SparkLEAudioProcessor : public juce::AudioProcessor
{
public:
    SparkLEAudioProcessor();
    ~SparkLEAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SparkLE Drum Engine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    enum class UiKind { note, macro, transport, clear };

    struct UiEvent
    {
        UiKind kind;
        int id = 0;
        float value = 0.0f;
    };

    void pushUi(UiEvent event);
    void publishParameters();

    struct View
    {
        std::array<float, 16> pads {};
        std::array<int, 16> steps {};
        int playhead = 0;
        int bank = 0;
        float drive = 0.22f;
        float decay = 0.45f;
        float tune = 1.0f;
        float x = 0.55f;
        float y = 0.30f;
        float volume = 0.8f;
        float bpm = 124.0f;
        bool playing = false;
        bool recording = false;
        bool loop = true;
        float meter = 0.0f;
    };

    View view() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void pullHostParameters();
    void handleMessage(const juce::MidiMessage& message);
    void triggerPad(int pad, float velocity);
    void fireStep(int step);
    void renderRange(float* left, float* right, int start, int count, double clockBpm, bool followHost, double ppqOrigin);
    void applyTransport(int id, float value);
    void setMacroAtomic(int id, float actual, bool fromRelative);
    float macroValue(int id) const;
    void writePatternCell(int step, int pad, uint8_t velocity);

    DrumEngine engine;
    juce::AbstractFifo uiFifo { 256 };
    std::array<UiEvent, 256> uiEvents {};

    std::atomic<float> volume { 0.80f };
    std::atomic<float> drive { 0.22f };
    std::atomic<float> decay { 0.45f };
    std::atomic<float> tune { 1.0f };
    std::atomic<float> xyX { 0.55f };
    std::atomic<float> xyY { 0.30f };
    std::atomic<float> bpm { 124.0f };
    std::atomic<uint32_t> revision { 0 };
    std::atomic<uint32_t> hostSeen { 0 };
    uint32_t publishedRevision = 0;

    std::array<std::atomic<uint8_t>, 256> pattern {};
    std::array<std::atomic<float>, 16> padGlow {};
    std::atomic<int> playhead { 0 };
    std::atomic<int> bank { 0 };
    std::atomic<bool> recording { false };
    std::atomic<bool> loopEnabled { true };
    std::atomic<bool> sequencerEnabled { true };
    std::atomic<bool> internalRunning { false };
    std::atomic<bool> hostPlaying { false };
    std::atomic<float> activeBpm { 124.0f };
    std::atomic<float> meter { 0.0f };

    double sampleRate = 44100.0;
    double stepCursor = 0.0;
    int lastHostStep = -1;
    bool blockHostPlaying = false;
    double blockHostBpm = 124.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SparkLEAudioProcessor)
};
