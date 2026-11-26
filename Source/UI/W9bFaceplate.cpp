// AuraRack — NextDSP · façade du W9BMK1

#include "W9bFaceplate.h"
#include "../DSP/AuraModuleDSP.h"

namespace aura::ui
{
namespace
{
const juce::Colour kAmber { 0xffffc531 };
const juce::Colour kAmberDim { 0xff3a3018 };
const juce::Colour kEngrave { 0xffd2e1ff };
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Point<float> polar (juce::Point<float> c, float r, float angle) { return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) }; }

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
}

//==============================================================================
W9bKnob::W9bKnob()
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kPi * 1.25f, kPi * 2.75f, true);
    setMouseDragSensitivity (200);
}

void W9bKnob::bind (juce::RangedAudioParameter& p)
{
    attachment = std::make_unique<juce::SliderParameterAttachment> (p, *this, nullptr);
}

void W9bKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float labelH = std::max (9.0f, b.getHeight() * 0.105f);
    auto top = b.withHeight (labelH);
    auto bottom = b.removeFromBottom (labelH * 1.1f);
    const auto knobArea = b.withTrimmedTop (labelH + 1.0f).withTrimmedBottom (1.0f);
    const float d = std::min (knobArea.getWidth(), knobArea.getHeight());
    const auto c = knobArea.getCentre();
    const float R = d * 0.5f;

    // Nom (gravé) et valeur (affichage ambre).
    g.setColour (kEngrave.withAlpha (0.9f));
    g.setFont (NextLookAndFeel::font (labelH, true));
    g.drawText (name, top.toNearestInt(), juce::Justification::centred);
    g.setColour (kAmber);
    g.setFont (NextLookAndFeel::font (labelH * 0.95f, true));
    g.drawText (getTextFromValue (getValue()), bottom.toNearestInt(), juce::Justification::centred);

    // Collerette noire anodisée.
    {
        juce::ColourGradient grad (juce::Colour (0xff23262e), c.x - R * 0.5f, c.y - R * 0.7f, juce::Colour (0xff020203), c.x + R * 0.6f, c.y + R * 0.8f, false);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - R, c.y - R, d, d);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (c.x - R, c.y - R, d, d, 1.0f);
    }

    // Anneau de LED : 36 segments sur 270°, allumés jusqu'à la valeur.
    const float start = kPi * 1.25f, sweep = kPi * 1.5f;
    const float prop = (float) valueToProportionOfLength (getValue());
    const int segments = 36;
    const float ringR = R * 0.86f, thick = std::max (1.6f, d * 0.055f);
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = start + sweep * (float) i / (float) segments;
        const float a1 = start + sweep * ((float) i + 0.72f) / (float) segments;
        const bool lit = ((float) i + 0.5f) / (float) segments <= prop + 1.0e-4f;
        juce::Path seg;
        seg.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a0, a1, true);
        if (lit)
        {
            g.setColour (kAmber.withAlpha (0.22f));
            g.strokePath (seg, juce::PathStrokeType (thick * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        g.setColour (lit ? kAmber : kAmberDim);
        g.strokePath (seg, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // Capuchon en aluminium usiné brossé : secteurs radiaux dont la clarté suit deux lobes (reflet de brossage).
    const float capR = R * 0.60f;
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        g.reduceClipRegion (clip);
        const int wedges = 120;
        for (int i = 0; i < wedges; ++i)
        {
            const float a0 = 2.0f * kPi * (float) i / (float) wedges, a1 = 2.0f * kPi * ((float) i + 1.15f) / (float) wedges;
            const float light = 0.50f + 0.34f * std::abs (std::cos (a0 - 0.6f)) + 0.10f * std::cos (2.0f * (a0 + 1.2f));
            g.setColour (juce::Colour::fromFloatRGBA (light * 0.93f, light * 0.95f, light, 1.0f));
            juce::Path w;
            w.startNewSubPath (c);
            w.lineTo (polar (c, capR * 1.1f, a0));
            w.lineTo (polar (c, capR * 1.1f, a1));
            w.closeSubPath();
            g.fillPath (w);
        }
        // Bombé : lumière douce en haut à gauche, ombre en bas à droite.
        juce::ColourGradient shade (juce::Colours::white.withAlpha (0.22f), c.x - capR * 0.5f, c.y - capR * 0.55f,
                                    juce::Colours::black.withAlpha (0.30f), c.x + capR * 0.7f, c.y + capR * 0.8f, false);
        g.setGradientFill (shade);
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, std::max (1.0f, d * 0.015f));
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawEllipse (c.x - capR + 1.0f, c.y - capR + 1.0f, capR * 2.0f - 2.0f, capR * 2.0f - 2.0f, 0.8f);

    // Repère de position, net et lisible.
    const float angle = start + sweep * prop;
    g.setColour (juce::Colour (0xff0b0c10));
    g.drawLine ({ polar (c, capR * 0.30f, angle), polar (c, capR * 0.92f, angle) }, std::max (2.0f, d * 0.04f));
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.drawLine ({ polar (c, capR * 0.42f, angle), polar (c, capR * 0.88f, angle) }, std::max (1.2f, d * 0.02f));
}

//==============================================================================
void W9bSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto b = getLocalBounds().toFloat();
    const bool bypass = getToggleState();
    const float labelH = std::max (8.0f, b.getHeight() * 0.36f);
    auto labels = b.removeFromTop (labelH);
    const auto track = b.reduced (b.getWidth() * 0.12f, b.getHeight() * 0.10f);

    g.setFont (NextLookAndFeel::font (labelH * 0.95f, true));
    g.setColour (bypass ? kEngrave.withAlpha (0.45f) : kAmber);
    g.drawText ("ACTIVE", labels.removeFromLeft (labels.getWidth() * 0.5f).toNearestInt(), juce::Justification::centred);
    g.setColour (bypass ? juce::Colour (0xffff7a5c) : kEngrave.withAlpha (0.45f));
    g.drawText ("BYPASS", labels.toNearestInt(), juce::Justification::centred);

    // Glissière encastrée.
    g.setColour (juce::Colour (0xff04122e));
    g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.35f : 0.18f));
    g.drawRoundedRectangle (track, track.getHeight() * 0.5f, 1.0f);

    // Levier métallique.
    const float kw = track.getWidth() * 0.46f;
    const auto lever = juce::Rectangle<float> (bypass ? track.getRight() - kw - 1.5f : track.getX() + 1.5f, track.getY() + 1.5f, kw, track.getHeight() - 3.0f);
    juce::ColourGradient grad (juce::Colour (0xfff1f3f7), lever.getX(), lever.getY(), juce::Colour (0xff7d8594), lever.getX(), lever.getBottom(), false);
    grad.addColour (0.5, juce::Colour (0xffc9ceda));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (lever, lever.getHeight() * 0.45f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (lever, lever.getHeight() * 0.45f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (int i = -1; i <= 1; ++i)
        g.drawLine (lever.getCentreX() + (float) i * lever.getWidth() * 0.14f, lever.getY() + lever.getHeight() * 0.25f,
                    lever.getCentreX() + (float) i * lever.getWidth() * 0.14f, lever.getBottom() - lever.getHeight() * 0.25f, 1.0f);
}

//==============================================================================
W9bFaceplate::W9bFaceplate (juce::AudioProcessorValueTreeState& apvts, int slot)
{
    static const char* names[] = { "Input", "Thresh", "Ratio", "Attack", "Release", "Output" };
    // Ordre d'affichage = ordre des paramètres du module : Input, Threshold, Ratio / Attack, Release, Output.
    for (int i = 0; i < 6; ++i)
    {
        knobs[(std::size_t) i].bind (*apvts.getParameter (aura::params::generic (slot, i)));
        knobs[(std::size_t) i].setName (names[i]);
        addAndMakeVisible (knobs[(std::size_t) i]);
    }
    bypassSwitch.setClickingTogglesState (true);
    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::bypass (slot)), bypassSwitch, nullptr);
    addAndMakeVisible (bypassSwitch);
    bypassed = bypassSwitch.getToggleState();
}

//==============================================================================
void W9bFaceplate::resized()
{
    const bool wide = isWide();
    const float dw = wide ? (float) kWideWidth : (float) kCompactWidth, dh = wide ? (float) kWideHeight : (float) kCompactHeight;
    scale = std::min ((float) getWidth() / dw, (float) getHeight() / dh);
    const float ox = ((float) getWidth() - dw * scale) * 0.5f, oy = ((float) getHeight() - dh * scale) * 0.5f;
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<float> (ox + x * scale, oy + y * scale, w * scale, h * scale); };

    plate = getLocalBounds().toFloat();
    if (wide)
    {
        for (int i = 0; i < 6; ++i)
            knobs[(std::size_t) i].setBounds (R (40.0f + (float) (i % 3) * 150.0f, 24.0f + (float) (i / 3) * 170.0f, 150.0f, 160.0f).toNearestInt());
        vuFrame = R (540.0f, 30.0f, 420.0f, 190.0f);
        logoArea = R (540.0f, 236.0f, 420.0f, 64.0f);
        lampArea = R (590.0f, 304.0f, 62.0f, 62.0f);
        bypassSwitch.setBounds (R (770.0f, 306.0f, 190.0f, 58.0f).toNearestInt());
    }
    else
    {
        for (int i = 0; i < 6; ++i)
            knobs[(std::size_t) i].setBounds (R (8.0f + (float) (i % 3) * 75.0f, 100.0f + (float) (i / 3) * 70.0f, 75.0f, 68.0f).toNearestInt());
        vuFrame = R (8.0f, 6.0f, 224.0f, 90.0f);
        logoArea = R (84.0f, 236.0f, 82.0f, 24.0f);
        lampArea = R (30.0f, 240.0f, 20.0f, 20.0f);
        bypassSwitch.setBounds (R (168.0f, 236.0f, 62.0f, 26.0f).toNearestInt());
    }

    vuWindow = vuFrame.reduced (std::max (4.0f, 9.0f * scale));
    arcRadius = vuWindow.getWidth() * 0.74f;
    pivot = { vuWindow.getCentreX(), vuWindow.getY() + vuWindow.getHeight() * 0.26f + arcRadius };
    needleLength = arcRadius * 0.985f;
    logoFont = findLogoFont (std::max (12.0f, logoArea.getHeight() * (wide ? 0.95f : 1.0f)));

    // Texture brossée de la plaque (bruit déterministe : identique d'un redimensionnement à l'autre).
    if (getWidth() > 0 && getHeight() > 0)
    {
        brushed = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics g (brushed);
        std::uint32_t seed = 0x9e3779b9u;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
        for (int y = 0; y < getHeight(); ++y)
            for (int k = 0; k < 3; ++k)
            {
                const float x0 = rnd() * (float) getWidth(), len = (0.25f + rnd() * 0.75f) * (float) getWidth();
                g.setColour ((rnd() > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (0.02f + rnd() * 0.035f));
                g.fillRect (x0, (float) y, len, 1.0f);
            }
        // Trame de points discrète autour du VU-mètre, qui s'estompe avec la distance.
        const auto c = vuFrame.getCentre();
        const float pitch = std::max (6.0f, 11.0f * scale), reach = vuFrame.getWidth() * 0.95f;
        for (float y = 0.0f, row = 0.0f; y < (float) getHeight(); y += pitch * 0.5f, row += 1.0f)
            for (float x = (std::fmod (row, 2.0f) < 1.0f ? 0.0f : pitch * 0.5f); x < (float) getWidth(); x += pitch)
            {
                const float dist = std::hypot ((x - c.x) * 0.7f, y - c.y);
                const float f = 1.0f - dist / reach;
                if (f <= 0.0f) continue;
                const float r = pitch * 0.30f * f;
                g.setColour (juce::Colour (0xff02134a).withAlpha (0.30f * f));
                g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
            }
    }
}

//==============================================================================
double W9bFaceplate::needlePosition (double db) noexcept
{
    // Échelle de lecture dilatée près de 0 (où l'on regarde) : 0, 1, 2, 3, 5, 7, 10, 20 dB bien espacés.
    constexpr double kRange = 20.0, kSoft = 1.5;
    return std::log1p (std::clamp (db, 0.0, kRange) / kSoft) / std::log1p (kRange / kSoft);
}

void W9bFaceplate::tick (float dt, float reductionDb, float peakLinear)
{
    // Aiguille : masse-ressort (ω0 = 2π·4 Hz, ζ = 0,9), intégrée par pas de 2 ms (stable quelle que soit la cadence).
    needleTarget = needlePosition (reductionDb);
    const double omega = 2.0 * juce::MathConstants<double>::pi * 4.0, zeta = 0.9;
    double remaining = std::min (0.1, (double) dt);
    const double before = needle;
    while (remaining > 0.0)
    {
        const double h = std::min (0.002, remaining);
        needleVelocity += h * (omega * omega * (needleTarget - needle) - 2.0 * zeta * omega * needleVelocity);
        needle += h * needleVelocity;
        remaining -= h;
    }
    needle = std::clamp (needle, 0.0, 1.0);
    const auto travelPixels = std::abs (needle - before) * (double) arcRadius * 1.33;
    const bool needleMoved = travelPixels > 0.05;

    // Voyant : s'allume de -6 dBFS (rouge sombre) à 0 dBFS (plein), attaque instantanée, retombée douce.
    const float peakDb = peakLinear > 1.0e-6f ? 20.0f * std::log10 (peakLinear) : -120.0f;
    const float target = bypassed ? 0.0f : juce::jlimit (0.0f, 1.0f, (peakDb + 6.0f) / 6.0f);
    const float before2 = lamp;
    lamp = target > lamp ? target : lamp + (target - lamp) * (1.0f - std::exp (-dt * 5.0f));
    if (lamp < 0.002f) lamp = 0.0f;

    const bool nowBypassed = bypassSwitch.getToggleState();
    if (nowBypassed != bypassed) { bypassed = nowBypassed; repaint(); return; }

    if (needleMoved) repaint (vuWindow.getSmallestIntegerContainer());
    if (std::abs (lamp - before2) > 0.004f) repaint (lampArea.expanded (lampArea.getWidth() * 0.9f).getSmallestIntegerContainer());
}

//==============================================================================
void W9bFaceplate::drawPlate (juce::Graphics& g) const
{
    auto r = plate;
    juce::ColourGradient base (juce::Colour (0xff0f4bc2), 0.0f, r.getY(), juce::Colour (0xff062a80), 0.0f, r.getBottom(), false);
    base.addColour (0.5, juce::Colour (0xff0a3a9c));
    g.setGradientFill (base);
    g.fillRect (r);
    g.drawImageAt (brushed, 0, 0);

    // Biseau : arête claire en haut / à gauche, sombre en bas / à droite, voile lumineux sur les bords.
    const float bevel = std::max (1.5f, 3.0f * scale);
    juce::ColourGradient edge (juce::Colours::white.withAlpha (0.14f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getX() + bevel * 6.0f, r.getY() + bevel * 6.0f, false);
    g.setGradientFill (edge);
    g.fillRect (r.removeFromTop (bevel * 6.0f));
    g.setColour (juce::Colours::white.withAlpha (0.38f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), r.getWidth(), bevel * 0.6f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), bevel * 0.6f, r.getHeight()));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom() - bevel * 0.8f, r.getWidth(), bevel * 0.8f));
    g.fillRect (juce::Rectangle<float> (r.getRight() - bevel * 0.8f, r.getY(), bevel * 0.8f, r.getHeight()));
}

void W9bFaceplate::drawScrews (juce::Graphics& g) const
{
    const float d = std::max (9.0f, 15.0f * scale), m = d * 1.15f;
    const juce::Point<float> pts[] = { { plate.getX() + m, plate.getY() + m }, { plate.getRight() - m, plate.getY() + m },
                                       { plate.getX() + m, plate.getBottom() - m }, { plate.getRight() - m, plate.getBottom() - m } };
    int k = 0;
    for (auto p : pts)
    {
        juce::ColourGradient grad (juce::Colour (0xfff4f6fa), p.x - d * 0.3f, p.y - d * 0.35f, juce::Colour (0xff6e7684), p.x + d * 0.4f, p.y + d * 0.45f, true);
        g.setGradientFill (grad);
        g.fillEllipse (p.x - d * 0.5f, p.y - d * 0.5f, d, d);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (p.x - d * 0.5f, p.y - d * 0.5f, d, d, 1.0f);
        const float a = 0.5f + 0.9f * (float) (k++);   // fentes dans des orientations variées, comme des vis réelles
        g.setColour (juce::Colour (0xff2a2e38));
        g.drawLine ({ polar (p, d * 0.36f, a), polar (p, d * 0.36f, a + kPi) }, std::max (1.0f, d * 0.12f));
    }
}

void W9bFaceplate::drawVu (juce::Graphics& g) const
{
    // Cadre noir, fond ambre rétro-éclairé.
    g.setColour (juce::Colour (0xff050608));
    g.fillRoundedRectangle (vuFrame, 4.0f * scale);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (vuFrame.reduced (0.5f), 4.0f * scale, 1.0f);

    const auto w = vuWindow;
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (w, 2.5f * scale);
        g.reduceClipRegion (clip);

        juce::ColourGradient face (juce::Colour (0xfffbe3a6), w.getCentreX(), w.getBottom() - w.getHeight() * 0.1f, juce::Colour (0xff9a6c25), w.getX(), w.getY(), true);
        face.addColour (0.55, juce::Colour (0xffe8bd6e));
        g.setGradientFill (face);
        g.fillRect (w);

        // Ombre intérieure du haut de la fenêtre.
        juce::ColourGradient inner (juce::Colours::black.withAlpha (0.38f), 0.0f, w.getY(), juce::Colours::transparentBlack, 0.0f, w.getY() + w.getHeight() * 0.28f, false);
        g.setGradientFill (inner);
        g.fillRect (w);

        // Graduation : réduction de gain en dB, arc centré sur le pivot (hors fenêtre, en bas).
        const float span = 31.0f * kPi / 180.0f;
        auto angleFor = [&] (double db) { return span - 2.0f * span * (float) needlePosition (db); };
        const juce::Colour ink (0xff2a1a08), warm (0xffb3262b);
        g.setColour (ink.withAlpha (0.9f));
        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, arcRadius, arcRadius, 0.0f, -span, span, true);
        g.strokePath (arc, juce::PathStrokeType (std::max (1.0f, 1.3f * scale)));

        const float majorLen = arcRadius * 0.075f, minorLen = arcRadius * 0.04f;
        for (double db : { 0.5, 1.5, 2.5, 4.0, 15.0 })
        {
            const float a = angleFor (db);
            g.drawLine ({ polar (pivot, arcRadius, a), polar (pivot, arcRadius - minorLen, a) }, std::max (0.8f, 1.0f * scale));
        }
        g.setFont (NextLookAndFeel::font (std::max (8.0f, arcRadius * 0.085f), true));
        for (double db : { 0.0, 1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 20.0 })
        {
            const float a = angleFor (db);
            g.setColour (db >= 5.0 ? ink : (db > 0.0 ? ink : warm));
            g.drawLine ({ polar (pivot, arcRadius, a), polar (pivot, arcRadius - majorLen, a) }, std::max (1.2f, 1.8f * scale));
            const auto tp = polar (pivot, arcRadius + majorLen * 1.5f, a);
            const float tw = arcRadius * 0.16f;
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (tp.x - tw * 0.5f, tp.y - tw * 0.32f, tw, tw * 0.64f).toNearestInt(), juce::Justification::centred);
        }

        g.setColour (ink.withAlpha (0.75f));
        g.setFont (NextLookAndFeel::font (std::max (7.0f, arcRadius * 0.062f), true));
        g.drawText ("GAIN REDUCTION  dB", w.withTrimmedTop (w.getHeight() * 0.66f).toNearestInt(), juce::Justification::centred);
    }
}

void W9bFaceplate::drawLamp (juce::Graphics& g) const
{
    const auto c = lampArea.getCentre();
    const float d = std::min (lampArea.getWidth(), lampArea.getHeight()), R = d * 0.5f;

    if (lamp > 0.05f)   // halo
    {
        juce::ColourGradient halo (juce::Colour (0xffff3020).withAlpha (0.55f * lamp), c.x, c.y, juce::Colours::transparentBlack, c.x + R * 2.0f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (c.x - R * 2.0f, c.y - R * 2.0f, R * 4.0f, R * 4.0f);
    }
    // Collerette sombre.
    juce::ColourGradient ring (juce::Colour (0xff3a3d46), c.x - R * 0.6f, c.y - R * 0.8f, juce::Colour (0xff050506), c.x + R * 0.6f, c.y + R * 0.9f, false);
    g.setGradientFill (ring);
    g.fillEllipse (c.x - R, c.y - R, d, d);
    // Verre rouge : éteint = bordeaux sombre, allumé = rouge vif.
    const float lr = R * 0.78f;
    const auto off = juce::Colour (0xff3d0909), on = juce::Colour (0xffff2d1c);
    juce::ColourGradient lens (off.interpolatedWith (juce::Colour (0xffff7a5c), lamp), c.x - lr * 0.3f, c.y - lr * 0.4f, off.interpolatedWith (on, lamp).darker (0.6f - 0.4f * lamp), c.x + lr, c.y + lr, true);
    g.setGradientFill (lens);
    g.fillEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f);
    // Facettes du verre et reflet.
    g.setColour (juce::Colours::black.withAlpha (0.20f));
    for (int i = 0; i < 6; ++i)
        g.drawLine ({ polar (c, lr * 0.25f, (float) i * kPi / 3.0f), polar (c, lr, (float) i * kPi / 3.0f) }, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.22f + 0.25f * lamp));
    g.fillEllipse (c.x - lr * 0.62f, c.y - lr * 0.78f, lr * 0.7f, lr * 0.32f);

    g.setColour (kEngrave.withAlpha (0.85f));
    g.setFont (NextLookAndFeel::font (std::max (8.0f, d * 0.2f), true));
    const auto textArea = isWide() ? juce::Rectangle<float> (c.x + R * 1.2f, c.y - R * 0.3f, R * 3.0f, R * 0.6f)
                                   : juce::Rectangle<float> (c.x + R * 1.1f, c.y - R * 0.5f, R * 2.4f, R);
    g.drawText ("PEAK", textArea.toNearestInt(), juce::Justification::centredLeft);
}

void W9bFaceplate::drawLogo (juce::Graphics& g) const
{
    const auto r = logoArea.toNearestInt();
    g.setFont (logoFont);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawText ("W9BMK1", r.translated (1, 2), juce::Justification::centred);
    g.setColour (juce::Colour (0xffa9bdf4));
    g.drawText ("W9BMK1", r, juce::Justification::centred);
}

void W9bFaceplate::paint (juce::Graphics& g)
{
    drawPlate (g);
    drawScrews (g);
    drawLogo (g);
}

void W9bFaceplate::paintOverChildren (juce::Graphics& g)
{
    // Éléments dynamiques : fenêtre du VU (cadran, aiguille, vitre) et voyant.
    drawVu (g);

    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (vuWindow, 2.5f * scale);
        g.reduceClipRegion (clip);
        const float span = 31.0f * kPi / 180.0f;
        const float a = span - 2.0f * span * (float) needle;
        const auto tip = polar (pivot, needleLength, a);
        const float lw = std::max (1.2f, 1.7f * scale);
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.drawLine ({ polar (pivot, arcRadius * 0.55f, a).translated (2.0f * scale, 2.5f * scale), tip.translated (2.0f * scale, 2.5f * scale) }, lw);
        g.setColour (juce::Colour (0xff1c0d06));
        g.drawLine ({ polar (pivot, arcRadius * 0.55f, a), tip }, lw);

        // Vitre : reflets doux, sans cacher le cadran.
        juce::ColourGradient glass (juce::Colours::white.withAlpha (0.20f), 0.0f, vuWindow.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, vuWindow.getY() + vuWindow.getHeight() * 0.5f, false);
        g.setGradientFill (glass);
        g.fillRect (vuWindow);
        juce::Path sheen;
        sheen.startNewSubPath (vuWindow.getX() + vuWindow.getWidth() * 0.08f, vuWindow.getBottom());
        sheen.lineTo (vuWindow.getX() + vuWindow.getWidth() * 0.30f, vuWindow.getY());
        sheen.lineTo (vuWindow.getX() + vuWindow.getWidth() * 0.40f, vuWindow.getY());
        sheen.lineTo (vuWindow.getX() + vuWindow.getWidth() * 0.18f, vuWindow.getBottom());
        sheen.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillPath (sheen);
    }
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.drawRoundedRectangle (vuWindow, 2.5f * scale, std::max (1.0f, 1.5f * scale));

    drawLamp (g);

    // Module en bypass : la façade s'assombrit, sauf l'interrupteur qui reste lisible.
    if (bypassed)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.excludeClipRegion (bypassSwitch.getBounds());
        g.setColour (juce::Colour (0xff02050f).withAlpha (0.42f));
        g.fillRect (plate);
    }
}

} // namespace aura::ui
