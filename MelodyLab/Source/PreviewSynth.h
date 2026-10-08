// MELODY LAB — tiny preview synth (lead / pad / bass) used to audition the generated parts
#pragma once
#include <cmath>
#include <array>
#include <algorithm>

class PreviewSynth
{
public:
    enum Part { Melody = 0, Chords = 1, Bass = 2 };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        allOff (true);
    }

    void noteOn (int part, int pitch, float velocity, double lengthSamples)
    {
        Voice* v = nullptr;
        for (auto& vc : voices) if (! vc.active) { v = &vc; break; }
        if (v == nullptr)                                         // steal the oldest
        {
            v = &voices[0];
            for (auto& vc : voices) if (vc.age > v->age) v = &vc;
        }
        *v = Voice {};
        v->active = true;
        v->part = part;
        v->vel = velocity;
        v->freq = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
        v->samplesLeft = std::max (1.0, lengthSamples);
        v->phase[1] = 0.33; v->phase[2] = 0.71;
    }

    void allOff (bool hard)
    {
        for (auto& v : voices)
        {
            if (hard) v.active = false;
            else if (v.active) { v.samplesLeft = 0; }
        }
    }

    // adds into L/R
    void render (float* L, float* R, int n, const std::array<float, 3>& gains)
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const Shape& sh = shapes[(size_t) v.part];
            const float g = gains[(size_t) v.part] * v.vel * sh.level;
            for (int i = 0; i < n; ++i)
            {
                // envelope
                if (v.samplesLeft > 0)
                {
                    v.samplesLeft -= 1.0;
                    if (v.stage == 0) { v.env += (float) (1.0 / (sh.attack * sr)); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } }
                    else              { v.env += (sh.sustain - v.env) * (float) (1.0 / (sh.decay * sr)); }
                }
                else
                {
                    v.env *= (float) std::exp (-1.0 / (sh.release * sr));
                    if (v.env < 1.0e-4f) { v.active = false; break; }
                }

                float s = 0.0f;
                if (v.part == Bass)
                {
                    v.phase[0] += v.freq / sr; if (v.phase[0] >= 1.0) v.phase[0] -= 1.0;
                    const double p = v.phase[0] * 6.283185307179586;
                    s = (float) std::tanh (1.6 * (std::sin (p) + 0.25 * std::sin (2.0 * p)));
                }
                else
                {
                    const int nOsc = v.part == Chords ? 3 : 2;
                    for (int o = 0; o < nOsc; ++o)
                    {
                        const double det = (o == 0 ? 1.0 : (o == 1 ? 1.0 + sh.detune : 1.0 - sh.detune));
                        const double dt = v.freq * det / sr;
                        double& ph = v.phase[(size_t) o];
                        ph += dt; if (ph >= 1.0) ph -= 1.0;
                        double saw = 2.0 * ph - 1.0;
                        saw -= polyBlep (ph, dt);
                        s += (float) saw;
                    }
                    s /= (float) nOsc;
                    // state-variable low-pass, cutoff follows the envelope
                    const double fc = std::min (sr * 0.45, sh.cutoff + sh.envAmount * v.env);
                    const double gg = std::tan (3.141592653589793 * fc / sr);
                    const double k = 1.2;
                    const double a1 = 1.0 / (1.0 + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
                    const double v3 = s - v.ic2;
                    const double v1 = a1 * v.ic1 + a2 * v3;
                    const double v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
                    v.ic1 = 2.0 * v1 - v.ic1; v.ic2 = 2.0 * v2 - v.ic2;
                    s = (float) v2;
                }
                const float out = s * v.env * g;
                L[i] += out * sh.panL;
                R[i] += out * sh.panR;
            }
            ++v.age;
        }
    }

private:
    struct Shape { double attack, decay; float sustain; double release, cutoff, envAmount, detune; float level, panL, panR; };
    const std::array<Shape, 3> shapes {{
        { 0.005, 0.30, 0.55f, 0.18, 1800.0, 3500.0, 0.0035, 0.32f, 1.0f, 0.9f },   // lead
        { 0.040, 0.60, 0.80f, 0.35, 1100.0,  600.0, 0.0060, 0.12f, 0.9f, 1.0f },   // pad
        { 0.003, 0.80, 0.70f, 0.08,    0.0,    0.0, 0.0,    0.45f, 1.0f, 1.0f } }}; // bass

    struct Voice
    {
        bool active = false; int part = 0; float vel = 0.8f;
        double freq = 440.0, samplesLeft = 0.0;
        std::array<double, 3> phase {};
        float env = 0.0f; int stage = 0;
        double ic1 = 0.0, ic2 = 0.0;
        long age = 0;
    };

    static double polyBlep (double t, double dt)
    {
        if (t < dt)       { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    double sr = 44100.0;
    std::array<Voice, 32> voices;
};
