#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
// Small building blocks used by Giverny's four effects.
//==============================================================================
namespace gv
{
    #ifndef REVERB_NORM
     #define REVERB_NORM 3.4f
    #endif

    constexpr float pi    = juce::MathConstants<float>::pi;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    inline float softClip (float x)
    {
        // Transparent below ~0.8, gently rounds anything hotter.
        const float a = std::abs (x);
        if (a < 0.8f) return x;
        const float s = x > 0.0f ? 1.0f : -1.0f;
        return s * (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f));
    }

    //--------------------------------------------------------------------------
    struct OnePoleLP
    {
        float z = 0.0f, a = 0.0f;
        void setCutoff (float hz, float sr) { a = std::exp (-twoPi * juce::jlimit (10.0f, 0.49f * sr, hz) / sr); }
        float process (float x)             { z = x + a * (z - x); return z; }
        void reset()                         { z = 0.0f; }
    };

    struct OnePoleHP
    {
        OnePoleLP lp;
        void setCutoff (float hz, float sr) { lp.setCutoff (hz, sr); }
        float process (float x)             { return x - lp.process (x); }
        void reset()                         { lp.reset(); }
    };

    //--------------------------------------------------------------------------
    // Keeps a feedback loop from building up: if the signal circulating in an
    // echo loop gets hotter than 'threshold', it is smoothly turned down.
    struct LoopGuard
    {
        float env = 0.0f, atk = 0.0f, rel = 0.0f, threshold = 0.5f;
        void prepare (float sr, float thr)
        {
            threshold = thr;
            atk = 1.0f - std::exp (-1.0f / (0.002f * sr));
            rel = std::exp (-1.0f / (0.25f * sr));
            env = 0.0f;
        }
        float process (float x)
        {
            const float a = std::abs (x);
            env = a > env ? env + (a - env) * atk : env * rel;
            return env > threshold ? x * (threshold / env) : x;
        }
    };

    //--------------------------------------------------------------------------
    // Interpolated circular delay line
    struct DelayLine
    {
        std::vector<float> buf;
        int w = 0;

        void prepare (int maxSamples) { buf.assign ((size_t) maxSamples + 4, 0.0f); w = 0; }
        void reset()                  { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        void push (float x)           { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }

        // Read 'd' samples behind the most recently pushed sample (d >= 1)
        float read (float d) const
        {
            const int size = (int) buf.size();
            d = juce::jlimit (1.0f, (float) size - 3.0f, d);
            float pos = (float) w - d;
            while (pos < 0.0f) pos += (float) size;
            const int i0 = (int) pos;
            const int i1 = (i0 + 1) % size;
            const float f = pos - (float) i0;
            return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
        }
    };

    //--------------------------------------------------------------------------
    struct Allpass
    {
        DelayLine d;
        float len = 1.0f, g = 0.6f;
        void prepare (int maxLen) { d.prepare (maxLen); }
        void reset()              { d.reset(); }
        float process (float x, float mod = 0.0f)
        {
            const float delayed = d.read (len + mod);
            const float v = x + g * delayed;
            d.push (v);
            return delayed - g * v;
        }
    };

    //==========================================================================
    // Shared values the four effects read every sample
    struct Context
    {
        float sr          = 48000.0f;
        float delaySmp    = 24000.0f; // main delay time in samples
        float repeats     = 0.4f;     // 0..1
        float dark        = 0.0f;     // 0..1
        float light       = 0.0f;     // 0..1
        float distance    = 0.4f;     // 0..1 (near..far)
    };

    //==========================================================================
    // 1. Reverb: 8-line feedback delay network with modulated lines
    //==========================================================================
    class Reverb
    {
    public:
        void prepare (float sampleRate)
        {
            sr = sampleRate;
            pre.prepare ((int) (0.12f * sr));
            for (int i = 0; i < 4; ++i)
            {
                diff[(size_t) i].prepare ((int) (0.03f * sr));
                diff[(size_t) i].len = diffMs[i] * 0.001f * sr;
                diff[(size_t) i].g   = 0.62f;
            }
            for (int i = 0; i < 8; ++i)
                lines[(size_t) i].prepare ((int) (0.2f * sr));
            reset();
        }

        void reset()
        {
            pre.reset();
            for (auto& a : diff)  a.reset();
            for (auto& l : lines) l.reset();
            for (auto& f : damp)  f.reset();
            lfo = 0.0f; cAmount = -1.0f;
        }

        void process (float& L, float& R, float amount, const Context& c)
        {
            if (amount != cAmount || c.dark != cDark || c.light != cLight || c.distance != cDist)
            {
                cAmount = amount; cDark = c.dark; cLight = c.light; cDist = c.distance;
                const float distBoost = 0.6f + 0.8f * c.distance;          // far = more room
                mix  = juce::jlimit (0.0f, 1.0f, amount * distBoost);
                const float t60 = 1.4f + 10.0f * std::pow (amount, 1.4f) * (0.7f + 0.6f * c.distance);
                size = 0.9f + 0.7f * amount;
                predelaySmp = juce::jmax (1.0f, (45.0f - 40.0f * c.distance) * 0.001f * sr); // near = longer gap
                // Dark closes the damping, Light opens it up (more reflective)
                const float dampHz = juce::jlimit (900.0f, 16000.0f,
                                                   6500.0f * std::pow (0.2f, c.dark) * std::pow (2.6f, c.light));
                for (auto& f : damp) f.setCutoff (dampHz, sr);
                float gSq = 0.0f;
                for (int i = 0; i < 8; ++i)
                {
                    gains[(size_t) i] = std::pow (10.0f, -3.0f * lineMs[i] * 0.001f * size / t60);
                    gSq += gains[(size_t) i] * gains[(size_t) i] / 8.0f;
                }
                // Long tails pile up energy on sustained notes: scale the room so it
                // never gets louder than the sound going into it
                wetNorm = juce::jmin (1.0f, REVERB_NORM * std::sqrt (1.0f - gSq));
                diffG = 0.55f + 0.2f * c.light;
            }

            // Input: mono sum -> predelay -> diffusion (Light = denser, glassier diffusion)
            pre.push (0.5f * (L + R));
            float x = pre.read (predelaySmp);
            for (auto& a : diff) { a.g = diffG; x = a.process (x); }

            lfo += 0.31f / sr; if (lfo >= 1.0f) lfo -= 1.0f;
            const float m1 = std::sin (twoPi * lfo) * 0.0012f * sr;
            const float m2 = std::cos (twoPi * lfo * 0.73f) * 0.0009f * sr;

            std::array<float, 8> o {};
            for (int i = 0; i < 8; ++i)
            {
                const float lenSmp = lineMs[i] * 0.001f * sr * size;
                const float mod = (i == 1 ? m1 : (i == 4 ? m2 : (i == 6 ? -m1 : 0.0f)));
                o[(size_t) i] = lines[(size_t) i].read (lenSmp + mod);
            }

            // Hadamard 8x8 mixing
            std::array<float, 8> h = o;
            for (int len = 1; len < 8; len <<= 1)
                for (int i = 0; i < 8; i += len << 1)
                    for (int j = i; j < i + len; ++j)
                    {
                        const float a = h[(size_t) j], b = h[(size_t) (j + len)];
                        h[(size_t) j] = a + b; h[(size_t) (j + len)] = a - b;
                    }

            for (int i = 0; i < 8; ++i)
            {
                const float in = (i % 2 == 0 ? x : -x) * 0.5f;
                lines[(size_t) i].push (damp[(size_t) i].process (h[(size_t) i] * 0.35355f * gains[(size_t) i]) + in);
            }

            const float wL = (o[0] + o[2] + o[4] + o[6]) * 0.5f;
            const float wR = (o[1] + o[3] + o[5] + o[7]) * 0.5f;

            const float dry = 1.0f - 0.55f * mix;
            L = L * dry + wL * wetNorm * mix;
            R = R * dry + wR * wetNorm * mix;
        }

    private:
        static constexpr float lineMs[8] = { 37.1f, 41.3f, 47.9f, 53.3f, 61.1f, 67.9f, 73.7f, 83.3f };
        static constexpr float diffMs[4] = { 4.7f, 6.1f, 9.3f, 12.7f };

        float sr = 48000.0f, lfo = 0.0f;
        float cAmount = -1.0f, cDark = -1.0f, cLight = -1.0f, cDist = -1.0f;
        float mix = 0.0f, size = 1.0f, predelaySmp = 1.0f, diffG = 0.6f, wetNorm = 1.0f;
        std::array<float, 8> gains {};
        DelayLine pre;
        std::array<Allpass, 4> diff;
        std::array<DelayLine, 8> lines;
        std::array<OnePoleLP, 8> damp;
    };

    //==========================================================================
    // 2. Delay: clean stereo ping-pong
    //==========================================================================
    class Delay
    {
    public:
        void prepare (float sampleRate)
        {
            sr = sampleRate;
            for (auto& d : dl) d.prepare ((int) (4.2f * sr));
            for (auto& g : guard) g.prepare (sr, 0.5f);
            reset();
        }

        void reset()
        {
            for (auto& d : dl) d.reset();
            for (auto& f : lp) f.reset();
            for (auto& f : hp) f.reset();
            curDelay = -1.0f; cDark = -1.0f;
            for (auto& g : guard) g.env = 0.0f;
        }

        void process (float& L, float& R, float amount, const Context& c)
        {
            const float target = juce::jlimit (16.0f, 4.0f * sr, c.delaySmp);
            if (curDelay < 0.0f) curDelay = target;
            curDelay += (target - curDelay) * 0.00008f;   // slow glide when time changes

            const float fb = juce::jlimit (0.0f, 0.92f, c.repeats * 0.92f);
            if (c.dark != cDark || c.light != cLight)
            {
                cDark = c.dark; cLight = c.light;
                const float lpHz = 9000.0f * std::pow (0.25f, c.dark) * std::pow (1.8f, c.light);
                for (auto& f : lp) f.setCutoff (lpHz, sr);
                for (auto& f : hp) f.setCutoff (90.0f, sr);
            }

            const float dL = dl[0].read (curDelay);
            const float dR = dl[1].read (curDelay);

            const float in = 0.5f * (L + R) * (1.0f - 0.35f * fb);
            dl[0].push (softClip (guard[0].process (in + fb * hp[0].process (lp[0].process (dR)))));
            dl[1].push (softClip (guard[1].process (fb * hp[1].process (lp[1].process (dL)))));

            const float mix = juce::jlimit (0.0f, 1.0f, amount);
            L = L * (1.0f - 0.3f * mix) + dL * mix;
            R = R * (1.0f - 0.3f * mix) + dR * mix;
        }

    private:
        float sr = 48000.0f, curDelay = -1.0f, cDark = -1.0f, cLight = -1.0f;
        std::array<DelayLine, 2> dl;
        std::array<LoopGuard, 2> guard;
        std::array<OnePoleLP, 2> lp;
        std::array<OnePoleHP, 2> hp;
    };

    //==========================================================================
    // 3. Tape echo: 1.5x the delay time, wow & flutter, saturation, worn tone
    //==========================================================================
    class Tape
    {
    public:
        void prepare (float sampleRate)
        {
            sr = sampleRate;
            for (auto& d : dl) d.prepare ((int) (4.2f * sr));
            for (auto& g : guard) g.prepare (sr, 0.5f);
            reset();
        }

        void reset()
        {
            for (auto& d : dl) d.reset();
            for (auto& f : lp) f.reset();
            for (auto& f : hp) f.reset();
            wow = flutter = drift = 0.0f;
            curDelay = -1.0f; cDark = -1.0f;
            for (auto& g : guard) g.env = 0.0f;
        }

        void process (float& L, float& R, float amount, const Context& c)
        {
            const float target = juce::jlimit (16.0f, 3.9f * sr, c.delaySmp * 1.5f);
            if (curDelay < 0.0f) curDelay = target;
            curDelay += (target - curDelay) * 0.00004f;   // tape motors are slow to change speed

            wow     += 0.55f / sr; if (wow >= 1.0f)     wow -= 1.0f;
            flutter += 6.3f / sr;  if (flutter >= 1.0f) flutter -= 1.0f;
            drift   += 0.07f / sr; if (drift >= 1.0f)   drift -= 1.0f;

            const float depth = 0.4f + 0.6f * amount;
            const float mod = (std::sin (twoPi * wow) * 0.0028f
                               + std::sin (twoPi * drift) * 0.0016f
                               + std::sin (twoPi * flutter) * 0.00018f) * sr * depth;

            const float fb = juce::jlimit (0.0f, 0.9f, c.repeats * 0.9f);
            const float inGain = 1.0f - 0.35f * fb;
            if (c.dark != cDark || c.light != cLight)
            {
                cDark = c.dark; cLight = c.light;
                const float lpHz = 4200.0f * std::pow (0.35f, c.dark) * std::pow (1.5f, c.light);
                for (auto& f : lp) f.setCutoff (lpHz, sr);
                for (auto& f : hp) f.setCutoff (120.0f, sr);
            }

            const float tL = dl[0].read (curDelay + mod);
            const float tR = dl[1].read (curDelay * 1.012f + mod * 0.8f);   // heads slightly apart = width

            const float drive = 1.6f;
            dl[0].push (std::tanh (drive * guard[0].process (L * inGain + fb * hp[0].process (lp[0].process (tL)))) / drive);
            dl[1].push (std::tanh (drive * guard[1].process (R * inGain + fb * hp[1].process (lp[1].process (tR)))) / drive);

            const float mix = juce::jlimit (0.0f, 1.0f, amount);
            L = L * (1.0f - 0.3f * mix) + tL * mix;
            R = R * (1.0f - 0.3f * mix) + tR * mix;
        }

    private:
        float sr = 48000.0f, curDelay = -1.0f, cDark = -1.0f, cLight = -1.0f;
        float wow = 0.0f, flutter = 0.0f, drift = 0.0f;
        std::array<DelayLine, 2> dl;
        std::array<LoopGuard, 2> guard;
        std::array<OnePoleLP, 2> lp;
        std::array<OnePoleHP, 2> hp;
    };

    //==========================================================================
    // 4. Phase: 6-stage stereo phaser, slow sweep tied to the delay time
    //==========================================================================
    class Phaser
    {
    public:
        void prepare (float sampleRate) { sr = sampleRate; reset(); }

        void reset()
        {
            for (auto& ch : xs) ch.fill (0.0f);
            for (auto& ch : ys) ch.fill (0.0f);
            fbs.fill (0.0f);
            lfo = 0.0f;
        }

        void process (float& L, float& R, float amount, const Context& c)
        {
            // One full sweep every 16 delay repeats (so it follows tempo when synced)
            const float rate = juce::jlimit (0.03f, 1.2f, c.sr / (juce::jmax (1.0f, c.delaySmp) * 16.0f));
            lfo += rate / sr; if (lfo >= 1.0f) lfo -= 1.0f;

            const float mix   = juce::jlimit (0.0f, 1.0f, amount) * 0.5f;
            const float depth = 0.5f + 0.5f * amount;

            float* io[2] = { &L, &R };
            for (int ch = 0; ch < 2; ++ch)
            {
                float ph = lfo + (ch == 1 ? 0.2f : 0.0f);
                if (ph >= 1.0f) ph -= 1.0f;
                const float l  = 0.5f + 0.5f * std::sin (twoPi * ph);
                const float fc = 220.0f * std::pow (2.0f, l * depth * 4.2f);
                const float t  = std::tan (pi * juce::jmin (fc, 0.45f * sr) / sr);
                const float a  = (t - 1.0f) / (t + 1.0f);

                const float x = *io[ch];
                float s = x + 0.45f * fbs[(size_t) ch];
                for (int k = 0; k < 6; ++k)
                {
                    const float y = a * s + xs[(size_t) ch][(size_t) k] - a * ys[(size_t) ch][(size_t) k];
                    xs[(size_t) ch][(size_t) k] = s;
                    ys[(size_t) ch][(size_t) k] = y;
                    s = y;
                }
                fbs[(size_t) ch] = s;
                *io[ch] = x * (1.0f - mix) + s * mix;
            }
        }

    private:
        float sr = 48000.0f, lfo = 0.0f;
        std::array<std::array<float, 6>, 2> xs {}, ys {};
        std::array<float, 2> fbs {};
    };

    //==========================================================================
    // A full chain: the four effects in a configurable order
    //==========================================================================
    enum EffectId { kReverb = 0, kDelay = 1, kTape = 2, kPhase = 3 };

    struct Chain
    {
        Reverb reverb; Delay delay; Tape tape; Phaser phaser;
        std::array<int, 4>   order { kPhase, kTape, kDelay, kReverb };
        std::array<float, 4> scale { 1.0f, 1.0f, 1.0f, 1.0f };   // drift engine multipliers

        void prepare (float sr) { reverb.prepare (sr); delay.prepare (sr); tape.prepare (sr); phaser.prepare (sr); }
        void reset()            { reverb.reset(); delay.reset(); tape.reset(); phaser.reset(); }

        // Processes in place and returns the "dry factor": how much of the untouched
        // input passes straight through the chain. (Out = dry * input + effect.)
        float process (float& L, float& R, const std::array<float, 4>& knobs, const Context& c)
        {
            float dry = 1.0f;
            for (int id : order)
            {
                const float amt = juce::jlimit (0.0f, 1.0f, knobs[(size_t) id] * scale[(size_t) id]);
                switch (id)
                {
                    case kReverb:
                        reverb.process (L, R, amt, c);
                        dry *= 1.0f - 0.55f * juce::jlimit (0.0f, 1.0f, amt * (0.6f + 0.8f * c.distance));
                        break;
                    case kDelay:  delay.process  (L, R, amt, c); dry *= 1.0f - 0.3f * amt; break;
                    case kTape:   tape.process   (L, R, amt, c); dry *= 1.0f - 0.3f * amt; break;
                    default:      phaser.process (L, R, amt, c); dry *= 1.0f - 0.5f * amt; break;
                }
            }
            return dry;
        }
    };
    //==========================================================================
    // Distant waves: blurred, darker echoes of the effect returning from far
    // away, each swelling and receding at its own slow rate. Driven by Distance.
    //==========================================================================
    class DistantWaves
    {
    public:
        void prepare (float sampleRate)
        {
            sr = sampleRate;
            buf.prepare ((int) (6.0f * sr));
            fbGuard.prepare (sr, 0.4f);
            for (int k = 0; k < numTaps; ++k)
                for (int j = 0; j < 3; ++j)
                {
                    auto& a = diff[(size_t) k][(size_t) j];
                    a.prepare ((int) (0.06f * sr));
                    a.len = apMs[k][j] * 0.001f * sr;
                    a.g   = 0.66f;
                }
            reset();
        }

        void reset()
        {
            buf.reset();
            for (auto& t : diff) for (auto& a : t) a.reset();
            for (auto& f : lp) f.reset();
            for (int k = 0; k < numTaps; ++k) { wave[(size_t) k] = (float) k * 0.27f; wob[(size_t) k] = (float) k * 0.41f; }
            cDist = -1.0f; fbGuard.env = 0.0f;
        }

        void process (float wetL, float wetR, const Context& c, float& outL, float& outR)
        {
            const float d = c.distance;
            if (d != cDist)
            {
                cDist = d;
                amount = std::pow (d, 1.3f);
                baseSmp = (0.32f + 0.9f * d) * sr;
                for (int k = 0; k < numTaps; ++k)
                    lp[(size_t) k].setCutoff (5200.0f * std::pow (0.4f, d) * std::pow (0.78f, (float) k), sr);
            }

            float sumL = 0.0f, sumR = 0.0f, fbSum = 0.0f;
            for (int k = 0; k < numTaps; ++k)
            {
                wave[(size_t) k] += waveHz[k] / sr; if (wave[(size_t) k] >= 1.0f) wave[(size_t) k] -= 1.0f;
                wob[(size_t) k]  += (0.21f + 0.07f * (float) k) / sr; if (wob[(size_t) k] >= 1.0f) wob[(size_t) k] -= 1.0f;

                const float t = baseSmp * mult[k] + std::sin (twoPi * wob[(size_t) k]) * 0.0035f * sr;
                float x = buf.read (t);
                for (auto& a : diff[(size_t) k]) x = a.process (x);
                x = lp[(size_t) k].process (x);

                const float swell = 0.55f + 0.45f * std::sin (twoPi * wave[(size_t) k]);
                const float y = x * gain[k] * swell * amount;
                sumL += y * panL[k];
                sumR += y * panR[k];
                fbSum += x * gain[k];
            }

            buf.push (softClip (fbGuard.process (0.5f * (wetL + wetR) + 0.28f * amount * fbSum)));
            outL = sumL * 1.6f;
            outR = sumR * 1.6f;
        }

    private:
        static constexpr int numTaps = 4;
        static constexpr float mult[numTaps]   = { 1.0f, 1.55f, 2.35f, 3.4f };
        static constexpr float gain[numTaps]   = { 0.5f, 0.4f, 0.32f, 0.26f };
        static constexpr float waveHz[numTaps] = { 0.071f, 0.053f, 0.097f, 0.043f };
        static constexpr float panL[numTaps]   = { 0.85f, 0.35f, 0.75f, 0.5f };
        static constexpr float panR[numTaps]   = { 0.35f, 0.85f, 0.5f, 0.75f };
        static constexpr float apMs[numTaps][3] = { { 13.1f, 21.7f, 34.3f }, { 15.7f, 24.1f, 38.9f },
                                                    { 11.3f, 27.9f, 41.3f }, { 17.9f, 29.3f, 45.1f } };

        float sr = 48000.0f, cDist = -1.0f, amount = 0.0f, baseSmp = 1.0f;
        DelayLine buf;
        LoopGuard fbGuard;
        std::array<std::array<Allpass, 3>, numTaps> diff;
        std::array<OnePoleLP, numTaps> lp;
        std::array<float, numTaps> wave {}, wob {};
    };
}
