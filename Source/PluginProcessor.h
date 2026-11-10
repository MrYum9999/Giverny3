#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Effects.h"

//==============================================================================
// Giverny: an ambient reverb / delay / tape / phase machine.
//
// Two identical effect chains run side by side. The Blend knob drives a slow
// "drift" engine: every few seconds it gives the silent chain a new random
// order and new random intensities, then crossfades over to it. Both chains
// always process audio, so tails never get cut off when the order changes.
//==============================================================================
class GivernyProcessor : public juce::AudioProcessor
{
public:
    GivernyProcessor();
    ~GivernyProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 12.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    //--------------------------------------------------------------------------
    // Time helpers (shared with the editor for display)
    static constexpr int numDivisions = 14;
    static const char* divisionName (int index);
    static double divisionBeats (int index);
    static int    divisionIndex (float timeNorm)  { return juce::jlimit (0, numDivisions - 1, juce::roundToInt (timeNorm * (numDivisions - 1))); }
    static double freeTimeMs (float timeNorm)     { return 40.0 * std::pow (2000.0 / 40.0, (double) timeNorm); }

    //--------------------------------------------------------------------------
    // Read by the editor for the live "pond" display
    struct HudChain
    {
        std::atomic<int>   order[4];
        std::atomic<float> level[4];
    };
    HudChain hud[2];
    std::atomic<float> hudMorph   { 0.0f };   // 0 = chain A audible, 1 = chain B
    std::atomic<float> hudInput   { 0.0f };
    std::atomic<double> hudBpm    { 120.0 };
    std::atomic<bool>  hudHostBpm { false };

private:
    void startDrift (bool toDefault);
    float nextRandom() { return rng.nextFloat(); }

    double sampleRate = 48000.0;

    std::array<gv::Chain, 2> chains;
    gv::DistantWaves waves;
    int   audible  = 0;        // chain currently heard (when not morphing)
    float morph    = 0.0f;     // 0..1 progress towards the other chain
    float morphInc = 0.0f;     // per-sample increment while morphing (0 = idle)
    double driftCountdown = 0.0;
    bool  atDefault = true;

    // Output tone stages
    std::array<gv::OnePoleLP, 2> darkLP1, darkLP2, airLP;
    std::array<gv::OnePoleLP, 2> shelfLP;
    float inputEnv = 0.0f, lastDark = -1.0f, lastDist = -1.0f;
    float compEnv = 0.0f, compAtk = 0.0f, compRel = 0.0f;
    float limEnv = 0.0f, limRel = 0.0f;

    juce::SmoothedValue<float> sDark, sLight, sDist, sRepeats, sDelaySmp;
    std::array<juce::SmoothedValue<float>, 4> sKnobs;

    juce::Random rng;

    std::atomic<float>* pReverb   = nullptr;
    std::atomic<float>* pDelay    = nullptr;
    std::atomic<float>* pTape     = nullptr;
    std::atomic<float>* pPhase    = nullptr;
    std::atomic<float>* pBlend    = nullptr;
    std::atomic<float>* pDark     = nullptr;
    std::atomic<float>* pLight    = nullptr;
    std::atomic<float>* pDistance = nullptr;
    std::atomic<float>* pTime     = nullptr;
    std::atomic<float>* pSync     = nullptr;
    std::atomic<float>* pRepeats  = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GivernyProcessor)
};
