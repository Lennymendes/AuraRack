// AuraRack — NextDSP · kit commun des façades de module

#include "FaceplateKit.h"

namespace aura::ui
{
namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
juce::Point<float> polar (juce::Point<float> c, float r, float a) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }
}

const PlateTheme& blueAnodisedTheme()
{
    static const PlateTheme t { juce::Colour (0xff0f4bc2), juce::Colour (0xff0a3a9c), juce::Colour (0xff062a80),
                                juce::Colour (0xffd2e1ff), juce::Colour (0xff02134a) };
    return t;
}

const PlateTheme& silverPanelTheme()
{
    static const PlateTheme t = [] {
        PlateTheme p { juce::Colour (0xffdfe2e6), juce::Colour (0xffc4c8ce), juce::Colour (0xff9ea4ac), juce::Colour (0xff1b1f27), juce::Colour (0xff6c727c) };
        p.trackFill = juce::Colour (0xff15171c);
        p.darkCap = true;
        p.alert = juce::Colour (0xffff6a4d);
        return p;
    }();
    return t;
}

const PlateTheme& graphiteRoseTheme()
{
    static const PlateTheme t = [] {
        PlateTheme p { juce::Colour (0xff2b2d34), juce::Colour (0xff22242a), juce::Colour (0xff181a1f), juce::Colour (0xffe7d9e1), juce::Colour (0xff3a3d47) };
        p.led = juce::Colour (0xffff7ab8);
        p.ledDim = juce::Colour (0xff3d2231);
        p.trackFill = juce::Colour (0xff0e0f13);
        p.darkCap = true;
        p.alert = juce::Colour (0xffff6a4d);
        p.grain = 0.5f;
        return p;
    }();
    return t;
}

//==============================================================================
PlateKnob::PlateKnob (const PlateTheme& t) : theme (t)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kPi * 1.25f, kPi * 2.75f, true);
    setMouseDragSensitivity (200);
}

void PlateKnob::bind (juce::RangedAudioParameter& p)
{
    attachment = std::make_unique<juce::SliderParameterAttachment> (p, *this, nullptr);
}

void PlateKnob::setLive (std::optional<double> normalised, const juce::String& text)
{
    if (live == normalised && liveText == text) return;
    live = normalised; liveText = text;
    repaint();
}

void PlateKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float labelH = std::max (9.0f, b.getHeight() * 0.105f);
    auto top = b.withHeight (labelH);
    auto bottom = b.removeFromBottom (labelH * 1.1f);
    const auto knobArea = b.withTrimmedTop (labelH + 1.0f).withTrimmedBottom (1.0f);
    const float d = std::min (knobArea.getWidth(), knobArea.getHeight());
    const auto c = knobArea.getCentre();
    const float R = d * 0.5f;
    const float fade = dimmed ? 0.45f : 1.0f;
    juce::Graphics::ScopedSaveState state (g);
    g.beginTransparencyLayer (fade);

    g.setColour (theme.engrave.withAlpha (0.92f));
    g.setFont (fitFont (NextLookAndFeel::font (labelH, true), name, top.getWidth() * 0.98f));
    g.drawText (name, top.toNearestInt(), juce::Justification::centred);
    // Valeur : texte du paramètre, ou valeur imposée par le moteur.
    g.setColour (theme.darkCap ? theme.led.darker (0.1f) : theme.led);
    {
        const auto readout = bottom.toNearestInt();
        if (theme.darkCap)
        {
            g.setColour (juce::Colour (0xff15171c));
            g.fillRoundedRectangle (readout.toFloat().reduced (readout.getWidth() * 0.12f, 0.0f), 2.0f);
            g.setColour (theme.led);
        }
        const auto text = live.has_value() ? liveText : getTextFromValue (getValue());
        g.setFont (fitFont (NextLookAndFeel::font (labelH * 0.95f, true), text, readout.toFloat().getWidth() * 0.96f));
        g.drawText (text, readout, juce::Justification::centred);
    }

    // Collerette.
    juce::ColourGradient collar (juce::Colour (0xff23262e), c.x - R * 0.5f, c.y - R * 0.7f, juce::Colour (0xff020203), c.x + R * 0.6f, c.y + R * 0.8f, false);
    g.setGradientFill (collar);
    g.fillEllipse (c.x - R, c.y - R, d, d);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - R, c.y - R, d, d, 1.0f);

    // Anneau de LED : 36 segments sur 270°.
    const float start = kPi * 1.25f, sweep = kPi * 1.5f;
    const float prop = live.has_value() ? (float) *live : (float) valueToProportionOfLength (getValue());
    const int segments = 36;
    const float ringR = R * 0.86f, thick = std::max (1.6f, d * 0.055f);
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = start + sweep * (float) i / (float) segments, a1 = start + sweep * ((float) i + 0.72f) / (float) segments;
        const bool lit = ((float) i + 0.5f) / (float) segments <= prop + 1.0e-4f;
        juce::Path seg;
        seg.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a0, a1, true);
        if (lit)
        {
            g.setColour (theme.led.withAlpha (0.22f));
            g.strokePath (seg, juce::PathStrokeType (thick * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        g.setColour (lit ? theme.led : theme.ledDim);
        g.strokePath (seg, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // Capuchon : aluminium brossé (secteurs radiaux) ou noir moleté.
    const float capR = R * 0.60f;
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        g.reduceClipRegion (clip);
        const int wedges = theme.darkCap ? 72 : 120;
        for (int i = 0; i < wedges; ++i)
        {
            const float a0 = 2.0f * kPi * (float) i / (float) wedges, a1 = 2.0f * kPi * ((float) i + 1.15f) / (float) wedges;
            float light;
            if (theme.darkCap)
                light = 0.10f + 0.035f * (float) (i % 2) + 0.05f * std::abs (std::cos (a0 - 0.6f));
            else
                light = 0.50f + 0.34f * std::abs (std::cos (a0 - 0.6f)) + 0.10f * std::cos (2.0f * (a0 + 1.2f));
            g.setColour (juce::Colour::fromFloatRGBA (light * 0.93f, light * 0.95f, light, 1.0f));
            juce::Path w;
            w.startNewSubPath (c);
            w.lineTo (polar (c, capR * 1.1f, a0));
            w.lineTo (polar (c, capR * 1.1f, a1));
            w.closeSubPath();
            g.fillPath (w);
        }
        juce::ColourGradient shade (juce::Colours::white.withAlpha (theme.darkCap ? 0.16f : 0.22f), c.x - capR * 0.5f, c.y - capR * 0.55f,
                                    juce::Colours::black.withAlpha (0.30f), c.x + capR * 0.7f, c.y + capR * 0.8f, false);
        g.setGradientFill (shade);
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, std::max (1.0f, d * 0.015f));
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawEllipse (c.x - capR + 1.0f, c.y - capR + 1.0f, capR * 2.0f - 2.0f, capR * 2.0f - 2.0f, 0.8f);

    // Repère.
    const float angle = start + sweep * prop;
    if (! theme.darkCap)
    {
        g.setColour (juce::Colour (0xff0b0c10));
        g.drawLine ({ polar (c, capR * 0.30f, angle), polar (c, capR * 0.92f, angle) }, std::max (2.0f, d * 0.04f));
    }
    g.setColour (juce::Colours::white.withAlpha (0.94f));
    g.drawLine ({ polar (c, capR * 0.42f, angle), polar (c, capR * 0.88f, angle) }, std::max (1.2f, d * (theme.darkCap ? 0.035f : 0.02f)));
    g.endTransparencyLayer();
}

//==============================================================================
PlateSwitch::PlateSwitch (const PlateTheme& t, juce::String off, juce::String on, bool onIsAlert)
    : theme (t), offText (std::move (off)), onText (std::move (on)), alertOnState (onIsAlert)
{
    setClickingTogglesState (true);
}

void PlateSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto b = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float labelH = std::max (8.0f, b.getHeight() * 0.36f);
    auto labels = b.removeFromTop (labelH);
    const auto track = b.reduced (b.getWidth() * 0.12f, b.getHeight() * 0.10f);
    const auto dim = theme.engrave.withAlpha (theme.darkCap ? 0.40f : 0.45f);
    const auto lit = theme.darkCap ? theme.engrave : theme.led;
    const auto litOn = alertOnState ? theme.alert : lit;

    const float half = labels.getWidth() * 0.5f * 0.94f, fontH = std::min (labelH * 0.95f, getWidth() * 0.115f);   // même corps pour tous les interrupteurs d'une façade
    auto font = fitFont (NextLookAndFeel::font (fontH, true), offText, half);
    const auto f2 = fitFont (NextLookAndFeel::font (fontH, true), onText, half);
    if (f2.getHeight() < font.getHeight()) font = f2;
    g.setFont (font);
    g.setColour (on ? dim : (theme.darkCap ? theme.engrave : theme.led));
    g.drawText (offText, labels.removeFromLeft (labels.getWidth() * 0.5f).toNearestInt(), juce::Justification::centred);
    g.setColour (on ? litOn : dim);
    g.drawText (onText, labels.toNearestInt(), juce::Justification::centred);

    g.setColour (theme.trackFill);
    g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.35f : 0.18f));
    g.drawRoundedRectangle (track, track.getHeight() * 0.5f, 1.0f);

    const float kw = track.getWidth() * 0.46f;
    const auto lever = juce::Rectangle<float> (on ? track.getRight() - kw - 1.5f : track.getX() + 1.5f, track.getY() + 1.5f, kw, track.getHeight() - 3.0f);
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
void VuDial::setGeometry (juce::Rectangle<float> f, float pixelScale)
{
    frame = f; px = pixelScale;
    window = f.reduced (std::max (4.0f, 9.0f * pixelScale));
    arcRadius = window.getWidth() * 0.74f;
    pivot = { window.getCentreX(), window.getY() + window.getHeight() * 0.26f + arcRadius };
}

float VuDial::angleFor (double pos) const noexcept
{
    const float span = kSpanDeg * kPi / 180.0f;
    const bool rev = scale != nullptr && scale->reversed;
    return rev ? span - 2.0f * span * (float) pos : -span + 2.0f * span * (float) pos;
}

void VuDial::snap (double value) { needle = scale != nullptr ? scale->position (value) : 0.0; velocity = 0.0; }

bool VuDial::advance (float dt, double value)
{
    if (scale == nullptr) return false;
    const double targetPos = scale->position (value);
    const double omega = 2.0 * juce::MathConstants<double>::pi * 4.0, zeta = 0.9;   // masse-ressort ≈ 4 Hz
    double remaining = std::min (0.1, (double) dt);
    const double before = needle;
    while (remaining > 0.0)
    {
        const double h = std::min (0.002, remaining);
        velocity += h * (omega * omega * (targetPos - needle) - 2.0 * zeta * omega * velocity);
        needle += h * velocity;
        remaining -= h;
    }
    needle = std::clamp (needle, 0.0, 1.0);
    return std::abs (needle - before) * (double) arcRadius * 1.33 > 0.05;
}

void VuDial::paintFace (juce::Graphics& g) const
{
    if (scale == nullptr) return;
    g.setColour (juce::Colour (0xff050608));
    g.fillRoundedRectangle (frame, 4.0f * px);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (frame.reduced (0.5f), 4.0f * px, 1.0f);

    const auto w = window;
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (w, 2.5f * px);
    g.reduceClipRegion (clip);

    juce::ColourGradient face (juce::Colour (0xfffbe3a6), w.getCentreX(), w.getBottom() - w.getHeight() * 0.1f, juce::Colour (0xff9a6c25), w.getX(), w.getY(), true);
    face.addColour (0.55, juce::Colour (0xffe8bd6e));
    g.setGradientFill (face);
    g.fillRect (w);
    juce::ColourGradient inner (juce::Colours::black.withAlpha (0.38f), 0.0f, w.getY(), juce::Colours::transparentBlack, 0.0f, w.getY() + w.getHeight() * 0.28f, false);
    g.setGradientFill (inner);
    g.fillRect (w);

    const float span = kSpanDeg * kPi / 180.0f;
    const juce::Colour ink (0xff2a1a08), warm (0xffb3262b);
    g.setColour (ink.withAlpha (0.9f));
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, arcRadius, arcRadius, 0.0f, -span, span, true);
    g.strokePath (arc, juce::PathStrokeType (std::max (1.0f, 1.3f * px)));

    const float majorLen = arcRadius * 0.075f, minorLen = arcRadius * 0.04f;
    g.setFont (NextLookAndFeel::font (std::max (8.0f, arcRadius * 0.085f), true));
    for (const auto& t : scale->ticks)
    {
        const float a = angleFor (scale->position (t.value));
        g.setColour (t.warm ? warm : ink);
        if (! t.major)
        {
            g.drawLine ({ polar (pivot, arcRadius, a), polar (pivot, arcRadius - minorLen, a) }, std::max (0.8f, 1.0f * px));
            continue;
        }
        g.drawLine ({ polar (pivot, arcRadius, a), polar (pivot, arcRadius - majorLen, a) }, std::max (1.2f, 1.8f * px));
        const auto tp = polar (pivot, arcRadius + majorLen * 1.5f, a);
        const float tw = arcRadius * 0.16f;
        g.drawText (t.label, juce::Rectangle<float> (tp.x - tw * 0.5f, tp.y - tw * 0.32f, tw, tw * 0.64f).toNearestInt(), juce::Justification::centred);
    }

    // Repère de cible (Auto Focus) : petit triangle sous l'arc, couleur de la plaque.
    if (marker.has_value())
    {
        const float a = angleFor (scale->position (*marker));
        const auto tip = polar (pivot, arcRadius - majorLen * 1.1f, a);
        const float s2 = std::max (4.0f, arcRadius * 0.055f);
        juce::Path tri;
        tri.addTriangle (tip, polar (pivot, arcRadius - majorLen * 1.1f - s2 * 1.7f, a - s2 / arcRadius), polar (pivot, arcRadius - majorLen * 1.1f - s2 * 1.7f, a + s2 / arcRadius));
        g.setColour (juce::Colour (0xff0f4bc2));
        g.fillPath (tri);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.strokePath (tri, juce::PathStrokeType (std::max (0.8f, 1.0f * px)));
    }

    g.setColour (ink.withAlpha (0.75f));
    g.setFont (NextLookAndFeel::font (std::max (7.0f, arcRadius * 0.062f), true));
    g.drawText (scale->caption, w.withTrimmedTop (w.getHeight() * 0.66f).toNearestInt(), juce::Justification::centred);
}

void VuDial::paintGlass (juce::Graphics& g) const
{
    if (scale == nullptr) return;
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (window, 2.5f * px);
    g.reduceClipRegion (clip);

    const float a = angleFor (needle);
    const auto tip = polarFromPivot (arcRadius * 0.985f, a);
    const float lw = std::max (1.2f, 1.7f * px);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.drawLine ({ polarFromPivot (arcRadius * 0.55f, a).translated (2.0f * px, 2.5f * px), tip.translated (2.0f * px, 2.5f * px) }, lw);
    g.setColour (juce::Colour (0xff1c0d06));
    g.drawLine ({ polarFromPivot (arcRadius * 0.55f, a), tip }, lw);

    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.20f), 0.0f, window.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, window.getY() + window.getHeight() * 0.5f, false);
    g.setGradientFill (glass);
    g.fillRect (window);
    juce::Path sheen;
    sheen.startNewSubPath (window.getX() + window.getWidth() * 0.08f, window.getBottom());
    sheen.lineTo (window.getX() + window.getWidth() * 0.30f, window.getY());
    sheen.lineTo (window.getX() + window.getWidth() * 0.40f, window.getY());
    sheen.lineTo (window.getX() + window.getWidth() * 0.18f, window.getBottom());
    sheen.closeSubPath();
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillPath (sheen);
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.drawRoundedRectangle (window, 2.5f * px, std::max (1.0f, 1.5f * px));
}

//==============================================================================
juce::Image makeBrushedPlate (int w, int h, const PlateTheme& theme, juce::Point<float> c, float reach, float pitch)
{
    juce::Image img (juce::Image::ARGB, std::max (1, w), std::max (1, h), true);
    juce::Graphics g (img);
    std::uint32_t seed = 0x9e3779b9u;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
    for (int y = 0; y < h; ++y)
        for (int k = 0; k < 3; ++k)
        {
            const float x0 = rnd() * (float) w, len = (0.25f + rnd() * 0.75f) * (float) w;
            g.setColour ((rnd() > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha ((0.02f + rnd() * 0.035f) * theme.grain));
            g.fillRect (x0, (float) y, len, 1.0f);
        }
    // Trame de points qui s'estompe avec la distance au VU-mètre.
    for (float y = 0.0f, row = 0.0f; y < (float) h; y += pitch * 0.5f, row += 1.0f)
        for (float x = (std::fmod (row, 2.0f) < 1.0f ? 0.0f : pitch * 0.5f); x < (float) w; x += pitch)
        {
            const float f = 1.0f - std::hypot ((x - c.x) * 0.7f, y - c.y) / reach;
            if (f <= 0.0f) continue;
            const float r = pitch * 0.30f * f;
            g.setColour (theme.dot.withAlpha (0.30f * f));
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
        }
    return img;
}

void drawPlate (juce::Graphics& g, juce::Rectangle<float> r, const PlateTheme& theme, const juce::Image& brushed, float px)
{
    juce::ColourGradient base (theme.top, 0.0f, r.getY(), theme.bottom, 0.0f, r.getBottom(), false);
    base.addColour (0.5, theme.middle);
    g.setGradientFill (base);
    g.fillRect (r);
    g.drawImageAt (brushed, 0, 0);

    const float bevel = std::max (1.5f, 3.0f * px);
    auto top = r;
    juce::ColourGradient edge (juce::Colours::white.withAlpha (0.14f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getX(), r.getY() + bevel * 6.0f, false);
    g.setGradientFill (edge);
    g.fillRect (top.removeFromTop (bevel * 6.0f));
    g.setColour (juce::Colours::white.withAlpha (0.38f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), r.getWidth(), bevel * 0.6f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), bevel * 0.6f, r.getHeight()));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom() - bevel * 0.8f, r.getWidth(), bevel * 0.8f));
    g.fillRect (juce::Rectangle<float> (r.getRight() - bevel * 0.8f, r.getY(), bevel * 0.8f, r.getHeight()));
}

void drawScrews (juce::Graphics& g, juce::Rectangle<float> r, float px)
{
    const float d = std::max (9.0f, 15.0f * px), m = d * 1.15f;
    const juce::Point<float> pts[] = { { r.getX() + m, r.getY() + m }, { r.getRight() - m, r.getY() + m },
                                       { r.getX() + m, r.getBottom() - m }, { r.getRight() - m, r.getBottom() - m } };
    int k = 0;
    for (auto p : pts)
    {
        if (r.getHeight() < 300.0f && p.y > r.getCentreY()) { ++k; continue; }   // carte compacte : les vis du bas gêneraient les contrôles
        juce::ColourGradient grad (juce::Colour (0xfff4f6fa), p.x - d * 0.3f, p.y - d * 0.35f, juce::Colour (0xff6e7684), p.x + d * 0.4f, p.y + d * 0.45f, true);
        g.setGradientFill (grad);
        g.fillEllipse (p.x - d * 0.5f, p.y - d * 0.5f, d, d);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (p.x - d * 0.5f, p.y - d * 0.5f, d, d, 1.0f);
        const float a = 0.5f + 0.9f * (float) (k++);
        g.setColour (juce::Colour (0xff2a2e38));
        g.drawLine ({ polar (p, d * 0.36f, a), polar (p, d * 0.36f, a + kPi) }, std::max (1.0f, d * 0.12f));
    }
}

void drawRedLamp (juce::Graphics& g, juce::Rectangle<float> area, float lamp)
{
    const auto c = area.getCentre();
    const float d = std::min (area.getWidth(), area.getHeight()), R = d * 0.5f;
    if (lamp > 0.05f)
    {
        juce::ColourGradient halo (juce::Colour (0xffff3020).withAlpha (0.55f * lamp), c.x, c.y, juce::Colours::transparentBlack, c.x + R * 2.0f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (c.x - R * 2.0f, c.y - R * 2.0f, R * 4.0f, R * 4.0f);
    }
    juce::ColourGradient ring (juce::Colour (0xff3a3d46), c.x - R * 0.6f, c.y - R * 0.8f, juce::Colour (0xff050506), c.x + R * 0.6f, c.y + R * 0.9f, false);
    g.setGradientFill (ring);
    g.fillEllipse (c.x - R, c.y - R, d, d);
    const float lr = R * 0.78f;
    const auto off = juce::Colour (0xff3d0909), on = juce::Colour (0xffff2d1c);
    juce::ColourGradient lens (off.interpolatedWith (juce::Colour (0xffff7a5c), lamp), c.x - lr * 0.3f, c.y - lr * 0.4f,
                               off.interpolatedWith (on, lamp).darker (0.6f - 0.4f * lamp), c.x + lr, c.y + lr, true);
    g.setGradientFill (lens);
    g.fillEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.20f));
    for (int i = 0; i < 6; ++i)
        g.drawLine ({ polar (c, lr * 0.25f, (float) i * kPi / 3.0f), polar (c, lr, (float) i * kPi / 3.0f) }, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.22f + 0.25f * lamp));
    g.fillEllipse (c.x - lr * 0.62f, c.y - lr * 0.78f, lr * 0.7f, lr * 0.32f);
}

} // namespace aura::ui
