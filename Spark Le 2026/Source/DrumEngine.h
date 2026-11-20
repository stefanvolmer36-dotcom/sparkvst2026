#pragma once

#include <JuceHeader.h>

class DrumEngine
{
public:
    struct Macro
    {
        float volume = 0.8f;
        float drive = 0.22f;
        float decay = 0.45f;
        float tune = 1.0f;
        float tone = 0.55f;
        float space = 0.3f;
    };

    void prepare(double newSampleRate);
    void trigger(int pad, float velocity);
    void setPressure(int pad, float amount);
    void setMacro(const Macro& newMacro);
    void tick(float& left, float& right);

private:
    struct Voice
    {
        bool alive = false;
        float vel = 0.0f;
        int age = 0;
        float env = 0.0f;
        float pitchEnv = 0.0f;
        float click = 0.0f;
        float toneEnv = 0.0f;
        float noiseEnv = 0.0f;
        float phase[6] {};
        float z1 = 0.0f;
        float z2 = 0.0f;
        float z3 = 0.0f;
        float pressure = 0.0f;
    };

    float noise();
    float coeff(float hz) const;
    void renderVoice(int pad, float& left, float& right);

    double sampleRate = 44100.0;
    uint32_t rng = 0xA341316Cu;
    Macro macro;
    Voice voices[16];
    float dcInL = 0.0f, dcOutL = 0.0f, dcInR = 0.0f, dcOutR = 0.0f;
};
