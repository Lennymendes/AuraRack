// AuraRack — NextDSP · barre globale.

#include "TopBar.h"

namespace aura::ui
{
using namespace colours;

BarKnob::BarKnob (juce::RangedAudioParameter& parameter, const juce::String& name)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, levelOk);
    addAndMakeVisible (slider);

    nameLabel.setText (name.toUpperCase(), juce::dontSendNotification);
    nameLabel.setFont (NextLookAndFeel::font (10.0f, true));
    nameLabel.setColour (juce::Label::textColourId, textDim);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setFont (NextLookAndFeel::font (12.5f, true));
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);

    attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, slider, nullptr);
    slider.onValueChange = [this] { valueLabel.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification); };
    slider.onValueChange();
}

void BarKnob::resized()
{
    auto r = getLocalBounds();
    slider.setBounds (r.removeFromLeft (r.getHeight()).reduced (2));
    r.removeFromLeft (4);
    nameLabel.setBounds (r.removeFromTop (r.getHeight() / 2).withTrimmedTop (4));
    valueLabel.setBounds (r);
}

//==============================================================================
TopBar::TopBar (AuraRackProcessor& p)
    : processor (p),
      inputKnob  (*p.getValueTreeState().getParameter (aura::params::inGain),    "Input"),
      outputKnob (*p.getValueTreeState().getParameter (aura::params::outGain),   "Output"),
      mixKnob    (*p.getValueTreeState().getParameter (aura::params::masterMix), "Master Mix")
{
    for (auto* k : { &inputKnob, &outputKnob, &mixKnob })
        addAndMakeVisible (*k);

    auto* sc = dynamic_cast<juce::AudioParameterChoice*> (p.getValueTreeState().getParameter (aura::params::scSource));
    jassert (sc != nullptr);
    sidechainBox.addItemList (sc->choices, 1);
    sidechainAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*sc, sidechainBox, nullptr);
    addAndMakeVisible (sidechainBox);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    r.removeFromLeft (150);   // titre
    inputKnob.setBounds  (r.removeFromLeft (118));
    outputKnob.setBounds (r.removeFromLeft (118));
    mixKnob.setBounds    (r.removeFromLeft (140));
    r.removeFromLeft (14);

    sidechainArea = r.removeFromLeft (210);
    sidechainBox.setBounds (sidechainArea.withTrimmedLeft (66).withSizeKeepingCentre (sidechainArea.getWidth() - 66, 26));
    r.removeFromLeft (18);

    // Niveaux : deux barres Input, deux barres Output.
    auto meters = r.removeFromLeft (230).toFloat();
    inBars  = meters.removeFromTop (meters.getHeight() * 0.5f).reduced (0.0f, 3.0f);
    outBars = meters.reduced (0.0f, 3.0f);
    r.removeFromLeft (14);

    infoArea = r.removeFromLeft (170);
    cpuBar = r.toFloat().withSizeKeepingCentre (r.getWidth() - 4.0f, 6.0f).withTrimmedLeft (30.0f);
}

void TopBar::tick (float dt)
{
    const auto& m = processor.getMeters();
    // exchange(0) : lecture + remise à zéro de la crête maintenue par le thread audio.
    const bool a = inL.update  (m.inputPeak[0].exchange (0.0f, std::memory_order_relaxed), dt);
    const bool b = inR.update  (m.inputPeak[1].exchange (0.0f, std::memory_order_relaxed), dt);
    const bool c = outL.update (m.outputPeak[0].exchange (0.0f, std::memory_order_relaxed), dt);
    const bool d = outR.update (m.outputPeak[1].exchange (0.0f, std::memory_order_relaxed), dt);
    if (a || b) repaint (inBars.expanded (4.0f).getSmallestIntegerContainer().expanded (26, 0));
    if (c || d) repaint (outBars.expanded (4.0f).getSmallestIntegerContainer().expanded (26, 0));

    const float newCpu = processor.getCpuLoad();
    if (std::abs (newCpu - cpu) > 0.004f) { cpu = newCpu; repaint (cpuBar.expanded (40.0f, 10.0f).getSmallestIntegerContainer()); }

    const double rate = processor.getCurrentSampleRate();
    const int latency = m.activeLatency.load (std::memory_order_relaxed);
    const bool sc = processor.isSidechainConnected();
    if (std::abs (rate - shownRate) > 0.5 || latency != shownLatency || sc != shownSidechain)
    {
        shownRate = rate; shownLatency = latency; shownSidechain = sc;
        repaint (infoArea);
        repaint (sidechainArea);
    }
}

void TopBar::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRect (getLocalBounds());
    g.setColour (stroke);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Titre.
    g.setColour (text);
    g.setFont (NextLookAndFeel::font (19.0f, true));
    g.drawText ("AURARACK", 16, 10, 140, 24, juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (NextLookAndFeel::font (10.5f, true));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("NEXTDSP  \xc2\xb7  DXD")), 16, 34, 140, 14, juce::Justification::centredLeft);

    // Sidechain : étiquette + voyant (vert = bus relié par l'hôte).
    g.setColour (textDim);
    g.setFont (NextLookAndFeel::font (10.0f, true));
    g.drawText ("SIDECHAIN", sidechainArea.getX(), sidechainArea.getY(), 62, sidechainArea.getHeight(), juce::Justification::centredLeft);
    g.setColour (shownSidechain ? levelOk : stroke);
    g.fillEllipse ((float) sidechainArea.getX() + 52.0f, (float) sidechainArea.getCentreY() - 3.0f, 6.0f, 6.0f);

    // Niveaux.
    auto bar = [&g] (juce::Rectangle<float> area, const char* label, const LevelBallistics& l, const LevelBallistics& rr)
    {
        g.setColour (textDim);
        g.setFont (NextLookAndFeel::font (9.5f, true));
        g.drawText (label, area.withWidth (26.0f).toNearestInt(), juce::Justification::centredLeft);
        auto bars = area.withTrimmedLeft (28.0f);
        drawLevelBar (g, bars.removeFromTop (bars.getHeight() * 0.5f - 1.0f), l);
        drawLevelBar (g, bars.withTrimmedTop (2.0f), rr);
    };
    bar (inBars, "IN", inL, inR);
    bar (outBars, "OUT", outL, outR);

    // Fréquence d'échantillonnage, indicateur DXD, latence.
    const bool dxd = shownRate >= 352000.0;
    g.setColour (dxd ? levelOk : text);
    g.setFont (NextLookAndFeel::font (14.0f, true));
    g.drawText (shownRate > 0.0 ? juce::String (shownRate / 1000.0, 1) + " kHz" + (dxd ? "  DXD" : "") : "--",
                infoArea.removeFromTop (getHeight() / 2).withTrimmedTop (8), juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (NextLookAndFeel::font (10.5f));
    const double ms = shownRate > 0.0 ? 1000.0 * (double) std::max (shownLatency, 0) / shownRate : 0.0;
    g.drawText (juce::String (juce::CharPointer_UTF8 ("64-bit  \xc2\xb7  latence ")) + juce::String (ms, 2) + " ms", infoArea, juce::Justification::topLeft);

    // CPU.
    g.setColour (textDim);
    g.setFont (NextLookAndFeel::font (9.5f, true));
    g.drawText ("CPU", (int) cpuBar.getX() - 30, (int) cpuBar.getCentreY() - 8, 28, 16, juce::Justification::centredLeft);
    g.setColour (panelHigh);
    g.fillRoundedRectangle (cpuBar, 3.0f);
    g.setColour (cpu > 0.8f ? levelOver : (cpu > 0.5f ? levelHot : levelOk));
    g.fillRoundedRectangle (cpuBar.withWidth (cpuBar.getWidth() * juce::jlimit (0.0f, 1.0f, cpu)), 3.0f);
    g.setColour (text);
    g.setFont (NextLookAndFeel::font (10.5f, true));
    g.drawText (juce::String (juce::roundToInt (cpu * 100.0f)) + " %", (int) cpuBar.getX(), (int) cpuBar.getCentreY() + 4, (int) cpuBar.getWidth(), 14,
                juce::Justification::centred);
}

} // namespace aura::ui
