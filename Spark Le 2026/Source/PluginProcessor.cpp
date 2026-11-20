#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SparkMap.h"

namespace
{
    constexpr int macroVolume = 0;
    constexpr int macroDrive = 1;
    constexpr int macroDecay = 2;
    constexpr int macroTune = 3;
    constexpr int macroX = 4;
    constexpr int macroY = 5;
    constexpr int macroTempo = 6;

    const char* macroId(int id)
    {
        switch (id)
        {
            case macroVolume: return "volume";
            case macroDrive: return "drive";
            case macroDecay: return "decay";
            case macroTune: return "tune";
            case macroX: return "xyX";
            case macroY: return "xyY";
            case macroTempo: return "bpm";
            default: return nullptr;
        }
    }
}

SparkLEAudioProcessor::SparkLEAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SparkLE", createLayout())
{
    const auto seed = [&](int step, int pad, uint8_t velocity)
    {
        pattern[(size_t) step * 16 + (size_t) pad].store(velocity);
    };

    seed(0, 0, 112);
    seed(4, 0, 108);
    seed(8, 0, 112);
    seed(12, 0, 104);
    seed(4, 1, 102);
    seed(12, 1, 110);
    seed(12, 2, 88);
    for (int step = 0; step < 16; ++step)
        seed(step, 3, step % 4 == 0 ? 96 : 64);
}

juce::AudioProcessorValueTreeState::ParameterLayout SparkLEAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "volume", 1 }, "Volume",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f, 0.6f), 0.80f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "drive", 1 }, "Drive", 0.0f, 1.0f, 0.22f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "decay", 1 }, "Decay", 0.0f, 1.0f, 0.45f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "tune", 1 }, "Tune",
                                                           juce::NormalisableRange<float>(0.5f, 2.0f, 0.0001f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "xyX", 1 }, "Tone", 0.0f, 1.0f, 0.55f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "xyY", 1 }, "Space", 0.0f, 1.0f, 0.30f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "bpm", 1 }, "Tempo",
                                                           juce::NormalisableRange<float>(40.0f, 240.0f, 0.01f), 124.0f));
    return layout;
}

void SparkLEAudioProcessor::prepareToPlay(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    engine.prepare(newSampleRate);
    stepCursor = 0.0;
    lastHostStep = -1;
}

void SparkLEAudioProcessor::releaseResources() {}

bool SparkLEAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SparkLEAudioProcessor::pushUi(UiEvent event)
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    uiFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0)
        uiEvents[(size_t) start1] = event;
    uiFifo.finishedWrite(size1);
}

void SparkLEAudioProcessor::publishParameters()
{
    const uint32_t revisionNow = revision.load();
    if (revisionNow == publishedRevision)
        return;

    for (int id = 0; id < 7; ++id)
    {
        if (auto* parameter = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(macroId(id))))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(macroValue(id)));
    }

    if (revision.load() == revisionNow)
    {
        publishedRevision = revisionNow;
        hostSeen.store(revisionNow);
    }
}

float SparkLEAudioProcessor::macroValue(int id) const
{
    switch (id)
    {
        case macroVolume: return volume.load();
        case macroDrive: return drive.load();
        case macroDecay: return decay.load();
        case macroTune: return tune.load();
        case macroX: return xyX.load();
        case macroY: return xyY.load();
        case macroTempo: return bpm.load();
        default: return 0.0f;
    }
}

void SparkLEAudioProcessor::pullHostParameters()
{
    if (revision.load() != hostSeen.load())
        return;

    const auto load = [&](const char* id, std::atomic<float>& target)
    {
        target.store(apvts.getRawParameterValue(id)->load());
    };

    load("volume", volume);
    load("drive", drive);
    load("decay", decay);
    load("tune", tune);
    load("xyX", xyX);
    load("xyY", xyY);
    load("bpm", bpm);
}

void SparkLEAudioProcessor::setMacroAtomic(int id, float actual, bool fromRelative)
{
    std::atomic<float>* target = nullptr;
    float minValue = 0.0f;
    float maxValue = 1.0f;

    switch (id)
    {
        case macroVolume: target = &volume; break;
        case macroDrive: target = &drive; break;
        case macroDecay: target = &decay; break;
        case macroTune: target = &tune; minValue = 0.5f; maxValue = 2.0f; break;
        case macroX: target = &xyX; break;
        case macroY: target = &xyY; break;
        case macroTempo: target = &bpm; minValue = 40.0f; maxValue = 240.0f; break;
        default: return;
    }

    float next = actual;
    if (fromRelative)
    {
        const float current = target->load();
        const float normalised = (current - minValue) / (maxValue - minValue);
        const float delta = id == macroTempo ? actual / (maxValue - minValue) : actual / 100.0f;
        next = minValue + juce::jlimit(0.0f, 1.0f, normalised + delta) * (maxValue - minValue);
    }

    target->store(juce::jlimit(minValue, maxValue, next));
    revision.fetch_add(1);
}

void SparkLEAudioProcessor::writePatternCell(int step, int pad, uint8_t velocity)
{
    if (step < 0 || step >= spark::stepCount || pad < 0 || pad >= spark::padCount)
        return;
    pattern[(size_t) step * spark::padCount + (size_t) pad].store(velocity);
}

void SparkLEAudioProcessor::triggerPad(int pad, float velocity)
{
    if (pad < 0 || pad >= spark::padCount)
        return;

    engine.trigger(pad, velocity);
    padGlow[(size_t) pad].store(velocity);

    if (recording.load() && (blockHostPlaying || internalRunning.load()))
        writePatternCell(playhead.load(), pad, (uint8_t) juce::jlimit(1, 127, (int) std::round(velocity * 127.0f)));
}

void SparkLEAudioProcessor::fireStep(int step)
{
    playhead.store(step);
    for (int pad = 0; pad < spark::padCount; ++pad)
    {
        const auto velocity = pattern[(size_t) step * spark::padCount + (size_t) pad].load();
        if (velocity > 0)
            triggerPad(pad, velocity / 127.0f);
    }
}

void SparkLEAudioProcessor::applyTransport(int id, float value)
{
    switch (id)
    {
        case 0:
            sequencerEnabled.store(true);
            if (!blockHostPlaying)
                internalRunning.store(true);
            break;
        case 1:
            sequencerEnabled.store(false);
            internalRunning.store(false);
            stepCursor = 0.0;
            playhead.store(0);
            lastHostStep = -1;
            break;
        case 2:
            recording.store(!recording.load());
            break;
        case 3:
            if (!blockHostPlaying)
                playhead.store((playhead.load() + spark::stepCount - 1) % spark::stepCount);
            break;
        case 4:
            if (!blockHostPlaying)
                playhead.store((playhead.load() + 1) % spark::stepCount);
            break;
        case 5:
            loopEnabled.store(!loopEnabled.load());
            break;
        case 6:
            bank.store(value >= 0.5f ? 1 : 0);
            break;
        default:
            break;
    }
}

void SparkLEAudioProcessor::handleMessage(const juce::MidiMessage& message)
{
    if (message.isSysEx())
    {
        const auto size = message.getSysExDataSize();
        const auto* data = message.getSysExData();
        if (size >= 4 && data[0] == 0x7f && data[2] == 0x06)
        {
            switch (data[3])
            {
                case 0x01: applyTransport(1, 0.0f); break;
                case 0x02:
                case 0x03: applyTransport(0, 0.0f); break;
                case 0x04: applyTransport(4, 0.0f); break;
                case 0x05: applyTransport(3, 0.0f); break;
                case 0x06: recording.store(true); sequencerEnabled.store(true); break;
                case 0x07:
                case 0x09: recording.store(false); break;
                default: break;
            }
        }
        return;
    }

    if (message.isMidiStart() || message.isMidiContinue())
    {
        applyTransport(0, 0.0f);
        return;
    }
    if (message.isMidiStop())
    {
        applyTransport(1, 0.0f);
        return;
    }

    if (message.isController())
    {
        const int cc = message.getControllerNumber();
        const int value = message.getControllerValue();
        const auto absolute = value / 127.0f;

        if (cc == spark::ccVolume)
        {
            if (value == 1 || value == 127)
                setMacroAtomic(macroVolume, (float) spark::relative1(value), true);
            else
                setMacroAtomic(macroVolume, absolute, false);
        }
        else if (cc == spark::ccDrive)
            setMacroAtomic(macroDrive, (float) spark::relative1(value), true);
        else if (cc == spark::ccDecay)
            setMacroAtomic(macroDecay, (float) spark::relative1(value), true);
        else if (cc == spark::ccTune)
            setMacroAtomic(macroTune, (float) spark::relative1(value), true);
        else if (cc == spark::ccTempo)
            setMacroAtomic(macroTempo, (float) spark::relative1(value), true);
        else if (cc == spark::ccXyX)
            setMacroAtomic(macroX, absolute, false);
        else if (cc == spark::ccXyY)
            setMacroAtomic(macroY, absolute, false);
        return;
    }

    if (message.isPitchWheel())
        return;

    if (message.isAftertouch())
    {
        const int pad = playhead.load() >= 0 ? -1 : -1;
        juce::ignoreUnused(pad);
        const float pressure = message.getAfterTouchValue() / 127.0f;
        for (int i = 0; i < spark::padCount; ++i)
            if (padGlow[(size_t) i].load() > 0.2f)
                engine.setPressure(i, pressure);
        return;
    }

    if (message.isChannelPressure())
    {
        const float pressure = message.getChannelPressureValue() / 127.0f;
        for (int i = 0; i < spark::padCount; ++i)
            if (padGlow[(size_t) i].load() > 0.2f)
                engine.setPressure(i, pressure);
        return;
    }

    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        const float velocity = message.getVelocity() / 127.0f;

        if (note >= spark::notePlay && note <= spark::noteBankA)
        {
            switch (note)
            {
                case spark::notePlay: applyTransport(0, 0.0f); break;
                case spark::noteStop: applyTransport(1, 0.0f); break;
                case spark::noteRecord: applyTransport(2, 0.0f); break;
                case spark::noteRewind: applyTransport(3, 0.0f); break;
                case spark::noteForward: applyTransport(4, 0.0f); break;
                case spark::noteLoop: applyTransport(5, 0.0f); break;
                case spark::noteBankB: applyTransport(6, 1.0f); break;
                case spark::noteBankA: applyTransport(6, 0.0f); break;
                default: break;
            }
            return;
        }

        int pad = -1;
        if (note >= spark::padNoteFirst && note <= spark::padNoteBankedLast)
            pad = bank.load() * 8 + (note - spark::padNoteFirst);
        else if (note >= 44 && note <= spark::padNoteAbsoluteLast)
            pad = note - spark::padNoteFirst;

        if (pad >= 0)
            triggerPad(pad, velocity);
        return;
    }

    if (message.isNoteOff())
    {
        const int note = message.getNoteNumber();
        int pad = -1;
        if (note >= spark::padNoteFirst && note <= spark::padNoteBankedLast)
            pad = bank.load() * 8 + (note - spark::padNoteFirst);
        else if (note >= 44 && note <= spark::padNoteAbsoluteLast)
            pad = note - spark::padNoteFirst;
        if (pad >= 0)
            engine.setPressure(pad, 0.0f);
    }
}

void SparkLEAudioProcessor::renderRange(float* left, float* right, int start, int count, double clockBpm, bool followHost, double ppqOrigin)
{
    DrumEngine::Macro macro;
    macro.volume = volume.load();
    macro.drive = drive.load();
    macro.decay = decay.load();
    macro.tune = tune.load();
    macro.tone = xyX.load();
    macro.space = xyY.load();
    engine.setMacro(macro);

    const bool runPattern = sequencerEnabled.load() && clockBpm > 1.0;
    const double samplesPerStep = sampleRate * 60.0 / clockBpm / 4.0;

    for (int i = 0; i < count; ++i)
    {
        if (runPattern && followHost)
        {
            const double ppq = ppqOrigin + (start + i) * clockBpm / 60.0 / sampleRate;
            int step = (int) std::floor(ppq * 4.0);
            step %= spark::stepCount;
            if (step < 0)
                step += spark::stepCount;
            if (step != lastHostStep)
            {
                lastHostStep = step;
                fireStep(step);
            }
        }
        else if (runPattern && internalRunning.load())
        {
            stepCursor += 1.0;
            if (stepCursor >= samplesPerStep)
            {
                stepCursor -= samplesPerStep;
                int step = playhead.load() + 1;
                if (step >= spark::stepCount)
                {
                    if (!loopEnabled.load())
                    {
                        internalRunning.store(false);
                        sequencerEnabled.store(false);
                        step = 0;
                    }
                    else
                    {
                        step = 0;
                    }
                }
                if (internalRunning.load() || step == 0)
                    fireStep(step);
            }
        }

        float sampleL = 0.0f, sampleR = 0.0f;
        engine.tick(sampleL, sampleR);
        left[start + i] += sampleL;
        if (right != nullptr)
            right[start + i] += sampleR;
    }

    for (auto& glow : padGlow)
    {
        const float decayed = glow.load() * std::exp(-(float) count / (float) (sampleRate * 0.16));
        glow.store(decayed < 0.004f ? 0.0f : decayed);
    }
}

void SparkLEAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    pullHostParameters();

    blockHostPlaying = false;
    blockHostBpm = bpm.load();
    double ppqOrigin = 0.0;
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            blockHostPlaying = position->getIsPlaying();
            if (auto hostBpm = position->getBpm())
                if (*hostBpm > 1.0)
                    blockHostBpm = *hostBpm;
            if (auto ppq = position->getPpqPosition())
                ppqOrigin = *ppq;
        }
    }

    if (!blockHostPlaying)
        lastHostStep = -1;

    hostPlaying.store(blockHostPlaying);
    activeBpm.store((float) blockHostBpm);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    uiFifo.prepareToRead(uiFifo.getNumReady(), start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i)
    {
        const auto& event = uiEvents[(size_t) (start1 + i)];
        if (event.kind == UiKind::note)
            triggerPad(event.id, event.value);
        else if (event.kind == UiKind::macro)
            setMacroAtomic(event.id, event.value, false);
        else if (event.kind == UiKind::transport)
            applyTransport(event.id, event.value);
        else if (event.kind == UiKind::clear)
            for (auto& cell : pattern)
                cell.store(0);
    }
    uiFifo.finishedRead(size1);

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    const int numSamples = buffer.getNumSamples();
    int rendered = 0;

    for (const auto metadata : midi)
    {
        const int sample = juce::jlimit(0, numSamples, metadata.samplePosition);
        if (sample > rendered)
            renderRange(left, right, rendered, sample - rendered, blockHostBpm, blockHostPlaying, ppqOrigin);
        handleMessage(metadata.getMessage());
        rendered = sample;
    }

    if (rendered < numSamples)
        renderRange(left, right, rendered, numSamples - rendered, blockHostBpm, blockHostPlaying, ppqOrigin);

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = juce::jmax(peak, std::abs(left[i]), right != nullptr ? std::abs(right[i]) : 0.0f);
    const float previous = meter.load();
    meter.store(juce::jmax(peak, previous * 0.86f));
}

SparkLEAudioProcessor::View SparkLEAudioProcessor::view() const
{
    View state;
    for (int i = 0; i < spark::padCount; ++i)
        state.pads[(size_t) i] = padGlow[(size_t) i].load();
    for (int step = 0; step < spark::stepCount; ++step)
    {
        int hit = 0;
        for (int pad = 0; pad < spark::padCount; ++pad)
            if (pattern[(size_t) step * spark::padCount + (size_t) pad].load() > 0)
                hit = 1;
        state.steps[(size_t) step] = hit;
    }
    state.playhead = playhead.load();
    state.bank = bank.load();
    state.drive = drive.load();
    state.decay = decay.load();
    state.tune = tune.load();
    state.x = xyX.load();
    state.y = xyY.load();
    state.volume = volume.load();
    state.bpm = activeBpm.load();
    state.playing = hostPlaying.load() || internalRunning.load();
    state.recording = recording.load();
    state.loop = loopEnabled.load();
    state.meter = meter.load();
    return state;
}

void SparkLEAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::String patternText;
    patternText.preallocateBytes(512);
    for (auto& cell : pattern)
        patternText += juce::String::toHexString(cell.load()).paddedLeft('0', 2);

    state.setProperty("pattern", patternText, nullptr);
    state.setProperty("bank", bank.load(), nullptr);
    state.setProperty("loop", loopEnabled.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SparkLEAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (!state.isValid())
            return;

        apvts.replaceState(state);
        const auto patternText = state.getProperty("pattern").toString();
        for (int i = 0; i < 256 && i * 2 + 1 < patternText.length(); ++i)
        {
            const auto byte = (uint8_t) patternText.substring(i * 2, i * 2 + 2).getHexValue32();
            pattern[(size_t) i].store(byte);
        }
        bank.store((int) state.getProperty("bank", 0));
        loopEnabled.store((bool) state.getProperty("loop", true));
        revision.fetch_add(1);
    }
}

juce::AudioProcessorEditor* SparkLEAudioProcessor::createEditor()
{
    return new SparkLEEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SparkLEAudioProcessor();
}
