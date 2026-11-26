// AuraRack — NextDSP · façade du L9A

#include "L9aFaceplate.h"

namespace aura::ui
{
namespace
{
using L = aura::AuraL9A;
const PlateTheme& theme() { return silverPanelTheme(); }

double reductionPosition (double db)
{
    constexpr double kRange = 20.0, kSoft = 1.5;
    return std::log1p (std::clamp (db, 0.0, kRange) / kSoft) / std::log1p (kRange / kSoft);
}

/** Échelle VU classique : position proportionnelle à l'amplitude, +3 VU en bout d'arc, 0 VU à ~71 %. */
double vuPosition (double vu) { return std::clamp (std::pow (10.0, (vu - 3.0) / 20.0), 0.0, 1.0); }
}

L9aFaceplate::L9aFaceplate (juce::AudioProcessorValueTreeState& apvts, int slot)
    : gainKnob (theme()), peakKnob (theme()),
      modeSwitch (theme(), "COMP", "LIMIT", false),
      meterSwitch (theme(), "GR", "OUT", false),
      bypassSwitch (theme(), "ACTIVE", "BYPASS", true)
{
    gainKnob.bind (*apvts.getParameter (aura::params::generic (slot, L::gain)));
    gainKnob.setName ("Gain");
    peakKnob.bind (*apvts.getParameter (aura::params::generic (slot, L::peakReduction)));
    peakKnob.setName ("Peak Reduction");
    addAndMakeVisible (gainKnob);
    addAndMakeVisible (peakKnob);

    modeAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::generic (slot, L::mode)), modeSwitch, nullptr);
    meterAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::generic (slot, L::meter)), meterSwitch, nullptr);
    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::bypass (slot)), bypassSwitch, nullptr);
    for (auto* s : { &modeSwitch, &meterSwitch, &bypassSwitch }) addAndMakeVisible (*s);

    grScale.reversed = true;
    grScale.position = reductionPosition;
    grScale.caption = "GAIN REDUCTION  dB";
    for (double v : { 0.0, 1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 20.0 }) grScale.ticks.push_back ({ v, juce::String ((int) v), true, v < 0.5 });
    for (double v : { 0.5, 1.5, 2.5, 4.0, 15.0 }) grScale.ticks.push_back ({ v, {}, false, false });

    vuScale.position = vuPosition;
    vuScale.caption = "VU   0 = -18 dBFS";
    for (double v : { -20.0, -10.0, -7.0, -5.0, -3.0, 0.0, 3.0 })
        vuScale.ticks.push_back ({ v, v > 0.0 ? "+" + juce::String ((int) v) : juce::String ((int) v), true, v > -0.5 });
    for (double v : { -2.0, -1.0, 1.0, 2.0 }) vuScale.ticks.push_back ({ v, {}, false, v > 0.0 });

    bypassed = bypassSwitch.getToggleState();
    showOutput = meterSwitch.getToggleState();
    vu.setScale (showOutput ? &vuScale : &grScale);
}

void L9aFaceplate::resized()
{
    const bool wide = isWide();
    const float dw = wide ? (float) kWideWidth : (float) kCompactWidth, dh = wide ? (float) kWideHeight : (float) kCompactHeight;
    pixelScale = std::min ((float) getWidth() / dw, (float) getHeight() / dh);
    const float ox = ((float) getWidth() - dw * pixelScale) * 0.5f, oy = ((float) getHeight() - dh * pixelScale) * 0.5f;
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<float> (ox + x * pixelScale, oy + y * pixelScale, w * pixelScale, h * pixelScale); };

    plate = getLocalBounds().toFloat();
    if (wide)
    {
        gainKnob.setBounds (R (36.0f, 40.0f, 200.0f, 290.0f).toNearestInt());
        peakKnob.setBounds (R (764.0f, 40.0f, 200.0f, 290.0f).toNearestInt());
        vu.setGeometry (R (262.0f, 26.0f, 476.0f, 196.0f), pixelScale);
        logoArea = R (262.0f, 228.0f, 476.0f, 66.0f);
        modeSwitch.setBounds (R (262.0f, 306.0f, 150.0f, 58.0f).toNearestInt());
        meterSwitch.setBounds (R (425.0f, 306.0f, 150.0f, 58.0f).toNearestInt());
        bypassSwitch.setBounds (R (588.0f, 306.0f, 150.0f, 58.0f).toNearestInt());
    }
    else
    {
        vu.setGeometry (R (8.0f, 6.0f, 224.0f, 84.0f), pixelScale);
        gainKnob.setBounds (R (6.0f, 94.0f, 108.0f, 100.0f).toNearestInt());
        peakKnob.setBounds (R (126.0f, 94.0f, 108.0f, 100.0f).toNearestInt());
        logoArea = R (8.0f, 196.0f, 224.0f, 0.0f);   // pas de logo en carte compacte (la place va aux interrupteurs)
        modeSwitch.setBounds (R (6.0f, 204.0f, 74.0f, 50.0f).toNearestInt());
        meterSwitch.setBounds (R (83.0f, 204.0f, 74.0f, 50.0f).toNearestInt());
        bypassSwitch.setBounds (R (160.0f, 204.0f, 74.0f, 50.0f).toNearestInt());
    }
    vu.snap (0.0);
    brushed = makeBrushedPlate (getWidth(), getHeight(), theme(), vu.getFrame().getCentre(), vu.getFrame().getWidth() * 0.95f, std::max (6.0f, 11.0f * pixelScale));
}

void L9aFaceplate::tick (float dt, const FaceplateFeed& feed)
{
    const bool nowBypassed = bypassSwitch.getToggleState(), nowOutput = meterSwitch.getToggleState();
    const bool changed = nowBypassed != bypassed || nowOutput != showOutput;
    bypassed = nowBypassed;
    if (nowOutput != showOutput) { showOutput = nowOutput; vu.setScale (showOutput ? &vuScale : &grScale); }

    double value;
    if (showOutput) value = bypassed ? -40.0 : feed.aux - L9aFaceplate::kVuReferenceDbfs;
    else            value = bypassed ? 0.0 : feed.reductionDb;
    const bool moved = vu.advance (dt, value);

    if (changed) { repaint(); return; }
    if (moved) repaint (vu.getWindow());
}

void L9aFaceplate::paint (juce::Graphics& g)
{
    drawPlate (g, plate, theme(), brushed, pixelScale);
    drawScrews (g, plate, pixelScale);

    if (logoArea.getHeight() > 1.0f)
    {
        const auto ink = theme().engrave;
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (NextLookAndFeel::font (logoArea.getHeight() * 0.78f, true));
        g.drawText ("L9A", logoArea.withTrimmedBottom (logoArea.getHeight() * 0.22f).toNearestInt().translated (1, 1), juce::Justification::centred);
        g.setColour (ink);
        g.drawText ("L9A", logoArea.withTrimmedBottom (logoArea.getHeight() * 0.22f).toNearestInt(), juce::Justification::centred);
        g.setColour (ink.withAlpha (0.7f));
        g.setFont (NextLookAndFeel::font (logoArea.getHeight() * 0.17f, true));
        g.drawText ("OPTICAL LEVELING AMPLIFIER", logoArea.removeFromBottom (logoArea.getHeight() * 0.22f).toNearestInt(), juce::Justification::centred);
    }
    vu.paintFace (g);
}

void L9aFaceplate::paintOverChildren (juce::Graphics& g)
{
    vu.paintGlass (g);
    if (bypassed)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.excludeClipRegion (bypassSwitch.getBounds());
        g.setColour (juce::Colour (0xff10131a).withAlpha (0.38f));
        g.fillRect (plate);
    }
}

} // namespace aura::ui
