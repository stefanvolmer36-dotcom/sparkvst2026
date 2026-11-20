#include "DrumEngine.h"

namespace
{
    struct Spec
    {
        int model;
        float pitch;
        float decay;
        int variant;
    };

    constexpr Spec specs[16] = {
        { 0, 1.00f, 1.00f, 0 },
        { 1, 1.00f, 1.00f, 0 },
        { 2, 1.00f, 1.00f, 0 },
        { 3, 1.00f, 0.22f, 0 },
        { 3, 0.94f, 1.20f, 1 },
        { 4, 0.70f, 1.15f, 0 },
        { 4, 1.12f, 0.90f, 0 },
        { 4, 1.68f, 0.70f, 0 },
        { 0, 1.38f, 0.52f, 1 },
        { 1, 1.28f, 0.58f, 1 },
        { 2, 1.18f, 0.42f, 1 },
        { 3, 1.22f, 0.12f, 2 },
        { 5, 1.00f, 0.40f, 0 },
        { 5, 1.55f, 0.22f, 1 },
        { 6, 1.00f, 1.35f, 0 },
        { 7, 1.00f, 1.00f, 0 },
    };

    constexpr float hatRatios[6] = { 2.00f, 3.00f, 4.16f, 5.43f, 6.79f, 8.21f };

    inline float wrap(float phase)
    {
        return phase - std::floor(phase);
    }
}

void DrumEngine::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
}

void DrumEngine::setPressure(int pad, float amount)
{
    if (pad >= 0 && pad < 16)
        voices[pad].pressure = juce::jlimit(0.0f, 1.0f, amount);
}

void DrumEngine::setMacro(const Macro& newMacro)
{
    macro = newMacro;
}

void DrumEngine::trigger(int pad, float velocity)
{
    if (pad < 0 || pad >= 16)
        return;

    auto& voice = voices[pad];
    voice.alive = true;
    voice.vel = juce::jlimit(0.05f, 1.0f, velocity);
    voice.age = 0;
    voice.env = 1.0f;
    voice.pitchEnv = 1.0f;
    voice.click = 1.0f;
    voice.toneEnv = 1.0f;
    voice.noiseEnv = 1.0f;
    voice.z1 = voice.z2 = voice.z3 = 0.0f;

    const bool tonal = specs[pad].model == 0 || specs[pad].model == 4;
    for (float& phase : voice.phase)
        phase = tonal ? 0.0f : noise() * 0.5f + 0.5f;
}

float DrumEngine::noise()
{
    rng = rng * 1664525u + 1013904223u;
    return ((int) (rng >> 16) & 65535) / 32768.0f - 1.0f;
}

float DrumEngine::coeff(float hz) const
{
    const float limited = juce::jlimit(30.0f, (float) sampleRate * 0.45f, hz);
    return 1.0f - std::exp(-juce::MathConstants<float>::twoPi * limited / (float) sampleRate);
}

void DrumEngine::renderVoice(int pad, float& left, float& right)
{
    auto& v = voices[pad];
    if (!v.alive)
        return;

    const auto& spec = specs[pad];
    const float sr = (float) sampleRate;
    const float tune = juce::jlimit(0.25f, 4.0f, macro.tune * spec.pitch);
    const float vel = 0.28f + 0.72f * v.vel;
    float mono = 0.0f;
    float side = 0.0f;

    switch (spec.model)
    {
        case 0:
        {
            const float f0 = (spec.variant == 0 ? 46.0f : 62.0f) * tune;
            const float f = f0 + f0 * (5.2f + macro.drive * 2.4f) * v.pitchEnv * v.pitchEnv;
            v.phase[0] = wrap(v.phase[0] + f / sr);
            const float sine = std::sin(juce::MathConstants<float>::twoPi * v.phase[0]);
            const float bodyDec = (spec.variant == 0 ? 0.16f : 0.07f) + macro.decay * (spec.variant == 0 ? 0.72f : 0.28f);
            v.pitchEnv *= std::exp(-1.0f / (sr * (0.030f + macro.tone * 0.02f)));
            v.env *= std::exp(-1.0f / (sr * bodyDec * spec.decay));
            v.click *= std::exp(-1.0f / (sr * 0.0032f));
            mono = std::tanh(sine * (1.25f + macro.drive * 2.2f) + noise() * v.click * 0.65f) * v.env;
            if (v.env < 0.0008f)
                v.alive = false;
            break;
        }
        case 1:
        {
            v.phase[0] = wrap(v.phase[0] + (180.0f * tune) / sr);
            v.phase[1] = wrap(v.phase[1] + (328.0f * tune) / sr);
            const float tone = std::sin(juce::MathConstants<float>::twoPi * v.phase[0]) * 0.62f
                             + std::sin(juce::MathConstants<float>::twoPi * v.phase[1]) * 0.38f;
            const float raw = noise();
            const float hp = raw - (v.z1 += coeff(800.0f + macro.tone * 1800.0f) * (raw - v.z1));
            v.z2 += coeff(1800.0f + macro.tone * 5200.0f) * (hp - v.z2);
            const float toneDec = (0.055f + macro.decay * 0.16f) * spec.decay;
            const float noiseDec = (0.09f + macro.decay * 0.34f) * spec.decay * (1.0f + v.pressure);
            v.toneEnv *= std::exp(-1.0f / (sr * toneDec));
            v.noiseEnv *= std::exp(-1.0f / (sr * noiseDec));
            v.click *= std::exp(-1.0f / (sr * 0.004f));
            mono = std::tanh((tone * v.toneEnv * 0.7f + v.z2 * v.noiseEnv * (0.9f + macro.drive) + noise() * v.click * 0.35f)
                             * (1.05f + macro.drive * 1.4f));
            side = v.z2 * v.noiseEnv * macro.space * 0.18f;
            if (v.toneEnv < 0.001f && v.noiseEnv < 0.001f)
                v.alive = false;
            break;
        }
        case 2:
        {
            const float t = v.age / sr;
            float bursts = 0.0f;
            const float times[4] = { 0.0f, 0.011f, 0.021f, 0.032f };
            for (float burst : times)
                if (t >= burst)
                    bursts += std::exp(-(t - burst) / (spec.variant == 0 ? 0.012f : 0.007f));

            const float tailT = (spec.variant == 0 ? 0.10f : 0.045f) + macro.decay * 0.26f;
            const float tail = t > 0.03f ? std::exp(-(t - 0.03f) / tailT) : 0.0f;
            const float env = bursts * 0.38f + tail;
            const float raw = noise();
            const float hp = raw - (v.z1 += coeff(650.0f + macro.tone * 500.0f) * (raw - v.z1));
            v.z2 += coeff(1200.0f + macro.tone * 2200.0f) * (hp - v.z2);
            mono = std::tanh(v.z2 * env * (1.35f + macro.drive * 1.8f));
            side = mono * macro.space * 0.22f;
            if (t > tailT * 5.5f + 0.08f)
                v.alive = false;
            break;
        }
        case 3:
        case 6:
        case 7:
        {
            const float base = (spec.model == 6 ? 168.0f : spec.model == 7 ? 210.0f : (spec.variant == 2 ? 360.0f : spec.variant == 1 ? 292.0f : 328.0f)) * tune;
            float sum = 0.0f;
            for (int i = 0; i < 6; ++i)
            {
                v.phase[i] = wrap(v.phase[i] + base * hatRatios[i] / sr);
                sum += v.phase[i] < 0.5f ? 1.0f : -1.0f;
            }
            sum /= 6.0f;
            const float raw = sum + noise() * (spec.model == 7 ? 0.35f : 0.08f);
            const float cut = (spec.model == 6 ? 2400.0f : 4800.0f) + macro.tone * (spec.model == 6 ? 3500.0f : 7000.0f);
            const float c = coeff(cut);
            const float hp1 = raw - (v.z1 += c * (raw - v.z1));
            const float hp2 = hp1 - (v.z2 += c * (hp1 - v.z2));
            float dec = 0.045f + macro.decay * 0.08f;
            if (spec.variant == 1 || spec.model == 6)
                dec = 0.22f + macro.decay * 1.15f;
            else if (spec.variant == 2)
                dec = 0.018f + macro.decay * 0.035f;
            else if (spec.model == 7)
                dec = 0.35f + macro.decay * 1.25f;
            v.env *= std::exp(-1.0f / (sr * dec * spec.decay * (1.0f + v.pressure * 1.8f)));
            v.click *= std::exp(-1.0f / (sr * 0.008f));
            mono = std::tanh((hp2 + noise() * v.click * 0.3f) * (1.15f + macro.drive * 1.3f)) * v.env;
            side = mono * (0.12f + macro.space * 0.55f);
            if (v.env < 0.0008f)
                v.alive = false;
            break;
        }
        case 4:
        {
            const float f0 = 78.0f * tune;
            const float f = f0 + f0 * 3.4f * v.pitchEnv * v.pitchEnv;
            v.phase[0] = wrap(v.phase[0] + f / sr);
            const float sine = std::sin(juce::MathConstants<float>::twoPi * v.phase[0]);
            const float dec = (0.12f + macro.decay * 0.55f) * spec.decay;
            v.pitchEnv *= std::exp(-1.0f / (sr * 0.055f));
            v.env *= std::exp(-1.0f / (sr * dec));
            v.noiseEnv *= std::exp(-1.0f / (sr * 0.045f));
            mono = std::tanh((sine + noise() * v.noiseEnv * 0.18f) * (1.15f + macro.drive)) * v.env;
            if (v.env < 0.001f)
                v.alive = false;
            break;
        }
        default:
        {
            v.phase[0] = wrap(v.phase[0] + (420.0f * tune) / sr);
            v.phase[1] = wrap(v.phase[1] + (910.0f * tune) / sr);
            const float tone = std::sin(juce::MathConstants<float>::twoPi * v.phase[0]) * 0.45f
                             + std::sin(juce::MathConstants<float>::twoPi * v.phase[1]) * 0.55f;
            const float dec = (spec.variant == 0 ? 0.055f : 0.028f) + macro.decay * 0.04f;
            v.env *= std::exp(-1.0f / (sr * dec));
            v.click *= std::exp(-1.0f / (sr * 0.006f));
            mono = std::tanh((tone * v.env + noise() * v.click) * (1.2f + macro.drive));
            if (v.env < 0.001f)
                v.alive = false;
            break;
        }
    }

    ++v.age;
    const float gain = vel * (0.72f + 0.28f * v.vel);
    left += (mono + side) * gain;
    right += (mono - side) * gain;
}

void DrumEngine::tick(float& left, float& right)
{
    float l = 0.0f;
    float r = 0.0f;
    for (int i = 0; i < 16; ++i)
        renderVoice(i, l, r);

    const float drive = 0.58f + macro.drive * 0.55f;
    l = std::tanh(l * drive);
    r = std::tanh(r * drive);

    const float yL = l - dcInL + 0.9955f * dcOutL;
    dcInL = l;
    dcOutL = yL;
    const float yR = r - dcInR + 0.9955f * dcOutR;
    dcInR = r;
    dcOutR = yR;

    const float vol = juce::jlimit(0.0f, 1.0f, macro.volume);
    left = yL * vol;
    right = yR * vol;
}
