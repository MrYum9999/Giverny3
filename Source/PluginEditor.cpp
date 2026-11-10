#include "PluginEditor.h"

using namespace Palette;

namespace
{
    // Draw one brush stroke: a short rounded line with a slightly lighter core
    void brush (juce::Graphics& g, float x, float y, float len, float width, float angle, juce::Colour c)
    {
        const float dx = std::cos (angle) * len * 0.5f, dy = std::sin (angle) * len * 0.5f;
        juce::Path p;
        p.startNewSubPath (x - dx, y - dy);
        p.quadraticTo (x + dy * 0.15f, y - dx * 0.15f, x + dx, y + dy);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.brighter (0.25f).withMultipliedAlpha (0.45f));
        g.strokePath (p, juce::PathStrokeType (width * 0.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Colour jitter (juce::Colour c, juce::Random& r, float amount = 0.06f)
    {
        return c.withRotatedHue ((r.nextFloat() - 0.5f) * amount)
                .withMultipliedSaturation (0.85f + 0.3f * r.nextFloat())
                .withMultipliedBrightness (0.88f + 0.24f * r.nextFloat());
    }

    juce::Colour paletteAt (float t, juce::Random& r)
    {
        static const juce::Colour sky[]   = { juce::Colour (0xfff2c9a5), juce::Colour (0xffc9b6e4), juce::Colour (0xffa9c8e8), juce::Colour (0xfff6dfa0), juce::Colour (0xfff0b3b8) };
        static const juce::Colour mid[]   = { juce::Colour (0xffa9c8e8), juce::Colour (0xffb7a6dc), juce::Colour (0xff9fd0c8), juce::Colour (0xffd8c6ea), juce::Colour (0xff8fbad8) };
        static const juce::Colour water[] = { juce::Colour (0xff6fa3c7), juce::Colour (0xff4e86b0), juce::Colour (0xff7fb8b0), juce::Colour (0xff8e7cc3),
                                              juce::Colour (0xff3e6e9e), juce::Colour (0xff9cc9a8), juce::Colour (0xff5d9a8c), juce::Colour (0xffb6a3dd) };
        if (t < 0.2f)  return jitter (sky[r.nextInt (5)], r);
        if (t < 0.42f) return jitter (mid[r.nextInt (5)], r);
        return jitter (water[r.nextInt (8)], r);
    }

    const char* effectNames[4] = { "Reverb", "Delay", "Tape", "Phase" };
    const juce::Colour effectColours[4] = { reverb, delay, tape, phase };
}

//==============================================================================
PaintedLookAndFeel::PaintedLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId,       ink);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,  reverb.withAlpha (0.4f));
    setColour (juce::Label::textColourId,               ink);
    setColour (juce::TextEditor::textColourId,          ink);
}

juce::Font PaintedLookAndFeel::serif (float height, bool italic)
{
    static const juce::String face = []
    {
        const auto names = juce::Font::findAllTypefaceNames();
        for (auto* f : { "Georgia", "Palatino Linotype", "Cambria", "DejaVu Serif", "Liberation Serif" })
            if (names.contains (f)) return juce::String (f);
        return juce::Font::getDefaultSerifFontName();
    }();

    auto opts = juce::FontOptions().withName (face).withHeight (height);
    if (italic) opts = opts.withStyle ("Italic");
    return juce::Font (opts);
}

juce::Font PaintedLookAndFeel::getLabelFont (juce::Label& l)
{
    if (l.getFont().getHeight() > 16.0f) return l.getFont();
    return serif (15.5f, false);
}

void PaintedLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                           float pos, float startAngle, float endAngle, juce::Slider& s)
{
    const juce::Colour col ((juce::uint32) (juce::int64) s.getProperties().getWithDefault ("paint", (juce::int64) 0xff6fa3c7));
    juce::Random r ((juce::int64) s.getComponentID().hashCode() + 7);

    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  c      = bounds.getCentre();
    const float angle  = startAngle + pos * (endAngle - startAngle);
    const float body   = radius * 0.72f;

    // Track of soft dabs
    const int dabs = 30;
    for (int i = 0; i <= dabs; ++i)
    {
        const float t  = (float) i / dabs;
        const float a  = startAngle + t * (endAngle - startAngle);
        const auto  p  = c.getPointOnCircumference (radius * 0.9f, a);
        const bool lit = t <= pos + 0.0001f;
        const float sz = lit ? radius * (0.11f + 0.03f * r.nextFloat()) : radius * 0.06f;
        g.setColour (lit ? jitter (col, r, 0.1f).withAlpha (0.9f) : ink.withAlpha (0.16f));
        g.fillEllipse (juce::Rectangle<float> (sz * 1.3f, sz).withCentre (p));
    }

    // Shadow
    g.setColour (ink.withAlpha (0.18f));
    g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c.translated (2.0f, 3.0f)));

    // Body: base wash + brush strokes
    g.setColour (col.withMultipliedBrightness (0.92f));
    g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c));

    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c));
        g.reduceClipRegion (clip);
        for (int i = 0; i < 70; ++i)
        {
            const float rr = body * std::sqrt (r.nextFloat());
            const float th = r.nextFloat() * gv::twoPi;
            auto colour = r.nextFloat() < 0.5f ? col.brighter (0.2f + 0.4f * r.nextFloat())
                                                : col.darker (0.1f + 0.35f * r.nextFloat());
            brush (g, c.x + rr * std::cos (th), c.y + rr * std::sin (th),
                   body * (0.15f + 0.25f * r.nextFloat()), body * (0.06f + 0.06f * r.nextFloat()),
                   r.nextFloat() * gv::pi, jitter (colour, r).withAlpha (0.55f));
        }
        // Soft light from the upper left
        g.setGradientFill (juce::ColourGradient (cream.withAlpha (0.45f), c.x - body * 0.6f, c.y - body * 0.7f,
                                                 cream.withAlpha (0.0f), c.x + body * 0.3f, c.y + body * 0.4f, true));
        g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c));
    }

    // Painted rim
    g.setColour (ink.withAlpha (0.35f));
    g.drawEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c), 1.2f);

    // Pointer: a cream stroke outlined in ink
    const auto p1 = c.getPointOnCircumference (body * 0.25f, angle);
    const auto p2 = c.getPointOnCircumference (body * 0.92f, angle);
    juce::Path ptr; ptr.startNewSubPath (p1); ptr.lineTo (p2);
    g.setColour (ink.withAlpha (0.6f));
    g.strokePath (ptr, juce::PathStrokeType (juce::jmax (4.0f, body * 0.13f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (cream);
    g.strokePath (ptr, juce::PathStrokeType (juce::jmax (2.2f, body * 0.07f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void PaintedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                               bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (3.0f);
    juce::Random rnd (42);
    if (b.getToggleState())
    {
        g.setColour (Palette::time.withMultipliedBrightness (0.95f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        for (int i = 0; i < 26; ++i)
            brush (g, r.getX() + 8.0f + rnd.nextFloat() * (r.getWidth() - 16.0f),
                   r.getY() + 5.0f + rnd.nextFloat() * (r.getHeight() - 10.0f),
                   8.0f + 8.0f * rnd.nextFloat(), 3.0f, (rnd.nextFloat() - 0.5f) * 0.6f,
                   jitter (rnd.nextFloat() < 0.5f ? Palette::time.brighter (0.4f) : Palette::time.darker (0.3f), rnd).withAlpha (0.6f));
    }
    else
    {
        g.setColour (cream.withAlpha (highlighted ? 0.95f : 0.8f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    }
    g.setColour (ink.withAlpha (0.45f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.3f);
}

void PaintedLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (serif (16.0f, true));
    g.setColour (b.getToggleState() ? cream : ink);
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
GivernyEditor::GivernyEditor (GivernyProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    setupKnob (reverb,   "reverb",   "Reverb",   Palette::reverb);
    setupKnob (delay,    "delay",    "Delay",    Palette::delay);
    setupKnob (tape,     "tape",     "Tape",     Palette::tape);
    setupKnob (phase,    "phase",    "Phase",    Palette::phase);
    setupKnob (blend,    "blend",    "Blend",    Palette::blend, true);
    setupKnob (dark,     "dark",     "Dark",     Palette::dark);
    setupKnob (light,    "light",    "Light",    Palette::light);
    setupKnob (distance, "distance", "Distance", Palette::distance);
    setupKnob (time,     "time",     "Time",     Palette::time);
    setupKnob (repeats,  "repeats",  "Repeats",  Palette::repeats);

    // Time shows a note value when synced, milliseconds when free
    time.slider.textFromValueFunction = [this] (double v)
    {
        if (processor.apvts.getRawParameterValue ("sync")->load() > 0.5f)
            return juce::String (GivernyProcessor::divisionName (GivernyProcessor::divisionIndex ((float) v)));
        return juce::String (juce::roundToInt (GivernyProcessor::freeTimeMs ((float) v))) + " ms";
    };
    time.slider.updateText();

    syncButton.setClickingTogglesState (true);
    addAndMakeVisible (syncButton);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "sync", syncButton);

    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setFont (PaintedLookAndFeel::serif (13.0f, true));
    bpmLabel.setColour (juce::Label::textColourId, ink.withAlpha (0.75f));
    addAndMakeVisible (bpmLabel);

    setSize (900, 640);
    startTimerHz (30);
}

GivernyEditor::~GivernyEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GivernyEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, juce::Colour c, bool big)
{
    k.slider.setComponentID (paramId);
    k.slider.getProperties().set ("paint", (juce::int64) c.getARGB());
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setColour (juce::Slider::textBoxTextColourId,       ink);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (PaintedLookAndFeel::serif (big ? 26.0f : 19.0f, true));
    k.label.setColour (juce::Label::textColourId, ink);
    k.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k.slider);
}

void GivernyEditor::placeKnob (Knob& k, juce::Rectangle<int> area, int size)
{
    k.label.setBounds (area.removeFromTop (26));
    k.slider.setBounds (area.withSizeKeepingCentre (size, size + 22).withY (area.getY()));
}

//==============================================================================
void GivernyEditor::resized()
{
    auto area = getLocalBounds().reduced (22);
    area.removeFromTop (58);                                    // title

    auto bottom = area.removeFromBottom (170);
    area.removeFromBottom (12);

    leftPanel  = area.removeFromLeft (200);
    rightPanel = area.removeFromRight (200);
    area.reduce (14, 0);
    pondArea   = area.removeFromTop (128);
    area.removeFromTop (6);
    blendArea  = area;

    tonePanel = bottom.removeFromLeft (bottom.getWidth() / 2 - 6);
    bottom.removeFromLeft (12);
    timePanel = bottom;

    auto lp = leftPanel.reduced (10, 8);
    placeKnob (reverb, lp.removeFromTop (lp.getHeight() / 2), 100);
    placeKnob (delay,  lp, 100);

    auto rp = rightPanel.reduced (10, 8);
    placeKnob (tape,  rp.removeFromTop (rp.getHeight() / 2), 100);
    placeKnob (phase, rp, 100);

    placeKnob (blend, blendArea.withTrimmedTop (6), 156);

    auto tp = tonePanel.reduced (10, 10);
    const int tw = tp.getWidth() / 3;
    placeKnob (dark,     tp.removeFromLeft (tw), 104);
    placeKnob (light,    tp.removeFromLeft (tw), 104);
    placeKnob (distance, tp, 104);

    auto mp = timePanel.reduced (10, 10);
    const int mw = mp.getWidth() / 3;
    placeKnob (time, mp.removeFromLeft (mw), 104);
    auto syncCol = mp.removeFromLeft (mw);
    placeKnob (repeats, mp, 104);
    syncButton.setBounds (syncCol.withSizeKeepingCentre (110, 44).translated (0, -6));
    bpmLabel.setBounds (syncButton.getBounds().translated (0, 44).withHeight (22).expanded (20, 0));

    paintBackground();
}

//==============================================================================
void GivernyEditor::paintBackground()
{
    const int W = getWidth(), H = getHeight();
    background = juce::Image (juce::Image::ARGB, W, H, true);
    juce::Graphics g (background);
    juce::Random r (1872);

    // Base wash: dawn sky into the lily pond
    juce::ColourGradient wash (juce::Colour (0xfff3d2b3), 0.0f, 0.0f, juce::Colour (0xff35588a), 0.0f, (float) H, false);
    wash.addColour (0.18, juce::Colour (0xffd9c3e6));
    wash.addColour (0.40, juce::Colour (0xff9ec3df));
    wash.addColour (0.70, juce::Colour (0xff5d93b9));
    g.setGradientFill (wash);
    g.fillAll();

    // Broad horizontal strokes, then smaller dabs on top
    for (int i = 0; i < 2600; ++i)
    {
        const float y = r.nextFloat() * (float) H;
        brush (g, r.nextFloat() * (float) W, y, 20.0f + 45.0f * r.nextFloat(), 6.0f + 8.0f * r.nextFloat(),
               (r.nextFloat() - 0.5f) * 0.25f, paletteAt (y / (float) H, r).withAlpha (0.12f + 0.12f * r.nextFloat()));
    }
    for (int i = 0; i < 5200; ++i)
    {
        const float y = r.nextFloat() * (float) H;
        brush (g, r.nextFloat() * (float) W, y, 4.0f + 11.0f * r.nextFloat(), 2.0f + 3.0f * r.nextFloat(),
               (r.nextFloat() - 0.5f) * 0.5f, paletteAt (y / (float) H, r).withAlpha (0.22f + 0.25f * r.nextFloat()));
    }

    // Sunlight glints on the water
    for (int i = 0; i < 260; ++i)
        brush (g, r.nextFloat() * (float) W, (0.35f + 0.6f * r.nextFloat()) * (float) H,
               6.0f + 14.0f * r.nextFloat(), 1.5f + 1.5f * r.nextFloat(), (r.nextFloat() - 0.5f) * 0.15f,
               juce::Colour (0xfffff3c8).withAlpha (0.25f + 0.3f * r.nextFloat()));

    // Willow fronds hanging from the top corners
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < 220; ++i)
        {
            const float x0 = side == 0 ? r.nextFloat() * 170.0f : (float) W - r.nextFloat() * 170.0f;
            const float y0 = r.nextFloat() * 90.0f - 20.0f;
            const juce::Colour greens[] = { juce::Colour (0xff5e8f4e), juce::Colour (0xff7daa5a), juce::Colour (0xffa3c46c), juce::Colour (0xff4f7a55) };
            brush (g, x0, y0 + 20.0f, 14.0f + 30.0f * r.nextFloat(), 2.0f + 2.5f * r.nextFloat(),
                   gv::pi * 0.5f + (r.nextFloat() - 0.5f) * 0.5f, jitter (greens[r.nextInt (4)], r).withAlpha (0.35f + 0.3f * r.nextFloat()));
        }

    // Lily pads and blossoms in the lower water
    for (int i = 0; i < 34; ++i)
    {
        const float cx = r.nextFloat() * (float) W;
        const float cy = (0.45f + 0.55f * r.nextFloat()) * (float) H;
        const float pw = 26.0f + 46.0f * r.nextFloat();
        const float ph = pw * (0.32f + 0.08f * r.nextFloat());
        const juce::Colour pad = jitter (juce::Colour (0xff6c9a52), r, 0.1f);

        juce::Path leaf;
        leaf.addPieSegment (cx - pw * 0.5f, cy - ph * 0.5f, pw, ph, 0.25f, gv::twoPi - 0.15f, 0.0f);
        g.setColour (pad.withAlpha (0.6f));
        g.fillPath (leaf);
        for (int k = 0; k < 10; ++k)
            brush (g, cx + (r.nextFloat() - 0.5f) * pw * 0.7f, cy + (r.nextFloat() - 0.5f) * ph * 0.6f,
                   pw * 0.25f, 2.5f, (r.nextFloat() - 0.5f) * 0.3f,
                   jitter (r.nextBool() ? pad.brighter (0.4f) : pad.darker (0.3f), r).withAlpha (0.5f));

        if (r.nextFloat() < 0.45f)
        {
            const juce::Colour petals[] = { juce::Colour (0xfff4a6c0), juce::Colour (0xfffbe3ec), juce::Colour (0xffe86f7a), juce::Colour (0xfff7f0f5) };
            const auto pc = petals[r.nextInt (4)];
            const float fx = cx + (r.nextFloat() - 0.5f) * pw * 0.3f, fy = cy - ph * 0.25f;
            for (int k = 0; k < 7; ++k)
            {
                const float a = -gv::pi * (0.1f + 0.8f * (float) k / 6.0f);
                brush (g, fx + std::cos (a) * 5.0f, fy + std::sin (a) * 4.0f, 9.0f, 4.0f, a, jitter (pc, r).withAlpha (0.85f));
            }
            g.setColour (juce::Colour (0xfff7d67a).withAlpha (0.9f));
            g.fillEllipse (fx - 2.5f, fy - 3.0f, 5.0f, 4.0f);
        }
    }

    // Gentle vignette
    juce::ColourGradient vig (juce::Colours::transparentBlack, (float) W * 0.5f, (float) H * 0.45f,
                              ink.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vig);
    g.fillAll();

    // Paper plaques behind the controls (keeps the text readable)
    auto plaque = [&] (juce::Rectangle<int> rc, float alpha)
    {
        auto rf = rc.toFloat();
        g.setColour (ink.withAlpha (0.18f));
        g.fillRoundedRectangle (rf.translated (2.0f, 3.0f), 16.0f);
        g.setColour (juce::Colour (0xfff8eed6).withAlpha (alpha));
        g.fillRoundedRectangle (rf, 16.0f);
        for (int i = 0; i < 400; ++i)  // paper grain
        {
            g.setColour ((r.nextBool() ? juce::Colours::white : juce::Colour (0xffd9c9a8)).withAlpha (0.12f));
            g.fillEllipse (rf.getX() + 8.0f + r.nextFloat() * (rf.getWidth() - 16.0f),
                           rf.getY() + 8.0f + r.nextFloat() * (rf.getHeight() - 16.0f), 2.0f, 2.0f);
        }
        for (int k = 0; k < 3; ++k)    // hand-drawn edge
        {
            g.setColour (ink.withAlpha (0.10f + 0.05f * (float) k));
            g.drawRoundedRectangle (rf.translated ((r.nextFloat() - 0.5f) * 2.0f, (r.nextFloat() - 0.5f) * 2.0f), 16.0f, 1.0f);
        }
    };

    plaque (leftPanel, 0.66f);
    plaque (rightPanel, 0.66f);
    plaque (tonePanel, 0.68f);
    plaque (timePanel, 0.68f);
    plaque (blendArea.withSizeKeepingCentre (236, blendArea.getHeight()), 0.58f);

    // Group captions

    // Title, signed like a painting
    auto title = getLocalBounds().reduced (30, 0).withHeight (64).withY (14);
    g.setFont (PaintedLookAndFeel::serif (44.0f, true));
    g.setColour (cream.withAlpha (0.55f));
    g.drawText ("Giverny 3", title.translated (2, 2), juce::Justification::centredLeft);
    g.setColour (ink);
    g.drawText ("Giverny 3", title, juce::Justification::centredLeft);
    for (int i = 0; i < 22; ++i)
        brush (g, 36.0f + (float) i * 9.0f, 70.0f + std::sin ((float) i * 0.6f) * 1.5f, 14.0f, 3.0f, 0.05f,
               jitter (Palette::tape, r).withAlpha (0.55f));

    g.setFont (PaintedLookAndFeel::serif (15.0f, true));
    g.setColour (ink.withAlpha (0.75f));
    g.drawText ("reverb, delay, tape and phase, adrift on the water", title.withTrimmedLeft (232),
                juce::Justification::centredLeft);
}

//==============================================================================
void GivernyEditor::timerCallback()
{
    clock += 1.0f / 30.0f;

    const bool sync = processor.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    if (sync != lastSync) { lastSync = sync; time.slider.updateText(); }

    const double bpm = processor.hudBpm.load();
    bpmLabel.setText (sync ? (juce::String (juce::roundToInt (bpm)) + " bpm" + (processor.hudHostBpm.load() ? " from host" : " (default)"))
                           : juce::String ("free time"),
                      juce::dontSendNotification);

    // Ripples: new ones bloom when you play
    const float in = processor.hudInput.load();
    if (in > 0.02f && (in > lastInput * 1.4f || rng.nextFloat() < in * 0.25f) && ripples.size() < 24)
        ripples.push_back ({ rng.nextFloat(), 0.2f + 0.7f * rng.nextFloat(), 0.0f, 1.0f });
    lastInput = in;

    for (auto& rp : ripples) { rp.r += 0.9f; rp.life -= 0.012f; }
    ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& rp) { return rp.life <= 0.0f; }),
                   ripples.end());

    repaint (pondArea.expanded (4));
}

void GivernyEditor::paint (juce::Graphics& g)
{
    if (background.isValid())
        g.drawImageAt (background, 0, 0);
    drawPond (g, pondArea.toFloat());
}

void GivernyEditor::drawPond (juce::Graphics& g, juce::Rectangle<float> r)
{
    // Pond window
    g.setColour (ink.withAlpha (0.25f));
    g.fillRoundedRectangle (r.translated (2.0f, 3.0f), 18.0f);

    juce::Graphics::ScopedSaveState ss (g);
    juce::Path frame; frame.addRoundedRectangle (r, 18.0f);
    g.reduceClipRegion (frame);

    juce::ColourGradient water (juce::Colour (0xff86b5d6), r.getX(), r.getY(),
                                juce::Colour (0xff3e6e9e), r.getX(), r.getBottom(), false);
    water.addColour (0.5, juce::Colour (0xff5f95bf));
    g.setGradientFill (water);
    g.fillRect (r);

    // Drifting reflections (deterministic, slowly moving)
    juce::Random wr (7);
    for (int i = 0; i < 90; ++i)
    {
        const float x = std::fmod (wr.nextFloat() * r.getWidth() + clock * (4.0f + 6.0f * wr.nextFloat()), r.getWidth());
        const float y = wr.nextFloat() * r.getHeight();
        const juce::Colour cs[] = { juce::Colour (0xffb6a3dd), juce::Colour (0xff9fd0c8), juce::Colour (0xfff2c9a5), juce::Colour (0xff4e86b0) };
        brush (g, r.getX() + x, r.getY() + y, 10.0f + 20.0f * wr.nextFloat(), 3.0f + 3.0f * wr.nextFloat(),
               (wr.nextFloat() - 0.5f) * 0.2f, cs[wr.nextInt (4)].withAlpha (0.25f));
    }

    // Ripples from your playing
    for (auto& rp : ripples)
    {
        const float cx = r.getX() + rp.x * r.getWidth(), cy = r.getY() + rp.y * r.getHeight();
        for (int k = 0; k < 2; ++k)
        {
            const float rad = rp.r * (1.0f - 0.35f * (float) k);
            g.setColour (cream.withAlpha (0.45f * rp.life * (1.0f - 0.4f * (float) k)));
            g.drawEllipse (cx - rad, cy - rad * 0.4f, rad * 2.0f, rad * 0.8f, 1.4f);
        }
    }

    // Effect lilies: slide between the two chains as the drift engine morphs
    const float w = processor.hudMorph.load();
    const float sm = w * w * (3.0f - 2.0f * w);
    std::array<float, 4> slot {}, lvl {};
    for (int id = 0; id < 4; ++id)
    {
        float sA = 0.0f, sB = 0.0f, lA = 0.0f, lB = 0.0f;
        for (int k = 0; k < 4; ++k)
        {
            if (processor.hud[0].order[k].load() == id) { sA = (float) k; lA = processor.hud[0].level[k].load(); }
            if (processor.hud[1].order[k].load() == id) { sB = (float) k; lB = processor.hud[1].level[k].load(); }
        }
        slot[(size_t) id] = sA + (sB - sA) * sm;
        lvl[(size_t) id]  = lA + (lB - lA) * sm;
    }

    const float left = r.getX() + 62.0f, right = r.getRight() - 62.0f;
    const float cy = r.getCentreY() - 8.0f;
    auto xFor = [&] (float s) { return left + (right - left) * s / 3.0f; };

    // Signal path: a flowing painted line from "in" to "out"
    std::array<int, 4> byPos { 0, 1, 2, 3 };
    std::sort (byPos.begin(), byPos.end(), [&] (int a, int b) { return slot[(size_t) a] < slot[(size_t) b]; });
    juce::Path flow;
    flow.startNewSubPath (r.getX() + 14.0f, cy);
    float px = r.getX() + 14.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float x = xFor (slot[(size_t) byPos[(size_t) i]]);
        flow.cubicTo (px + (x - px) * 0.4f, cy + 14.0f * std::sin (clock * 0.8f + (float) i),
                      px + (x - px) * 0.6f, cy - 14.0f * std::sin (clock * 0.8f + (float) i), x, cy);
        px = x;
    }
    flow.cubicTo (px + 20.0f, cy + 10.0f, r.getRight() - 40.0f, cy - 10.0f, r.getRight() - 14.0f, cy);
    g.setColour (cream.withAlpha (0.25f));
    g.strokePath (flow, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (cream.withAlpha (0.6f));
    g.strokePath (flow, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (PaintedLookAndFeel::serif (13.0f, true));
    g.setColour (cream.withAlpha (0.85f));
    g.drawText ("in",  juce::Rectangle<float> (r.getX() + 8.0f, cy + 6.0f, 30.0f, 16.0f), juce::Justification::centredLeft);
    g.drawText ("out", juce::Rectangle<float> (r.getRight() - 38.0f, cy + 6.0f, 30.0f, 16.0f), juce::Justification::centredRight);

    for (int id = 0; id < 4; ++id)
    {
        const float x   = xFor (slot[(size_t) id]);
        const float l   = lvl[(size_t) id];
        const float pad = 18.0f + 16.0f * l;
        const float bob = std::sin (clock * 1.3f + (float) id * 1.7f) * 1.5f;
        juce::Random lr (id * 31 + 5);

        // Pad
        juce::Path leaf;
        leaf.addPieSegment (x - pad, cy - pad * 0.45f + bob, pad * 2.0f, pad * 0.9f, 0.3f, gv::twoPi - 0.1f, 0.0f);
        g.setColour (juce::Colour (0xff5e8f4e).withAlpha (0.85f));
        g.fillPath (leaf);
        for (int k = 0; k < 8; ++k)
            brush (g, x + (lr.nextFloat() - 0.5f) * pad * 1.2f, cy + bob + (lr.nextFloat() - 0.5f) * pad * 0.4f,
                   pad * 0.5f, 3.0f, (lr.nextFloat() - 0.5f) * 0.3f, juce::Colour (0xffa3c46c).withAlpha (0.45f));

        // Blossom in the effect's colour, opening with its intensity
        const auto col = effectColours[id];
        const float bloom = 5.0f + 11.0f * l;
        const int petals = 8;
        for (int k = 0; k < petals; ++k)
        {
            const float a = gv::twoPi * (float) k / petals + clock * 0.1f;
            brush (g, x + std::cos (a) * bloom * 0.6f, cy - 4.0f + bob + std::sin (a) * bloom * 0.45f,
                   bloom, 3.0f + 3.0f * l, a, jitter (col, lr).withAlpha (0.35f + 0.6f * l));
        }
        g.setColour (juce::Colour (0xfff7d67a).withAlpha (0.4f + 0.6f * l));
        g.fillEllipse (x - 3.0f, cy - 7.0f + bob, 6.0f, 5.0f);

        // Name
        g.setFont (PaintedLookAndFeel::serif (14.0f, true));
        g.setColour (ink.withAlpha (0.5f));
        g.drawText (effectNames[id], juce::Rectangle<float> (x - 40.0f, cy + 20.0f, 80.0f, 18.0f).translated (1.0f, 1.0f), juce::Justification::centred);
        g.setColour (cream);
        g.drawText (effectNames[id], juce::Rectangle<float> (x - 40.0f, cy + 20.0f, 80.0f, 18.0f), juce::Justification::centred);
    }

    // Status line
    const float blendV = processor.apvts.getRawParameterValue ("blend")->load();
    g.setFont (PaintedLookAndFeel::serif (12.5f, true));
    g.setColour (cream.withAlpha (0.85f));
    const juce::String status = blendV < 0.01f ? "still water: fixed order"
                              : (w > 0.01f && w < 0.99f ? "the current is shifting..." : "drifting");
    g.drawText (status, r.withTrimmedTop (r.getHeight() - 20.0f).withTrimmedBottom (4.0f), juce::Justification::centred);

    // Hand-drawn frame
    g.setColour (ink.withAlpha (0.45f));
    g.drawRoundedRectangle (r.reduced (0.75f), 18.0f, 1.5f);
}
