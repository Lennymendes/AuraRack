// AuraRack — NextDSP · façade du E9DS

#include "E9dsFaceplate.h"

namespace aura::ui
{
namespace
{
using E = aura::AuraE9DS;
const PlateTheme& theme() { return graphiteRoseTheme(); }
constexpr double kFMin = 1000.0, kFMax = 20000.0, kDbMax = 60.0;

float xForFreq (juce::Rectangle<float> r, double f) { return r.getX() + r.getWidth() * (float) (std::log (f / kFMin) / std::log (kFMax / kFMin)); }

/** |1 − (1 − g)·H(f)| : réponse de la cloche dynamique (H = passe-bande 2e ordre, gain unité et phase nulle à f0). */
double splitResponseDb (double f, double f0, double q, double g)
{
    const double x = f / f0;
    const std::complex<double> h = std::complex<double> (0.0, x / q) / std::complex<double> (1.0 - x * x, x / q);
    return 20.0 * std::log10 (std::max (1.0e-6, std::abs (1.0 - (1.0 - g) * h)));
}
}

E9dsFaceplate::E9dsFaceplate (juce::AudioProcessorValueTreeState& a, int s)
    : apvts (a), slot (s),
      knobs { PlateKnob (theme()), PlateKnob (theme()), PlateKnob (theme()), PlateKnob (theme()), PlateKnob (theme()) },
      modeSwitch (theme(), "SPLIT", "WIDE", false),
      listenSwitch (theme(), "OFF", "LISTEN", false),
      bypassSwitch (theme(), "ACTIVE", "BYPASS", true)
{
    const char* names[] = { "Threshold", "Frequency", "Width", "Range", "Release" };
    for (int i = 0; i < 5; ++i)
    {
        knobs[(std::size_t) i].bind (*apvts.getParameter (aura::params::generic (slot, i)));
        knobs[(std::size_t) i].setName (names[i]);
        addAndMakeVisible (knobs[(std::size_t) i]);
    }
    modeAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::generic (slot, E::mode)), modeSwitch, nullptr);
    listenAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::generic (slot, E::listen)), listenSwitch, nullptr);
    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::bypass (slot)), bypassSwitch, nullptr);
    for (auto* sw : { &modeSwitch, &listenSwitch, &bypassSwitch }) addAndMakeVisible (*sw);
    bypassed = bypassSwitch.getToggleState();
}

double E9dsFaceplate::plain (int param) const
{
    const auto* raw = apvts.getRawParameterValue (aura::params::generic (slot, param));
    return E::info().params[(std::size_t) param].toPlain (raw != nullptr ? raw->load() : 0.0f);
}

void E9dsFaceplate::resized()
{
    const bool wide = isWide();
    const float dw = wide ? (float) kWideWidth : (float) kCompactWidth, dh = wide ? (float) kWideHeight : (float) kCompactHeight;
    pixelScale = std::min ((float) getWidth() / dw, (float) getHeight() / dh);
    const float ox = ((float) getWidth() - dw * pixelScale) * 0.5f, oy = ((float) getHeight() - dh * pixelScale) * 0.5f;
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<float> (ox + x * pixelScale, oy + y * pixelScale, w * pixelScale, h * pixelScale); };

    plate = getLocalBounds().toFloat();
    if (wide)
    {
        display = R (36.0f, 24.0f, 928.0f, 150.0f);
        for (int i = 0; i < 5; ++i) knobs[(std::size_t) i].setBounds (R (36.0f + (float) i * 146.0f, 184.0f, 146.0f, 172.0f).toNearestInt());
        modeSwitch.setBounds (R (790.0f, 190.0f, 174.0f, 52.0f).toNearestInt());
        listenSwitch.setBounds (R (790.0f, 252.0f, 174.0f, 52.0f).toNearestInt());
        bypassSwitch.setBounds (R (790.0f, 314.0f, 174.0f, 52.0f).toNearestInt());
    }
    else
    {
        display = R (8.0f, 6.0f, 224.0f, 62.0f);
        for (int i = 0; i < 3; ++i) knobs[(std::size_t) i].setBounds (R (6.0f + (float) i * 78.0f, 70.0f, 76.0f, 84.0f).toNearestInt());
        knobs[3].setBounds (R (6.0f, 156.0f, 76.0f, 84.0f).toNearestInt());
        knobs[4].setBounds (R (84.0f, 156.0f, 76.0f, 84.0f).toNearestInt());
        modeSwitch.setBounds (R (164.0f, 158.0f, 70.0f, 30.0f).toNearestInt());
        listenSwitch.setBounds (R (164.0f, 192.0f, 70.0f, 30.0f).toNearestInt());
        bypassSwitch.setBounds (R (164.0f, 226.0f, 70.0f, 30.0f).toNearestInt());
    }
    brushed = makeBrushedPlate (getWidth(), getHeight(), theme(), display.getCentre(), display.getWidth() * 0.5f, std::max (6.0f, 11.0f * pixelScale));
}

void E9dsFaceplate::tick (float dt, const FaceplateFeed& feed)
{
    const bool nowBypassed = bypassSwitch.getToggleState();
    bool dirty = nowBypassed != bypassed;
    bypassed = nowBypassed;

    const float target = bypassed ? 0.0f : feed.reductionDb;
    const float k = 1.0f - std::exp (-dt * 30.0f);
    const float prev = shownReduction;
    shownReduction += (target - shownReduction) * k;
    if (shownReduction < 0.005f) shownReduction = 0.0f;
    dirty = dirty || std::abs (shownReduction - prev) > 0.002f;

    const float band = bypassed ? -120.0f : feed.aux;
    if (std::abs (band - bandDb) > 0.1f) { bandDb = band; dirty = true; }

    const double t = plain (E::threshold), f = plain (E::frequency), w = plain (E::width), r = plain (E::range);
    if (t != lastThreshold || f != lastFreq || w != lastWidth || r != lastRange) { lastThreshold = t; lastFreq = f; lastWidth = w; lastRange = r; dirty = true; }

    if (dirty) repaint (display.expanded (2.0f).getSmallestIntegerContainer());
}

void E9dsFaceplate::paintDisplay (juce::Graphics& g) const
{
    const auto accent = theme().led;
    auto r = display;
    g.setColour (juce::Colour (0xff0a0b0e));
    g.fillRoundedRectangle (r, 6.0f * pixelScale);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r, 6.0f * pixelScale, 1.0f);

    auto inner = r.reduced (10.0f * pixelScale, 8.0f * pixelScale);
    const bool wide = isWide();
    auto meter = inner.removeFromBottom (wide ? 16.0f * pixelScale : 8.0f * pixelScale);
    inner.removeFromBottom (4.0f * pixelScale);
    const float fontPx = std::max (7.0f, 10.0f * pixelScale);

    // Grille : fréquences et dB.
    g.setFont (NextLookAndFeel::font (fontPx, true));
    for (double f : { 2000.0, 4000.0, 8000.0, 16000.0 })
    {
        const float x = xForFreq (inner, f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
        if (wide)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (juce::String ((int) (f / 1000.0)) + "k", (int) x - 18, (int) inner.getBottom() - 14, 36, 12, juce::Justification::centred);
        }
    }
    const double range = std::max (1.0, lastRange);
    const double dbTop = 2.0, dbBottom = -std::max (12.0, range + 4.0);
    auto yForDb = [&] (double db) { return inner.getY() + inner.getHeight() * (float) ((dbTop - db) / (dbTop - dbBottom)); };
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine ((int) yForDb (0.0), inner.getX(), inner.getRight());

    const double f0 = lastFreq > 0.0 ? lastFreq : 6500.0, q = lastWidth > 0.0 ? lastWidth : 1.4;
    auto curve = [&] (double reductionDb, bool close)
    {
        juce::Path p;
        const double g0 = std::pow (10.0, -reductionDb / 20.0);
        const int n = std::max (60, (int) inner.getWidth() / 3);
        for (int i = 0; i <= n; ++i)
        {
            const double f = kFMin * std::pow (kFMax / kFMin, (double) i / n);
            const float x = inner.getX() + inner.getWidth() * (float) i / (float) n;
            const float y = yForDb (std::clamp (splitResponseDb (f, f0, q, g0), dbBottom, dbTop));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (close) { p.lineTo (inner.getRight(), yForDb (0.0)); p.lineTo (inner.getX(), yForDb (0.0)); p.closeSubPath(); }
        return p;
    };
    const bool wideMode = modeSwitch.getToggleState();
    if (! wideMode)
    {
        g.setColour (accent.withAlpha (0.28f));
        g.strokePath (curve (lastRange, false), juce::PathStrokeType (1.0f, juce::PathStrokeType::curved), {});
        const auto live = curve ((double) shownReduction, true);
        g.setColour (accent.withAlpha (0.30f));
        g.fillPath (live);
        g.setColour (accent);
        g.strokePath (curve ((double) shownReduction, false), juce::PathStrokeType (std::max (1.2f, 1.8f * pixelScale)));
    }
    else
    {
        // Wide : le gain s'applique à tout le spectre — ligne plate abaissée.
        const float y = yForDb (-(double) shownReduction);
        g.setColour (accent.withAlpha (0.30f));
        g.fillRect (juce::Rectangle<float> (inner.getX(), yForDb (0.0), inner.getWidth(), y - yForDb (0.0)));
        g.setColour (accent);
        g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
    }

    // Marqueur de fréquence centrale.
    g.setColour (accent.withAlpha (0.55f));
    const float xc = xForFreq (inner, f0);
    g.drawVerticalLine ((int) xc, inner.getY(), inner.getBottom());

    // Valeur de réduction.
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (NextLookAndFeel::font (std::max (9.0f, 17.0f * pixelScale), true));
    g.drawText (juce::String (-shownReduction, 1) + " dB", inner.removeFromTop (22.0f * pixelScale).removeFromRight (90.0f * pixelScale).toNearestInt(), juce::Justification::centredRight);

    // Niveau de la bande sibilante (dBFS crête) face au seuil.
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (meter, 2.0f);
    auto xForLevel = [&] (double db) { return meter.getX() + meter.getWidth() * (float) std::clamp ((db + kDbMax) / kDbMax, 0.0, 1.0); };
    const float lx = xForLevel (bandDb);
    g.setColour (bandDb > lastThreshold ? accent : accent.withAlpha (0.45f));
    g.fillRoundedRectangle (meter.withRight (std::max (meter.getX() + 1.0f, lx)), 2.0f);
    g.setColour (juce::Colours::white);
    g.drawVerticalLine ((int) xForLevel (lastThreshold), meter.getY() - 2.0f, meter.getBottom() + 2.0f);
}

void E9dsFaceplate::paint (juce::Graphics& g)
{
    drawPlate (g, plate, theme(), brushed, pixelScale);
    drawScrews (g, plate, pixelScale);
    paintDisplay (g);
}

void E9dsFaceplate::paintOverChildren (juce::Graphics& g)
{
    if (bypassed)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.excludeClipRegion (bypassSwitch.getBounds());
        g.setColour (juce::Colour (0xff10131a).withAlpha (0.38f));
        g.fillRect (plate);
    }
}

} // namespace aura::ui
