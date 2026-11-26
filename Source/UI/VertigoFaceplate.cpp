// AuraRack — NextDSP · façades V9C3 / V9P2

#include "VertigoFaceplate.h"

namespace aura::ui
{
namespace
{
const PlateTheme& theme (aura::ModuleType t)
{
    static const PlateTheme c = [] {
        PlateTheme p { juce::Colour (0xff25302a), juce::Colour (0xff1d2620), juce::Colour (0xff141a16), juce::Colour (0xffdfe8d8), juce::Colour (0xff35423a) };
        p.led = juce::Colour (0xff8fd14f); p.ledDim = juce::Colour (0xff25361a); p.trackFill = juce::Colour (0xff0b100c);
        p.darkCap = true; p.alert = juce::Colour (0xffff6a4d); p.grain = 0.5f;
        return p;
    }();
    static const PlateTheme pre = [] {
        PlateTheme p { juce::Colour (0xff3a3128), juce::Colour (0xff2c251e), juce::Colour (0xff1e1914), juce::Colour (0xfff0e2cc), juce::Colour (0xff4a3f33) };
        p.led = juce::Colour (0xffe8a24a); p.ledDim = juce::Colour (0xff3d2c14); p.trackFill = juce::Colour (0xff120e0a);
        p.darkCap = false; p.alert = juce::Colour (0xffff6a4d); p.grain = 0.8f;
        return p;
    }();
    return t == aura::ModuleType::v9c3 ? c : pre;
}

struct KnobDef { int param; const char* name; };
}

VertigoFaceplate::VertigoFaceplate (aura::ModuleType t, juce::AudioProcessorValueTreeState& a, int s) : type (t), apvts (a), slot (s)
{
    using C = aura::AuraV9C3;
    using P = aura::AuraV9P2;
    std::vector<KnobDef> kd;
    std::vector<SwitchDef> sd;
    if (type == aura::ModuleType::v9c3)
    {
        kd = { { C::threshold, "Threshold" }, { C::ratio, "Ratio" }, { C::attack, "Attack" }, { C::release, "Release" },
               { C::makeUp, "Make-Up" }, { C::scHpf, "SC HPF" }, { C::mode, "Mode" }, { C::focus, "Focus" } };
        sd = { { C::detect, "PEAK", "RMS", false }, { C::autoFocus, "MANUAL", "AUTO", false } };
    }
    else
    {
        kd = { { P::gain, "Gain" }, { P::fine, "Fine" }, { P::hpf, "HPF" }, { P::phase, "Phase \xc3\x98" }, { P::rOffset, "R Offset" } };
        sd = { { P::pad, "PAD OFF", "PAD -20", false }, { P::input, "MIC", "INST / DI", false }, { P::link, "DUAL", "LINKED", false } };
    }
    for (auto& k : kd)
    {
        auto kn = std::make_unique<PlateKnob> (theme (type));
        kn->bind (*apvts.getParameter (aura::params::generic (slot, k.param)));
        kn->setName (juce::String::fromUTF8 (k.name));
        addAndMakeVisible (*kn);
        knobs.push_back (std::move (kn));
    }
    for (auto& d : sd)
    {
        auto sw = std::make_unique<PlateSwitch> (theme (type), juce::String::fromUTF8 (d.off), juce::String::fromUTF8 (d.on), d.alert);
        attachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::generic (slot, d.param)), *sw, nullptr));
        addAndMakeVisible (*sw);
        switches.push_back (std::move (sw));
    }
    auto bp = std::make_unique<PlateSwitch> (theme (type), "ACTIVE", "BYPASS", true);
    attachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::bypass (slot)), *bp, nullptr));
    addAndMakeVisible (*bp);
    bypassSwitch = bp.get();
    switches.push_back (std::move (bp));
    bypassed = bypassSwitch->getToggleState();
}

double VertigoFaceplate::plain (int param) const
{
    const auto* raw = apvts.getRawParameterValue (aura::params::generic (slot, param));
    const auto& info = type == aura::ModuleType::v9c3 ? aura::AuraV9C3::info() : aura::AuraV9P2::info();
    return info.params[(std::size_t) param].toPlain (raw != nullptr ? raw->load() : 0.0f);
}

void VertigoFaceplate::resized()
{
    pixelScale = std::min ((float) getWidth() / (float) kWideWidth, (float) getHeight() / (float) kWideHeight);
    const float ox = ((float) getWidth() - kWideWidth * pixelScale) * 0.5f, oy = ((float) getHeight() - kWideHeight * pixelScale) * 0.5f;
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<float> (ox + x * pixelScale, oy + y * pixelScale, w * pixelScale, h * pixelScale); };
    plate = getLocalBounds().toFloat();
    display = R (36.0f, 24.0f, 928.0f, 150.0f);

    const bool c = type == aura::ModuleType::v9c3;
    const float kw = c ? 100.0f : 146.0f;
    for (std::size_t i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (R (36.0f + (float) i * kw, 184.0f, kw, 172.0f).toNearestInt());
    const float sx = c ? 846.0f : 790.0f, sw = c ? 118.0f : 174.0f;
    const float sh = switches.size() <= 3 ? 52.0f : 32.0f, step = switches.size() <= 3 ? 62.0f : 36.0f;
    const float y0 = switches.size() <= 3 ? 190.0f : 184.0f;
    for (std::size_t i = 0; i < switches.size(); ++i) switches[i]->setBounds (R (sx, y0 + (float) i * step, sw, sh).toNearestInt());
    brushed = makeBrushedPlate (getWidth(), getHeight(), theme (type), display.getCentre(), display.getWidth() * 0.5f, std::max (6.0f, 11.0f * pixelScale));
}

void VertigoFaceplate::tick (float dt, const FaceplateFeed& feed)
{
    const bool nowBypassed = bypassSwitch->getToggleState();
    bool dirty = nowBypassed != bypassed;
    bypassed = nowBypassed;

    if (type == aura::ModuleType::v9c3)
    {
        const float target = bypassed ? 0.0f : feed.reductionDb;
        const float prev = shownReduction;
        shownReduction += (target - shownReduction) * (1.0f - std::exp (-dt * 30.0f));
        if (shownReduction < 0.005f) shownReduction = 0.0f;
        dirty = dirty || std::abs (shownReduction - prev) > 0.002f;
        const double t = plain (aura::AuraV9C3::threshold), f = plain (aura::AuraV9C3::focus);
        const bool af = plain (aura::AuraV9C3::autoFocus) > 0.5;
        if (t != lastThreshold || f != lastFocus || af != autoFocus) { lastThreshold = t; lastFocus = f; autoFocus = af; dirty = true; }
        // En Auto Focus le moteur pilote le seuil : le potard le montre, estompé.
        knobs[0]->setDimmed (af);
        if (af) knobs[0]->setLive (std::clamp ((feed.aux + 22.0) / 45.0, 0.0, 1.0), juce::String (feed.aux, 1) + " dBu");
        else knobs[0]->setLive (std::nullopt, {});
        knobs[7]->setDimmed (! af);
        if (af && std::abs (feed.aux - (float) lastThreshold) > 0.05f) { lastThreshold = feed.aux; dirty = true; }
    }
    else
    {
        const float lv = bypassed ? -120.0f : feed.aux;
        if (std::abs (lv - level) > 0.1f) { level = lv; dirty = true; }
        const float lampTarget = level >= (float) aura::AuraV9P2::kClipLampDbu ? 1.0f : 0.0f;
        const float prev = lamp;
        lamp += (lampTarget - lamp) * (1.0f - std::exp (-dt * (lampTarget > lamp ? 60.0f : 6.0f)));
        if (lamp < 0.01f) lamp = 0.0f;
        dirty = dirty || std::abs (lamp - prev) > 0.01f;
        knobs[4]->setDimmed (plain (aura::AuraV9P2::link) > 0.5);
    }
    if (dirty) repaint (display.expanded (2.0f).getSmallestIntegerContainer());
}

void VertigoFaceplate::paintDisplay (juce::Graphics& g) const
{
    const auto accent = theme (type).led;
    auto r = display;
    g.setColour (juce::Colour (0xff0a0b0e));
    g.fillRoundedRectangle (r, 6.0f * pixelScale);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r, 6.0f * pixelScale, 1.0f);
    auto inner = r.reduced (18.0f * pixelScale, 14.0f * pixelScale);
    const float fontPx = std::max (7.0f, 11.0f * pixelScale);
    g.setFont (NextLookAndFeel::font (fontPx, true));

    if (type == aura::ModuleType::v9c3)
    {
        auto head = inner.removeFromTop (34.0f * pixelScale);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (NextLookAndFeel::font (std::max (9.0f, 26.0f * pixelScale), true));
        g.drawText (juce::String (-shownReduction, 1) + " dB", head.removeFromRight (160.0f * pixelScale).toNearestInt(), juce::Justification::centredRight);
        g.setFont (NextLookAndFeel::font (fontPx, true));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawText (autoFocus ? "AUTO FOCUS  " + juce::String (lastThreshold, 1) + " dBu" : "THRESHOLD  " + juce::String (lastThreshold, 1) + " dBu", head.toNearestInt(), juce::Justification::centredLeft);

        // Bargraphe : 0–6 dB sur la moitié gauche (zone fine), 6–24 dB sur la moitié droite.
        auto bar = inner.withTrimmedTop (18.0f * pixelScale).withHeight (34.0f * pixelScale);
        auto xFor = [&] (double db) {
            db = std::clamp (db, 0.0, 24.0);
            const double pos = db <= 6.0 ? 0.5 * db / 6.0 : 0.5 + 0.5 * (db - 6.0) / 18.0;
            return bar.getX() + bar.getWidth() * (float) pos;
        };
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (bar, 3.0f);
        for (int db : { 0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24 })
        {
            const float x = xFor (db);
            const bool major = db == 0 || db == 3 || db == 6 || db == 12 || db == 24;
            g.setColour (juce::Colours::white.withAlpha (major ? 0.45f : 0.2f));
            g.drawVerticalLine ((int) x, bar.getBottom() + 2.0f, bar.getBottom() + (major ? 10.0f : 6.0f) * pixelScale);
            if (major) g.drawText (juce::String (db), (int) x - 16, (int) (bar.getBottom() + 12.0f * pixelScale), 32, (int) (14 * pixelScale), juce::Justification::centred);
        }
        const float fx = xFor (shownReduction);
        auto fill = bar.withRight (std::max (bar.getX() + 1.0f, fx));
        g.setColour (shownReduction > 6.0f ? juce::Colour (0xffffb454) : accent);
        g.fillRoundedRectangle (fill, 3.0f);
        if (autoFocus)
        {
            g.setColour (juce::Colours::white);
            g.drawVerticalLine ((int) xFor (lastFocus), bar.getY() - 3.0f, bar.getBottom() + 3.0f);
        }
    }
    else
    {
        auto head = inner.removeFromTop (34.0f * pixelScale);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (NextLookAndFeel::font (std::max (9.0f, 26.0f * pixelScale), true));
        g.drawText (level > -100.0f ? juce::String (level, 1) + " dBu" : "-- dBu", head.removeFromRight (190.0f * pixelScale).toNearestInt(), juce::Justification::centredRight);
        g.setFont (NextLookAndFeel::font (fontPx, true));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawText ("NET GAIN  " + juce::String (aura::AuraV9P2::netGainDb ((int) std::lround (plain (aura::AuraV9P2::gain)), plain (aura::AuraV9P2::fine), plain (aura::AuraV9P2::pad) > 0.5) , 1) + " dB",
                    head.toNearestInt(), juce::Justification::centredLeft);

        auto bar = inner.withTrimmedTop (18.0f * pixelScale).withHeight (34.0f * pixelScale);
        bar.removeFromRight (70.0f * pixelScale);
        constexpr double lo = -40.0, hi = 26.0;
        auto xFor = [&] (double db) { return bar.getX() + bar.getWidth() * (float) (std::clamp (db, lo, hi) - lo) / (float) (hi - lo); };
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (bar, 3.0f);
        for (int db : { -40, -30, -20, -10, 0, 10, 19, 24 })
        {
            const float x = xFor (db);
            g.setColour (juce::Colours::white.withAlpha (db == 0 || db == 19 ? 0.5f : 0.25f));
            g.drawVerticalLine ((int) x, bar.getBottom() + 2.0f, bar.getBottom() + 8.0f * pixelScale);
            g.drawText (juce::String (db), (int) x - 16, (int) (bar.getBottom() + 12.0f * pixelScale), 32, (int) (14 * pixelScale), juce::Justification::centred);
        }
        const float lx = xFor (level);
        g.setColour (level >= (float) aura::AuraV9P2::kClipLampDbu ? juce::Colour (0xffff5d4a) : accent);
        g.fillRoundedRectangle (bar.withRight (std::max (bar.getX() + 1.0f, lx)), 3.0f);
        // Voyant Signal (vert) / Clip (rouge).
        auto lampArea = juce::Rectangle<float> (bar.getRight() + 16.0f * pixelScale, bar.getY(), 54.0f * pixelScale, bar.getHeight());
        const bool signal = level > -40.0f;
        g.setColour (juce::Colour (0xff3ddc84).withAlpha (signal ? 0.95f : 0.18f));
        g.fillEllipse (lampArea.removeFromLeft (lampArea.getWidth() * 0.5f).reduced (4.0f * pixelScale));
        drawRedLamp (g, lampArea, lamp);
    }
}

void VertigoFaceplate::paint (juce::Graphics& g)
{
    drawPlate (g, plate, theme (type), brushed, pixelScale);
    drawScrews (g, plate, pixelScale);
    paintDisplay (g);
}

void VertigoFaceplate::paintOverChildren (juce::Graphics& g)
{
    if (bypassed)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.excludeClipRegion (bypassSwitch->getBounds());
        g.setColour (juce::Colour (0xff10131a).withAlpha (0.38f));
        g.fillRect (plate);
    }
}

} // namespace aura::ui
