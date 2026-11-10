#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>
#include "PluginProcessor.h"

//==============================================================================
namespace Palette
{
    const juce::Colour ink      { 0xff2e2747 };
    const juce::Colour paper    { 0xfff6efe0 };
    const juce::Colour cream    { 0xfffbf6ea };

    const juce::Colour reverb   { 0xff8e7cc3 };   // violet
    const juce::Colour delay    { 0xff4fa3a5 };   // teal
    const juce::Colour tape     { 0xffe08a7a };   // rose coral
    const juce::Colour phase    { 0xffe3b94f };   // ochre
    const juce::Colour blend    { 0xff5c8fc7 };   // pond blue
    const juce::Colour dark     { 0xff3e4c7a };   // dusk indigo
    const juce::Colour light    { 0xfff1cf6e };   // sunlight
    const juce::Colour distance { 0xff7fb8a6 };   // willow green
    const juce::Colour time     { 0xff6fa3c7 };   // water
    const juce::Colour repeats  { 0xffc58fb8 };   // mauve
}

//==============================================================================
class PaintedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PaintedLookAndFeel();

    static juce::Font serif (float height, bool italic = false);

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
};

//==============================================================================
class GivernyEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit GivernyEditor (GivernyProcessor&);
    ~GivernyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Ripple { float x, y, r, life; };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, juce::Colour, bool big = false);
    void placeKnob (Knob&, juce::Rectangle<int> area, int size);
    void timerCallback() override;
    void paintBackground();
    void drawPond (juce::Graphics&, juce::Rectangle<float>);

    GivernyProcessor& processor;
    PaintedLookAndFeel lnf;

    Knob reverb, delay, tape, phase, blend, dark, light, distance, time, repeats;
    juce::TextButton syncButton { "Sync" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;
    juce::Label bpmLabel;

    juce::Image background;
    juce::Rectangle<int> pondArea, leftPanel, rightPanel, tonePanel, timePanel, blendArea;

    std::vector<Ripple> ripples;
    juce::Random rng { 1872 };
    float clock = 0.0f, lastInput = 0.0f;
    bool lastSync = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GivernyEditor)
};
