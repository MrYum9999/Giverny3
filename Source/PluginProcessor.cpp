#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct Division { const char* name; double beats; };
    constexpr Division divisions[GivernyProcessor::numDivisions] = {
        { "1/32",  0.125 },       { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 },  { "1/8T", 1.0 / 3.0 },
        { "1/16D", 0.375 },       { "1/8",   0.5 },       { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 },
        { "1/4",   1.0 },         { "1/2T",  4.0 / 3.0 }, { "1/4D", 1.5 },   { "1/2",  2.0 },
        { "1/2D",  3.0 },         { "1/1",   4.0 } };

    const std::array<int, 4> defaultOrder { gv::kPhase, gv::kTape, gv::kDelay, gv::kReverb };
}

const char* GivernyProcessor::divisionName (int i)  { return divisions[juce::jlimit (0, numDivisions - 1, i)].name; }
double      GivernyProcessor::divisionBeats (int i) { return divisions[juce::jlimit (0, numDivisions - 1, i)].beats; }

//==============================================================================
GivernyProcessor::GivernyProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pReverb   = apvts.getRawParameterValue ("reverb");
    pDelay    = apvts.getRawParameterValue ("delay");
    pTape     = apvts.getRawParameterValue ("tape");
    pPhase    = apvts.getRawParameterValue ("phase");
    pBlend    = apvts.getRawParameterValue ("blend");
    pDark     = apvts.getRawParameterValue ("dark");
    pLight    = apvts.getRawParameterValue ("light");
    pDistance = apvts.getRawParameterValue ("distance");
    pTime     = apvts.getRawParameterValue ("time");
    pSync     = apvts.getRawParameterValue ("sync");
    pRepeats  = apvts.getRawParameterValue ("repeats");

    for (auto& h : hud)
        for (int i = 0; i < 4; ++i) { h.order[i] = defaultOrder[(size_t) i]; h.level[i] = 0.0f; }
}

juce::AudioProcessorValueTreeState::ParameterLayout GivernyProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });

    auto add = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                           NormalisableRange<float> (0.0f, 1.0f), def, pct));
    };

    add ("reverb",   "Reverb",   0.55f);
    add ("delay",    "Delay",    0.35f);
    add ("tape",     "Tape",     0.30f);
    add ("phase",    "Phase",    0.20f);
    add ("blend",    "Blend",    0.35f);
    add ("dark",     "Dark",     0.15f);
    add ("light",    "Light",    0.30f);
    add ("distance", "Distance", 0.40f);
    add ("repeats",  "Repeats",  0.45f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "time", 1 }, "Time", NormalisableRange<float> (0.0f, 1.0f), 0.55f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (freeTimeMs (v))) + " ms"; })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Sync", false));

    return layout;
}

//==============================================================================
bool GivernyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (in  != juce::AudioChannelSet::stereo() && in  != juce::AudioChannelSet::mono())  return false;
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void GivernyProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (auto& c : chains) { c.prepare ((float) sr); c.order = defaultOrder; c.scale.fill (1.0f); }
    waves.prepare ((float) sr);

    audible = 0; morph = 0.0f; morphInc = 0.0f; atDefault = true;
    driftCountdown = 4.0;

    for (auto* arr : { &darkLP1, &darkLP2, &airLP, &shelfLP })
        for (auto& f : *arr) f.reset();
    for (auto& f : shelfLP) f.setCutoff (3200.0f, (float) sr);
    lastDark = lastDist = -1.0f;
    compEnv = limEnv = 0.0f;
    compAtk = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
    compRel = std::exp (-1.0f / (0.35f * (float) sr));
    limRel  = std::exp (-1.0f / (0.25f * (float) sr));
    inputEnv = 0.0f;

    for (auto* s : { &sDark, &sLight, &sDist, &sRepeats })
        s->reset (sr, 0.05);
    sDark.setCurrentAndTargetValue (pDark->load());
    sLight.setCurrentAndTargetValue (pLight->load());
    sDist.setCurrentAndTargetValue (pDistance->load());
    sRepeats.setCurrentAndTargetValue (pRepeats->load());

    std::atomic<float>* knobParams[4] = { pReverb, pDelay, pTape, pPhase };
    for (int i = 0; i < 4; ++i)
    {
        sKnobs[(size_t) i].reset (sr, 0.05);
        sKnobs[(size_t) i].setCurrentAndTargetValue (knobParams[i]->load());
    }
}

//==============================================================================
void GivernyProcessor::startDrift (bool toDefault)
{
    const float blend = pBlend->load();
    auto& next = chains[(size_t) (1 - audible)];
    const auto& cur = chains[(size_t) audible];

    if (toDefault)
    {
        next.order = defaultOrder;
        next.scale.fill (1.0f);
    }
    else
    {
        // New order (more likely the higher Blend is)
        next.order = cur.order;
        if (nextRandom() < 0.35f + 0.65f * blend)
            for (int i = 3; i > 0; --i)
                std::swap (next.order[(size_t) i], next.order[(size_t) rng.nextInt (i + 1)]);

        // New intensities: wander around the knob settings, sometimes dipping or
        // blooming, but never fading out completely
        for (auto& sc : next.scale)
        {
            sc = 1.0f + (nextRandom() * 2.0f - 1.0f) * blend * 0.5f;
            const float r = nextRandom();
            if      (r < 0.15f * blend) sc = 0.6f;
            else if (r < 0.30f * blend) sc = 1.5f;
            sc = juce::jlimit (0.6f, 1.6f, sc);
        }

        // Keep the total amount of effect close to what the knobs ask for
        float want = 0.0f, got = 0.0f;
        for (int k = 0; k < 4; ++k)
        {
            const float knob = sKnobs[(size_t) k].getTargetValue();
            want += knob;
            got  += juce::jmin (1.0f, knob * next.scale[(size_t) k]);
        }
        if (want > 0.001f && got < 0.92f * want)
        {
            const float boost = 0.92f * want / juce::jmax (0.001f, got);
            for (auto& sc : next.scale) sc = juce::jmin (1.6f, sc * boost);
        }
    }

    const float seconds = juce::jmap (blend, 5.0f, 1.6f) * (0.8f + 0.4f * nextRandom());
    morph    = 0.0f;
    morphInc = 1.0f / (seconds * (float) sampleRate);
    atDefault = toDefault;
}

//==============================================================================
void GivernyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();
    if (n == 0 || numOut == 0) return;

    // Mono in -> stereo out: duplicate the input
    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int ch = juce::jmax (numIn, numIn == 1 ? 2 : numIn); ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    const bool stereo = numOut >= 2;

    // ---- Tempo / delay time -------------------------------------------------
    double bpm = 120.0;
    bool hostBpm = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) { bpm = juce::jlimit (20.0, 400.0, *b); hostBpm = true; }

    const float timeNorm = pTime->load();
    const bool  sync     = pSync->load() > 0.5f;
    const double delayMs = sync ? 60000.0 / bpm * divisionBeats (divisionIndex (timeNorm))
                                : freeTimeMs (timeNorm);

    gv::Context ctx;
    ctx.sr       = (float) sampleRate;
    ctx.delaySmp = (float) (juce::jmin (delayMs, 2600.0) * 0.001 * sampleRate);

    sDark.setTargetValue (pDark->load());
    sLight.setTargetValue (pLight->load());
    sDist.setTargetValue (pDistance->load());
    sRepeats.setTargetValue (pRepeats->load());
    std::atomic<float>* knobParams[4] = { pReverb, pDelay, pTape, pPhase };
    for (int i = 0; i < 4; ++i) sKnobs[(size_t) i].setTargetValue (knobParams[i]->load());

    // ---- Drift engine scheduling --------------------------------------------
    const float blend = pBlend->load();
    if (morphInc == 0.0f)
    {
        if (blend < 0.01f)
        {
            if (! atDefault) startDrift (true);
        }
        else
        {
            driftCountdown -= n / sampleRate;
            if (driftCountdown <= 0.0)
            {
                startDrift (false);
                driftCountdown = juce::jmap ((double) blend, 14.0, 3.0) * (0.7 + 0.6 * rng.nextDouble());
            }
        }
    }

    // ---- Per-sample processing ----------------------------------------------
    float* L = buffer.getWritePointer (0);
    float* R = stereo ? buffer.getWritePointer (1) : nullptr;
    const float envRel = std::exp (-1.0f / (0.25f * (float) sampleRate));
    float inPeak = 0.0f;

    std::array<float, 4> knobs {};

    for (int i = 0; i < n; ++i)
    {
        for (int k = 0; k < 4; ++k) knobs[(size_t) k] = sKnobs[(size_t) k].getNextValue();
        ctx.dark     = sDark.getNextValue();
        ctx.light    = sLight.getNextValue();
        ctx.distance = sDist.getNextValue();
        ctx.repeats  = sRepeats.getNextValue();

        const float inL = L[i];
        const float inR = stereo ? R[i] : inL;
        inPeak = juce::jmax (inPeak, std::abs (inL), std::abs (inR));

        // Both chains always run so their tails stay alive
        float aL = inL, aR = inR, bL = inL, bR = inR;
        float dA = chains[(size_t) audible].process (aL, aR, knobs, ctx);
        float dB = chains[(size_t) (1 - audible)].process (bL, bR, knobs, ctx);

        // Split each chain into its dry part and its effect part
        float wAL = aL - dA * inL, wAR = aR - dA * inR;
        float wBL = bL - dB * inL, wBR = bR - dB * inR;

        float m = 0.0f;
        if (morphInc > 0.0f)
        {
            morph += morphInc;
            if (morph >= 1.0f)
            {
                audible = 1 - audible;
                morph = 0.0f; morphInc = 0.0f;
                std::swap (wAL, wBL); std::swap (wAR, wBR); std::swap (dA, dB);
            }
            else m = morph;
        }

        // Dry crossfades linearly (it is identical in both chains, so no bump),
        // the effect crossfades with equal power (so it never dips)
        const float gA = std::cos (m * gv::pi * 0.5f), gB = std::sin (m * gv::pi * 0.5f);
        const float dry = dA + (dB - dA) * m;
        const float wetL = wAL * gA + wBL * gB;
        const float wetR = wAR * gA + wBR * gB;

        // Distant waves: far-off echoes of the effect
        float farL = 0.0f, farR = 0.0f;
        waves.process (wetL, wetR, ctx, farL, farR);

        // Effect compressor: keeps the whole wash (and the distant waves) from
        // swelling far above your playing on long sustained notes
        float eL = wetL + farL, eR = wetR + farR;
        {
            const float a = juce::jmax (std::abs (eL), std::abs (eR));
            compEnv = a > compEnv ? compEnv + (a - compEnv) * compAtk : compEnv * compRel;
            const float thr = 0.3f;
            if (compEnv > thr)
            {
                const float g = (thr + (compEnv - thr) * 0.25f) / compEnv;   // 4:1 above threshold
                eL *= g; eR *= g;
            }
        }

        float oL = dry * inL + eL;
        float oR = dry * inR + eR;

        // ---- Dark: two-pole low-pass sweeping down to ~1.2 kHz ----------------
        if (ctx.dark != lastDark || ctx.distance != lastDist)
        {
            lastDark = ctx.dark; lastDist = ctx.distance;
            const float darkHz = 20000.0f * std::pow (0.06f, ctx.dark);
            const float airHz  = 20000.0f * std::pow (0.22f, ctx.distance);
            for (int ch = 0; ch < 2; ++ch)
            {
                darkLP1[(size_t) ch].setCutoff (darkHz, ctx.sr);
                darkLP2[(size_t) ch].setCutoff (darkHz, ctx.sr);
                airLP[(size_t) ch].setCutoff (airHz, ctx.sr);
            }
        }
        const float darkMix = juce::jmin (1.0f, ctx.dark * 4.0f);
        oL += darkMix * (darkLP2[0].process (darkLP1[0].process (oL)) - oL);
        oR += darkMix * (darkLP2[1].process (darkLP1[1].process (oR)) - oR);

        // ---- Light (+ near-field presence): high shelf above ~3 kHz ----------
        const float shelf = ctx.light * 1.5f + (1.0f - ctx.distance) * 0.25f;
        oL += shelf * (oL - shelfLP[0].process (oL));
        oR += shelf * (oR - shelfLP[1].process (oR));

        // ---- Distance: air absorption + level falloff ------------------------
        const float airMix = juce::jmin (1.0f, ctx.distance * 2.0f);
        oL += airMix * (airLP[0].process (oL) - oL);
        oR += airMix * (airLP[1].process (oR) - oR);
        const float level = 1.1f - 0.45f * ctx.distance;

        oL *= level;
        oR *= level;

        // Output limiter: never above -3 dBFS, so even two copies on the master
        // bus stay out of clipping
        {
            const float ceiling = 0.708f;
            const float a = juce::jmax (std::abs (oL), std::abs (oR));
            limEnv = a > limEnv ? a : limEnv * limRel;
            if (limEnv > ceiling) { const float g = ceiling / limEnv; oL *= g; oR *= g; }
        }

        // Safety net: if anything ever goes non-numeric, silence and reset
        if (! std::isfinite (oL) || ! std::isfinite (oR))
        {
            for (auto& ch : chains) ch.reset();
            waves.reset();
            for (auto* arr : { &darkLP1, &darkLP2, &airLP, &shelfLP })
                for (auto& f : *arr) f.reset();
            compEnv = limEnv = 0.0f;
            oL = oR = 0.0f;
        }

        if (stereo) { L[i] = oL; R[i] = oR; }
        else        { L[i] = 0.5f * (oL + oR); }
    }

    // ---- HUD ----------------------------------------------------------------
    inputEnv = juce::jmax (inPeak, inputEnv * std::pow (envRel, (float) n));
    hudInput.store (inputEnv);
    hudBpm.store (bpm);
    hudHostBpm.store (hostBpm);

    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < 4; ++k)
        {
            hud[c].order[k].store (chains[(size_t) c].order[(size_t) k]);
            const int id = chains[(size_t) c].order[(size_t) k];
            hud[c].level[k].store (juce::jlimit (0.0f, 1.0f, knobs[(size_t) id] * chains[(size_t) c].scale[(size_t) id]));
        }
    hudMorph.store (audible == 0 ? morph : 1.0f - morph);
}

//==============================================================================
juce::AudioProcessorEditor* GivernyProcessor::createEditor()
{
    return new GivernyEditor (*this);
}

void GivernyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GivernyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GivernyProcessor();
}
