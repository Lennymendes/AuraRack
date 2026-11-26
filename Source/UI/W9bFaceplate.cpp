// AuraRack — NextDSP · façade du W9BMK1

#include "W9bFaceplate.h"

namespace aura::ui
{
namespace
{
using W = aura::AuraW9B;

/** Police script pour le logo : première installée parmi une liste, sinon serif italique. */
juce::Font findLogoFont (float height)
{
    static const juce::StringArray installed = juce::Font::findAllTypefaceNames();
    for (auto* n : { "Snell Roundhand", "Apple Chancery", "Zapfino", "Segoe Script", "Lucida Calligraphy", "Brush Script MT",
                     "URW Chancery L", "Z003", "Lucida Handwriting", "Monotype Corsiva" })
        if (installed.contains (n))
            return juce::Font (juce::FontOptions (juce::String (n), height, juce::Font::plain));
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), height, juce::Font::italic));
}

const ParamSpec& spec (int p) { return W::info().params[(std::size_t) p]; }
}

W9bFaceplate::W9bFaceplate (juce::AudioProcessorValueTreeState& apvts, int slot)
    : knobs { PlateKnob (blueAnodisedTheme()), PlateKnob (blueAnodisedTheme()), PlateKnob (blueAnodisedTheme()), PlateKnob (blueAnodisedTheme()),
              PlateKnob (blueAnodisedTheme()), PlateKnob (blueAnodisedTheme()), PlateKnob (blueAnodisedTheme()) },
      autoSwitch (blueAnodisedTheme(), "MANUAL", "AUTO", false),
      bypassSwitch (blueAnodisedTheme(), "ACTIVE", "BYPASS", true)
{
    struct Binding { Cell cell; int param; const char* name; };
    static constexpr Binding bindings[] = { { cInput, W::input, "Input" }, { cThresh, W::threshold, "Thresh" }, { cRatio, W::ratio, "Ratio" },
                                            { cFocus, W::focus, "Focus" }, { cAttack, W::attack, "Attack" }, { cRelease, W::release, "Release" },
                                            { cOutput, W::output, "Output" } };
    for (const auto& b : bindings)
    {
        auto& k = knobs[(std::size_t) b.cell];
        k.bind (*apvts.getParameter (aura::params::generic (slot, b.param)));
        k.setName (b.name);
        addAndMakeVisible (k);
    }
    autoAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::generic (slot, W::autoFocus)), autoSwitch, nullptr);
    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::bypass (slot)), bypassSwitch, nullptr);
    addAndMakeVisible (autoSwitch);
    addAndMakeVisible (bypassSwitch);

    // Échelle de lecture : réduction de gain, 0 à droite (l'aiguille part vers la gauche quand on comprime).
    scale.reversed = true;
    scale.position = [] (double db) { return reductionPosition (db); };
    scale.caption = "GAIN REDUCTION  dB";
    for (double v : { 0.0, 1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 20.0 }) scale.ticks.push_back ({ v, juce::String ((int) v), true, v < 0.5 });
    for (double v : { 0.5, 1.5, 2.5, 4.0, 15.0 }) scale.ticks.push_back ({ v, {}, false, false });
    vu.setScale (&scale);

    bypassed = bypassSwitch.getToggleState();
    autoOn = autoSwitch.getToggleState();
    applyModeLabels();
}

double W9bFaceplate::reductionPosition (double db) noexcept
{
    constexpr double kRange = 20.0, kSoft = 1.5;
    return std::log1p (std::clamp (db, 0.0, kRange) / kSoft) / std::log1p (kRange / kSoft);
}

void W9bFaceplate::applyModeLabels()
{
    knobs[cThresh].setName (autoOn ? "Thresh  auto" : "Thresh");
    knobs[cThresh].setEnabled (! autoOn);
    knobs[cFocus].setDimmed (! autoOn);
}

//==============================================================================
void W9bFaceplate::resized()
{
    const bool wide = isWide();
    const float dw = wide ? (float) kWideWidth : (float) kCompactWidth, dh = wide ? (float) kWideHeight : (float) kCompactHeight;
    pixelScale = std::min ((float) getWidth() / dw, (float) getHeight() / dh);
    const float ox = ((float) getWidth() - dw * pixelScale) * 0.5f, oy = ((float) getHeight() - dh * pixelScale) * 0.5f;
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<float> (ox + x * pixelScale, oy + y * pixelScale, w * pixelScale, h * pixelScale); };

    plate = getLocalBounds().toFloat();
    // Grille 4 × 2 : Input Thresh Ratio Focus / Attack Release Output [AUTO].
    const float cw = wide ? 132.0f : 58.0f, ch = wide ? 166.0f : 64.0f, gx = wide ? 26.0f : 4.0f, gy = wide ? 20.0f : 94.0f;
    for (int i = 0; i < numCells; ++i)
        knobs[(std::size_t) i].setBounds (R (gx + (float) (i % 4) * cw, gy + (float) (i / 4) * ch, cw, ch).toNearestInt());

    const auto autoCell = R (gx + 3.0f * cw, gy + ch, cw, ch);
    autoLabelArea = autoCell.withHeight (std::max (9.0f, ch * pixelScale * 0.105f));
    autoSwitch.setBounds (autoCell.withTrimmedTop (autoLabelArea.getHeight() + (wide ? 38.0f : 4.0f) * pixelScale).withHeight ((wide ? 62.0f : 30.0f) * pixelScale).toNearestInt());
    autoSwitch.setLabels (wide ? "MANUAL" : "MAN", "AUTO");

    if (wide)
    {
        vu.setGeometry (R (580.0f, 30.0f, 390.0f, 190.0f), pixelScale);
        logoArea = R (580.0f, 232.0f, 390.0f, 64.0f);
        lampArea = R (610.0f, 306.0f, 58.0f, 58.0f);
        bypassSwitch.setBounds (R (770.0f, 308.0f, 190.0f, 56.0f).toNearestInt());
    }
    else
    {
        vu.setGeometry (R (8.0f, 6.0f, 224.0f, 84.0f), pixelScale);
        logoArea = R (86.0f, 228.0f, 76.0f, 30.0f);
        lampArea = R (24.0f, 232.0f, 22.0f, 22.0f);
        bypassSwitch.setBounds (R (166.0f, 228.0f, 66.0f, 30.0f).toNearestInt());
    }
    vu.snap (0.0);
    logoFont = findLogoFont (std::max (12.0f, logoArea.getHeight() * (wide ? 0.95f : 0.9f)));

    const auto c = vu.getFrame().getCentre();
    brushed = makeBrushedPlate (getWidth(), getHeight(), blueAnodisedTheme(), c, vu.getFrame().getWidth() * 0.95f, std::max (6.0f, 11.0f * pixelScale));
}

//==============================================================================
void W9bFaceplate::tick (float dt, const FaceplateFeed& feed)
{
    const bool nowBypassed = bypassSwitch.getToggleState(), nowAuto = autoSwitch.getToggleState();
    const bool modeChanged = nowBypassed != bypassed || nowAuto != autoOn;
    bypassed = nowBypassed;
    if (nowAuto != autoOn) { autoOn = nowAuto; applyModeLabels(); }

    // Seuil effectif (Auto) affiché sur le potard THRESH ; repère de cible sur le VU.
    if (autoOn && ! bypassed)
    {
        knobs[cThresh].setLive (spec (W::threshold).toNormalised (feed.aux), juce::String (spec (W::threshold).format (feed.aux)));
        vu.setMarker (spec (W::focus).toPlain (knobs[cFocus].getValue()));
    }
    else
    {
        knobs[cThresh].setLive (std::nullopt, {});
        vu.setMarker (std::nullopt);
    }

    const bool needleMoved = vu.advance (dt, bypassed ? 0.0 : feed.reductionDb);

    // Voyant : de -6 dBFS (rouge sombre) à 0 dBFS (plein), attaque instantanée, retombée douce.
    const float peakDb = feed.statusPeak > 1.0e-6f ? 20.0f * std::log10 (feed.statusPeak) : -120.0f;
    const float goal = bypassed ? 0.0f : juce::jlimit (0.0f, 1.0f, (peakDb + 6.0f) / 6.0f);
    const float before = lamp;
    lamp = goal > lamp ? goal : lamp + (goal - lamp) * (1.0f - std::exp (-dt * 5.0f));
    if (lamp < 0.002f) lamp = 0.0f;

    if (modeChanged) { repaint(); return; }
    if (needleMoved || autoOn) repaint (vu.getWindow());
    if (std::abs (lamp - before) > 0.004f) repaint (lampArea.expanded (lampArea.getWidth() * 0.9f).getSmallestIntegerContainer());
}

//==============================================================================
void W9bFaceplate::paint (juce::Graphics& g)
{
    drawPlate (g, plate, blueAnodisedTheme(), brushed, pixelScale);
    drawScrews (g, plate, pixelScale);

    g.setFont (fitFont (logoFont, "W9BMK1", logoArea.getWidth() * 0.95f));
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawText ("W9BMK1", logoArea.toNearestInt().translated (1, 2), juce::Justification::centred);
    g.setColour (juce::Colour (0xffa9bdf4));
    g.drawText ("W9BMK1", logoArea.toNearestInt(), juce::Justification::centred);

    g.setColour (blueAnodisedTheme().engrave.withAlpha (autoOn ? 0.95f : 0.8f));
    g.setFont (fitFont (NextLookAndFeel::font (autoLabelArea.getHeight(), true), "AUTO FOCUS", autoLabelArea.getWidth() * 0.96f));
    g.drawText ("AUTO FOCUS", autoLabelArea.toNearestInt(), juce::Justification::centred);

    vu.paintFace (g);

    // Étiquette du voyant.
    g.setColour (blueAnodisedTheme().engrave.withAlpha (0.85f));
    g.setFont (NextLookAndFeel::font (std::max (8.0f, lampArea.getHeight() * 0.2f), true));
    g.drawText ("PEAK", juce::Rectangle<float> (lampArea.getRight() + lampArea.getHeight() * 0.15f, lampArea.getCentreY() - lampArea.getHeight() * 0.3f,
                                                lampArea.getHeight() * 1.2f, lampArea.getHeight() * 0.6f).toNearestInt(), juce::Justification::centredLeft);
}

void W9bFaceplate::paintOverChildren (juce::Graphics& g)
{
    vu.paintGlass (g);
    drawRedLamp (g, lampArea, lamp);

    if (bypassed)   // module en bypass : la façade s'assombrit, sauf l'interrupteur qui reste lisible
    {
        juce::Graphics::ScopedSaveState s (g);
        g.excludeClipRegion (bypassSwitch.getBounds());
        g.setColour (juce::Colour (0xff02050f).withAlpha (0.42f));
        g.fillRect (plate);
    }
}

} // namespace aura::ui
