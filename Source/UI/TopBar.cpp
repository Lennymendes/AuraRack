// AuraRack — NextDSP · barre globale.

#include "TopBar.h"
#include "I18n.h"

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
      mixKnob    (*p.getValueTreeState().getParameter (aura::params::masterMix), "Master Mix"),
      widthKnob  (*p.getValueTreeState().getParameter (aura::params::width),     "Width")
{
    for (auto* k : { &inputKnob, &outputKnob, &mixKnob })
        addAndMakeVisible (*k);

    auto* sc = dynamic_cast<juce::AudioParameterChoice*> (p.getValueTreeState().getParameter (aura::params::scSource));
    jassert (sc != nullptr);
    sidechainBox.addItemList (sc->choices, 1);
    sidechainAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*sc, sidechainBox, nullptr);
    addAndMakeVisible (sidechainBox);

    gearButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<SettingsPanel> (processor), gearButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (gearButton);

    // --- Rangée 2 -------------------------------------------------------------------------------------
    auto& apvts = p.getValueTreeState();
    presetButton.setChevron (true);
    presetButton.onClick = [this]
    {
        auto menu = buildPresetMenu();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (presetButton.getWidth()),
                            [this] (int result)
                            {
                                if (result == 1000) processor.clearRack();
                                else if (result >= 1) processor.loadFactoryPreset (result - 1);
                            });
    };
    abA.onClick = [this] { processor.selectAb (0); };
    abB.onClick = [this] { processor.selectAb (1); };
    abCopy.onClick = [this] { processor.copyToOtherAb(); };
    abCopy.setAccent (levelOk);
    abA.setLabel ("A"); abB.setLabel ("B"); abCopy.setLabel ("A>B");
    for (auto* b : { &presetButton, &abA, &abB, &abCopy }) addAndMakeVisible (*b);

    rackAgcToggle.setClickingTogglesState (true);
    rackAgcToggle.setColour (juce::ToggleButton::tickColourId, levelOk);
    rackAgcAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::autoGain), rackAgcToggle, nullptr);
    monoToggle.setClickingTogglesState (true);
    monoToggle.setColour (juce::ToggleButton::tickColourId, levelHot);
    monoAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::mono), monoToggle, nullptr);
    auto* bass = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (aura::params::bassMono));
    jassert (bass != nullptr);
    bassBox.addItemList (bass->choices, 1);
    bassAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*bass, bassBox, nullptr);
    for (auto* c : std::initializer_list<juce::Component*> { &rackAgcToggle, &monoToggle, &widthKnob, &bassBox }) addAndMakeVisible (*c);
    refreshToolTexts();
}

juce::PopupMenu TopBar::buildPresetMenu() const
{
    juce::PopupMenu menu;
    const bool en = i18n::getLanguageIndex() == (int) i18n::Lang::en;
    const auto& all = aura::presets::factory();
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        const auto name = juce::String (juce::CharPointer_UTF8 (en ? all[i].nameEn : all[i].nameFr));
        const auto desc = juce::String (juce::CharPointer_UTF8 (en ? all[i].descEn : all[i].descFr));
        menu.addItem ((int) i + 1, name + juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   ")) + desc, true, processor.getPresetIndex() == (int) i);
    }
    menu.addSeparator();
    menu.addItem (1000, i18n::tr (i18n::Key::presetEmpty));
    return menu;
}

void TopBar::refreshToolTexts()
{
    const bool info = ! presetButton.getTooltip().isEmpty();
    juce::ignoreUnused (info);
}

void TopBar::setInfoMode (bool on)
{
    auto tip = [on] (i18n::Key k) { return on ? i18n::tr (k) : juce::String(); };
    presetButton.setTooltip (tip (i18n::Key::helpPreset));
    abA.setTooltip (tip (i18n::Key::helpAb));
    abB.setTooltip (tip (i18n::Key::helpAb));
    abCopy.setTooltip (tip (i18n::Key::helpCopy));
    rackAgcToggle.setTooltip (tip (i18n::Key::helpAutoGainRack));
    widthKnob.setTooltip (tip (i18n::Key::helpUtility));
    bassBox.setTooltip (tip (i18n::Key::helpUtility));
    monoToggle.setTooltip (tip (i18n::Key::helpUtility));
    gearButton.setTooltip (on ? i18n::tr (i18n::Key::helpSettings) : juce::String());
    sidechainBox.setTooltip (on ? juce::String (juce::CharPointer_UTF8 ("Sidechain : bus externe VST3 / AU ou entr\xc3\xa9" "e du rack")) : juce::String());
}

void TopBar::resized()
{
    auto r = getLocalBounds().removeFromTop (kRow1Height).reduced (16, 10);
    gearButton.setBounds (r.removeFromRight (40).withSizeKeepingCentre (38, 38));
    r.removeFromRight (8);
    r.removeFromLeft (140);   // titre
    inputKnob.setBounds  (r.removeFromLeft (112));
    outputKnob.setBounds (r.removeFromLeft (112));
    mixKnob.setBounds    (r.removeFromLeft (132));
    r.removeFromLeft (14);

    sidechainArea = r.removeFromLeft (190);
    sidechainBox.setBounds (sidechainArea.withTrimmedLeft (66).withSizeKeepingCentre (sidechainArea.getWidth() - 66, 26));
    r.removeFromLeft (16);

    // Niveaux : deux barres Input, deux barres Output.
    auto meters = r.removeFromLeft (200).toFloat();
    inBars  = meters.removeFromTop (meters.getHeight() * 0.5f).reduced (0.0f, 3.0f);
    outBars = meters.reduced (0.0f, 3.0f);
    r.removeFromLeft (14);

    infoArea = r.removeFromLeft (160);
    cpuBar = r.toFloat().withSizeKeepingCentre (r.getWidth() - 4.0f, 6.0f).withTrimmedLeft (30.0f);

    // Rangée 2 : presets | A B copie | auto-gain | utilitaire stéréo | latence totale.
    row2 = getLocalBounds().withTrimmedTop (kRow1Height).reduced (16, 3);
    auto q = row2;
    presetButton.setBounds (q.removeFromLeft (236));
    q.removeFromLeft (10);
    abA.setBounds (q.removeFromLeft (34));
    q.removeFromLeft (4);
    abB.setBounds (q.removeFromLeft (34));
    q.removeFromLeft (4);
    abCopy.setBounds (q.removeFromLeft (46));
    q.removeFromLeft (18);
    rackAgcToggle.setBounds (q.removeFromLeft (104));
    q.removeFromLeft (14);
    widthKnob.setBounds (q.removeFromLeft (128));
    q.removeFromLeft (8);
    bassCaption = q.removeFromLeft (70);
    bassBox.setBounds (q.removeFromLeft (92).withSizeKeepingCentre (92, 24));
    q.removeFromLeft (8);
    monoToggle.setBounds (q.removeFromLeft (66));
    q.removeFromLeft (10);
    corrArea = q.removeFromLeft (130);
    q.removeFromLeft (14);
    latencyArea = q;   // le reste : latence totale
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

    // Rangée 2 : A / B, libellé du preset (modifié ?), corrélation, latence totale.
    {
        const int ab = processor.getAbActive();
        if (ab != shownAb)
        {
            shownAb = ab;
            abA.setActive (ab == 0);
            abB.setActive (ab == 1);
            abCopy.setLabel (ab == 0 ? "A>B" : "B>A");
        }
        const int pi = processor.getPresetIndex();
        if (++presetClock >= 20 || pi != shownPreset)
        {
            presetClock = 0;
            const bool modified = processor.isPresetModified();
            if (pi != shownPreset || modified != shownModified)
            {
                shownPreset = pi; shownModified = modified;
                const bool en = i18n::getLanguageIndex() == (int) i18n::Lang::en;
                auto name = pi >= 0 ? juce::String (juce::CharPointer_UTF8 (en ? aura::presets::factory()[(std::size_t) pi].nameEn : aura::presets::factory()[(std::size_t) pi].nameFr))
                                    : i18n::tr (i18n::Key::presetNone);
                presetButton.setLabel (i18n::tr (i18n::Key::preset) + juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   ")) + name + (modified ? " *" : ""));
            }
        }
        const float c = m.correlation.load (std::memory_order_relaxed);
        if (std::abs (c - correlation) > 0.01f) { correlation = c; repaint (corrArea); }
        const int total = m.activeLatency.load (std::memory_order_relaxed);
        if (total != shownLatencyTotal || std::abs (processor.getCurrentSampleRate() - shownRate) > 0.5) { shownLatencyTotal = total; repaint (latencyArea); }
    }

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
    g.drawText (i18n::tr (i18n::Key::sidechain), sidechainArea.getX(), sidechainArea.getY(), 62, sidechainArea.getHeight(), juce::Justification::centredLeft);
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
                infoArea.removeFromTop (kRow1Height / 2).withTrimmedTop (8), juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (NextLookAndFeel::font (10.5f));
    const double ms = shownRate > 0.0 ? 1000.0 * (double) std::max (shownLatency, 0) / shownRate : 0.0;
    juce::ignoreUnused (ms);
    g.drawText ("64-bit", infoArea, juce::Justification::topLeft);

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

    // --- Rangée 2 --------------------------------------------------------------------------------------
    g.setColour (stroke);
    g.drawHorizontalLine (kRow1Height - 1, 12.0f, (float) getWidth() - 12.0f);

    g.setColour (textDim);
    g.setFont (NextLookAndFeel::font (9.5f, true));
    g.drawText (i18n::tr (i18n::Key::bassMono), bassCaption.withTrimmedRight (4), juce::Justification::centredRight);

    // Corrélation L/R : barre bipolaire (-1 … +1), verte vers +1, orange vers 0, rouge négatif.
    {
        auto a = corrArea.toFloat();
        g.setColour (textDim);
        g.drawText (i18n::tr (i18n::Key::correlation), a.removeFromLeft (44.0f).toNearestInt(), juce::Justification::centredLeft);
        auto bar = a.withSizeKeepingCentre (a.getWidth(), 8.0f);
        g.setColour (panelHigh);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (stroke);
        g.drawVerticalLine ((int) bar.getCentreX(), bar.getY() - 2.0f, bar.getBottom() + 2.0f);
        const float x = bar.getX() + bar.getWidth() * 0.5f * (1.0f + juce::jlimit (-1.0f, 1.0f, correlation));
        g.setColour (correlation < 0.0f ? levelOver : (correlation < 0.3f ? levelHot : levelOk));
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (x, bar.getCentreX()), bar.getY(), std::max (2.0f, std::abs (x - bar.getCentreX())), bar.getHeight()), 3.0f);
    }

    // Latence totale (modules + Lookahead E9DS, annoncée à l'hôte pour la PDC).
    {
        auto a = latencyArea.toFloat();
        const int samples = std::max (shownLatencyTotal, 0);
        const double msTotal = shownRate > 0.0 ? 1000.0 * (double) samples / shownRate : 0.0;
        g.setColour (textDim);
        g.setFont (NextLookAndFeel::font (9.5f, true));
        g.drawText (i18n::tr (i18n::Key::latencyTotal) + "  (PDC)", a.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredRight);
        g.setColour (samples == 0 ? levelOk : text);
        g.setFont (NextLookAndFeel::font (14.0f, true));
        g.drawText (juce::String (samples) + " " + i18n::tr (i18n::Key::samplesShort) + juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   ")) + juce::String (msTotal, 2) + " ms",
                    a.toNearestInt(), juce::Justification::centredRight);
    }
}

} // namespace aura::ui
