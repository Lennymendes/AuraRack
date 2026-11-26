// AuraRack — NextDSP · façades V9C3 / V9P2

#include "VertigoFaceplate.h"

namespace aura::ui
{
namespace
{
PlateTheme makeTheme (juce::uint32 top, juce::uint32 mid, juce::uint32 bot, juce::uint32 engrave, juce::uint32 dot, juce::uint32 led, juce::uint32 ledDim, juce::uint32 track, bool dark, float grain)
{
    PlateTheme p { juce::Colour (top), juce::Colour (mid), juce::Colour (bot), juce::Colour (engrave), juce::Colour (dot) };
    p.led = juce::Colour (led); p.ledDim = juce::Colour (ledDim); p.trackFill = juce::Colour (track);
    p.darkCap = dark; p.alert = juce::Colour (0xffff6a4d); p.grain = grain;
    return p;
}

const PlateTheme& theme (aura::ModuleType t)
{
    using M = aura::ModuleType;
    static const PlateTheme c    = makeTheme (0xff25302a, 0xff1d2620, 0xff141a16, 0xffdfe8d8, 0xff35423a, 0xff8fd14f, 0xff25361a, 0xff0b100c, true, 0.5f);
    static const PlateTheme pre  = makeTheme (0xff3a3128, 0xff2c251e, 0xff1e1914, 0xfff0e2cc, 0xff4a3f33, 0xffe8a24a, 0xff3d2c14, 0xff120e0a, false, 0.8f);
    static const PlateTheme fet  = makeTheme (0xff2b3038, 0xff22262d, 0xff16191e, 0xffe6ebf2, 0xff3a414c, 0xff6fb2ff, 0xff1a2a40, 0xff0c1018, true, 0.7f);
    static const PlateTheme dist = makeTheme (0xff26262b, 0xff1d1d21, 0xff121215, 0xffeeeeee, 0xff3a3a40, 0xffff7a45, 0xff40200f, 0xff0c0c0e, true, 0.4f);
    static const PlateTheme scu  = makeTheme (0xff2e2740, 0xff251f34, 0xff171322, 0xffece6ff, 0xff40375a, 0xffb28dff, 0xff2a2145, 0xff0e0b18, true, 0.5f);
    static const PlateTheme ton  = makeTheme (0xff22363a, 0xff1b2b2e, 0xff111c1e, 0xffe0f3f1, 0xff30474b, 0xff4fd1c5, 0xff143a37, 0xff0a1415, false, 0.6f);
    switch (t)
    {
        case M::v9c3:   return c;
        case M::f976:   return fet;
        case M::d9x:    return dist;
        case M::sculpt: return scu;
        case M::tone:   return ton;
        default:        return pre;
    }
}

constexpr int kMixParam = -1;   // le Mix du slot (paramètre commun, pas un paramètre générique)
struct KnobDef { int param; const char* name; };
}

VertigoFaceplate::VertigoFaceplate (aura::ModuleType t, juce::AudioProcessorValueTreeState& a, int s) : type (t), apvts (a), slot (s)
{
    using C = aura::AuraV9C3;
    using P = aura::AuraV9P2;
    using F = aura::AuraF976;
    using D = aura::AuraD9X;
    using U = aura::AuraSculpt;
    using T = aura::AuraTone;
    using M = aura::ModuleType;
    std::vector<KnobDef> kd;
    std::vector<SwitchDef> sd;
    switch (type)
    {
        case M::v9c3:
            kd = { { C::threshold, "Threshold" }, { C::ratio, "Ratio" }, { C::attack, "Attack" }, { C::release, "Release" },
                   { C::makeUp, "Make-Up" }, { C::scHpf, "SC HPF" }, { C::mode, "Mode" }, { C::focus, "Focus" } };
            sd = { { C::detect, "PEAK", "RMS", false }, { C::autoFocus, "MANUAL", "AUTO", false } };
            break;
        case M::f976:
            kd = { { F::input, "Input" }, { F::output, "Output" }, { F::attack, "Attack" }, { F::release, "Release" }, { F::ratio, "Ratio" },
                   { F::scHpf, "SC HPF" }, { F::meter, "Meter" }, { kMixParam, "Mix" } };
            sd = { { F::revision, "REV A", "REV D/E", false } };
            break;
        case M::d9x:
            kd = { { D::input, "Input" }, { D::ratio, "Ratio" }, { D::attack, "Attack" }, { D::release, "Release" }, { D::distortion, "Dist" },
                   { D::detHpf, "Det HPF" }, { D::bandEmphasis, "Emphasis" }, { D::autoFocus, "Focus" }, { D::output, "Output" }, { kMixParam, "Mix" } };
            sd = { { D::british, "BRITISH OFF", "BRITISH ON", false } };
            break;
        case M::sculpt:
            kd = { { U::attack, "Attack" }, { U::sustain, "Sustain" }, { U::speed, "Speed" }, { U::focus, "Focus" }, { U::focusAmount, "Focus Amt" },
                   { U::ceiling, "Ceiling" }, { U::output, "Output" }, { kMixParam, "Mix" } };
            break;
        case M::tone:
            kd = { { T::body, "Body" }, { T::air, "Air" }, { T::tilt, "Tilt" }, { T::even, "Even" }, { T::odd, "Odd" }, { T::output, "Output" }, { kMixParam, "Mix" } };
            break;
        default:
            kd = { { P::gain, "Gain" }, { P::fine, "Fine" }, { P::hpf, "HPF" }, { P::phase, "Phase \xc3\x98" }, { P::rOffset, "R Offset" } };
            sd = { { P::pad, "PAD OFF", "PAD -20", false }, { P::input, "MIC", "INST / DI", false }, { P::link, "DUAL", "LINKED", false } };
            break;
    }
    for (auto& k : kd)
    {
        auto kn = std::make_unique<PlateKnob> (theme (type));
        kn->bind (*apvts.getParameter (k.param == kMixParam ? aura::params::mix (slot) : aura::params::generic (slot, k.param)));
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
    const auto& info = aura::moduleInfo (type);
    return info.params[(std::size_t) param].toPlain (raw != nullptr ? raw->load() : 0.0f);
}

void VertigoFaceplate::resized()
{
    pixelScale = std::min ((float) getWidth() / (float) kWideWidth, (float) getHeight() / (float) kWideHeight);
    const float ox = ((float) getWidth() - kWideWidth * pixelScale) * 0.5f, oy = ((float) getHeight() - kWideHeight * pixelScale) * 0.5f;
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<float> (ox + x * pixelScale, oy + y * pixelScale, w * pixelScale, h * pixelScale); };
    plate = getLocalBounds().toFloat();
    display = R (36.0f, 24.0f, 928.0f, 150.0f);

    const bool narrow = type != aura::ModuleType::v9p2;   // V9P2 : 5 grands potards ; les autres modules : 7 à 10 potards
    const float sx = narrow ? 846.0f : 790.0f, sw = narrow ? 118.0f : 174.0f;
    const float kw = narrow ? std::min (146.0f, (sx - 36.0f - 8.0f) / (float) knobs.size()) : 146.0f;
    for (std::size_t i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (R (36.0f + (float) i * kw, 184.0f, kw, 172.0f).toNearestInt());
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
    else if (type == aura::ModuleType::v9p2)
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
    else if (type == aura::ModuleType::f976)
    {
        const float target = bypassed ? 0.0f : feed.reductionDb;
        const float prev = shownReduction;
        shownReduction += (target - shownReduction) * (1.0f - std::exp (-dt * 30.0f));
        if (shownReduction < 0.005f) shownReduction = 0.0f;
        const float lv = bypassed ? -120.0f : feed.aux;
        const float pl = level;
        level += (lv - level) * (1.0f - std::exp (-dt * 20.0f));
        dirty = dirty || std::abs (shownReduction - prev) > 0.002f || std::abs (level - pl) > 0.05f;
    }
    else if (type == aura::ModuleType::d9x)
    {
        const float target = bypassed ? 0.0f : feed.reductionDb;
        const float prev = shownReduction;
        shownReduction += (target - shownReduction) * (1.0f - std::exp (-dt * 30.0f));
        if (shownReduction < 0.005f) shownReduction = 0.0f;
        thd = bypassed ? 0.0f : feed.aux;
        const float pl = lampLow, ph = lampHigh;
        lampLow += ((thd >= 1.0f ? 1.0f : 0.0f) - lampLow) * (1.0f - std::exp (-dt * 25.0f));
        lampHigh += ((thd >= 3.0f ? 1.0f : 0.0f) - lampHigh) * (1.0f - std::exp (-dt * 25.0f));
        dirty = dirty || std::abs (shownReduction - prev) > 0.002f || std::abs (lampLow - pl) > 0.01f || std::abs (lampHigh - ph) > 0.01f;
    }
    else if (type == aura::ModuleType::sculpt)
    {
        const float target = bypassed ? 0.0f : feed.aux;
        const float prev = shownGain;
        shownGain += (target - shownGain) * (1.0f - std::exp (-dt * 25.0f));
        if (std::abs (shownGain) < 0.005f) shownGain = 0.0f;
        dirty = dirty || std::abs (shownGain - prev) > 0.002f;
    }
    else
        dirty = dirty || toneKey != toneSignature();
    if (type == aura::ModuleType::tone) toneKey = toneSignature();
    if (dirty) repaint (display.expanded (2.0f).getSmallestIntegerContainer());
}

double VertigoFaceplate::toneSignature() const
{
    using T = aura::AuraTone;
    return plain (T::body) * 1.0 + plain (T::air) * 31.0 + plain (T::tilt) * 997.0 + plain (T::even) * 0.013 + plain (T::odd) * 0.0071;
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
    else if (type == aura::ModuleType::v9p2)
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
    else
        paintNewDisplay (g, inner, accent, fontPx);
}

void VertigoFaceplate::paintNewDisplay (juce::Graphics& g, juce::Rectangle<float> inner, juce::Colour accent, float fontPx) const
{
    using M = aura::ModuleType;
    const auto& info = aura::moduleInfo (type);
    auto fmt = [&] (int p) { return juce::String::fromUTF8 (info.params[(std::size_t) p].format (plain (p)).c_str()); };
    auto head = inner.removeFromTop (34.0f * pixelScale);
    auto bigText = [&] (const juce::String& t, float w)
    {
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (NextLookAndFeel::font (std::max (9.0f, 26.0f * pixelScale), true));
        g.drawText (t, head.removeFromRight (w * pixelScale).toNearestInt(), juce::Justification::centredRight);
        g.setFont (NextLookAndFeel::font (fontPx, true));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
    };
    auto bar = inner.withTrimmedTop (18.0f * pixelScale).withHeight (34.0f * pixelScale);
    auto tickLabel = [&] (float x, const juce::String& t, bool major)
    {
        g.setColour (juce::Colours::white.withAlpha (major ? 0.45f : 0.2f));
        g.drawVerticalLine ((int) x, bar.getBottom() + 2.0f, bar.getBottom() + (major ? 10.0f : 6.0f) * pixelScale);
        if (major) g.drawText (t, (int) x - 18, (int) (bar.getBottom() + 12.0f * pixelScale), 36, (int) (14 * pixelScale), juce::Justification::centred);
    };

    if (type == M::f976)
    {
        const int mode = (int) std::lround (plain (aura::AuraF976::meter));
        const double ref = mode == 1 ? -14.0 : -10.0;   // 0 VU = +4 dBu (−14 dBFS) ou +8 dBu (−10 dBFS)
        bigText (mode == 0 ? juce::String (-shownReduction, 1) + " dB GR" : (level > -100.0f ? juce::String (level - ref, 1) + " VU" : juce::String ("-- VU")), 220.0f);
        g.drawText ("ATTACK " + fmt (aura::AuraF976::attack) + "   RELEASE " + fmt (aura::AuraF976::release) + "   RATIO " + fmt (aura::AuraF976::ratio) + "   " + fmt (aura::AuraF976::revision),
                    head.toNearestInt(), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (bar, 3.0f);
        if (mode == 0)
        {
            auto xFor = [&] (double db) { return bar.getX() + bar.getWidth() * (float) (std::clamp (db, 0.0, 20.0) / 20.0); };
            for (int db : { 0, 2, 4, 8, 12, 16, 20 }) tickLabel (xFor (db), juce::String (db), db == 0 || db == 4 || db == 8 || db == 12 || db == 20);
            g.setColour (shownReduction > 12.0f ? juce::Colour (0xffffb454) : accent);
            g.fillRoundedRectangle (bar.withRight (std::max (bar.getX() + 1.0f, xFor (shownReduction))), 3.0f);
        }
        else
        {
            constexpr double lo = -20.0, hi = 3.0;
            auto xFor = [&] (double v) { return bar.getX() + bar.getWidth() * (float) ((std::clamp (v, lo, hi) - lo) / (hi - lo)); };
            for (int v : { -20, -10, -7, -5, -3, 0, 3 }) tickLabel (xFor (v), juce::String (v), v == -20 || v == -10 || v == -5 || v == 0 || v == 3);
            const double vu = level - ref;
            g.setColour (vu > 0.0 ? juce::Colour (0xffff5d4a) : accent);
            g.fillRoundedRectangle (bar.withRight (std::max (bar.getX() + 1.0f, xFor (vu))), 3.0f);
        }
    }
    else if (type == M::d9x)
    {
        bigText (juce::String (-shownReduction, 1) + " dB", 160.0f);
        g.drawText ("RATIO " + fmt (aura::AuraD9X::ratio) + "   " + fmt (aura::AuraD9X::distortion) + "   ATTACK " + fmt (aura::AuraD9X::attack) + "   RELEASE " + fmt (aura::AuraD9X::release)
                        + (plain (aura::AuraD9X::british) > 0.5 ? "   BRITISH" : ""),
                    head.toNearestInt(), juce::Justification::centredLeft);
        auto lamps = bar.removeFromRight (120.0f * pixelScale);
        bar.removeFromRight (14.0f * pixelScale);
        constexpr int segs = 20;
        const float sw = bar.getWidth() / segs;
        for (int i = 0; i < segs; ++i)
        {
            auto seg = juce::Rectangle<float> (bar.getX() + (float) i * sw + 1.0f, bar.getY(), sw - 2.0f, bar.getHeight());
            const bool lit = shownReduction >= (float) i + 0.5f;
            const auto col = i >= 14 ? juce::Colour (0xffff5d4a) : (i >= 8 ? juce::Colour (0xffffb454) : accent);
            g.setColour (lit ? col : col.withAlpha (0.13f));
            g.fillRoundedRectangle (seg, 2.0f);
            if (i % 4 == 0) { g.setColour (juce::Colours::white.withAlpha (0.4f)); g.drawText (juce::String (i), (int) seg.getX() - 8, (int) (bar.getBottom() + 4.0f * pixelScale), 20, (int) (12 * pixelScale), juce::Justification::centred); }
        }
        auto drawLamp = [&] (juce::Rectangle<float> a, float b, const char* label, juce::Colour c)
        {
            const float d = std::min (a.getHeight(), a.getWidth()) * 0.72f;
            auto dot = juce::Rectangle<float> (d, d).withCentre (a.getCentre().withY (a.getCentreY() - 4.0f * pixelScale));
            g.setColour (c.withAlpha (0.15f + 0.85f * b)); g.fillEllipse (dot);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawText (label, a.toNearestInt().withTop ((int) dot.getBottom()), juce::Justification::centred);
        };
        drawLamp (lamps.removeFromLeft (lamps.getWidth() * 0.5f), lampLow, "THD 1%", juce::Colour (0xffffb454));
        drawLamp (lamps, lampHigh, "THD 3%", juce::Colour (0xffff5d4a));
    }
    else if (type == M::sculpt)
    {
        bigText ((shownGain >= 0.0f ? "+" : "") + juce::String (shownGain, 1) + " dB", 160.0f);
        g.drawText ("ATTACK " + fmt (aura::AuraSculpt::attack) + "   SUSTAIN " + fmt (aura::AuraSculpt::sustain) + "   FOCUS " + fmt (aura::AuraSculpt::focus) + " (" + fmt (aura::AuraSculpt::focusAmount) + ")"
                        + (plain (aura::AuraSculpt::ceiling) < -0.001 ? "   CEILING " + fmt (aura::AuraSculpt::ceiling) : juce::String()),
                    head.toNearestInt(), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (bar, 3.0f);
        auto xFor = [&] (double db) { return bar.getX() + bar.getWidth() * (float) ((std::clamp (db, -12.0, 12.0) + 12.0) / 24.0); };
        for (int db : { -12, -6, 0, 6, 12 }) tickLabel (xFor (db), (db > 0 ? "+" : "") + juce::String (db), true);
        const float x0 = xFor (0.0), x1 = xFor (shownGain);
        g.setColour (shownGain >= 0.0f ? accent : juce::Colour (0xffffb454));
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (x0, x1), bar.getY(), std::max (std::abs (x1 - x0), 1.5f), bar.getHeight()), 3.0f);
    }
    else
    {
        using T = aura::AuraTone;
        bigText ("TONE", 120.0f);
        g.drawText ("BODY " + fmt (T::body) + "   AIR " + fmt (T::air) + "   TILT " + fmt (T::tilt) + "   EVEN " + fmt (T::even) + "   ODD " + fmt (T::odd), head.toNearestInt(), juce::Justification::centredLeft);
        // Courbe schématique (formes lissées de même largeur que les filtres : cloche large 180 Hz, bascule 650 Hz, étagère 11 kHz).
        auto area = inner.withTrimmedTop (10.0f * pixelScale).withTrimmedBottom (4.0f * pixelScale);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (area, 3.0f);
        const double body = plain (T::body), air = plain (T::air), tilt = plain (T::tilt);
        auto yFor = [&] (double db) { return area.getCentreY() - area.getHeight() * 0.5f * (float) (std::clamp (db, -14.0, 14.0) / 14.0); };
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine ((int) yFor (0.0), area.getX(), area.getRight());
        juce::Path curve;
        for (int i = 0; i <= 160; ++i)
        {
            const double lf = std::log (20.0) + (std::log (20000.0) - std::log (20.0)) * i / 160.0, f = std::exp (lf);
            const double db = body * std::exp (-std::pow (std::log (f / 180.0) / 0.95, 2.0)) + 0.5 * tilt * std::tanh (1.1 * std::log (f / 650.0))
                              + air / (1.0 + std::exp (-std::log (f / 11000.0) / 0.45));
            const float x = area.getX() + area.getWidth() * (float) i / 160.0f, y = yFor (db);
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (accent);
        g.strokePath (curve, juce::PathStrokeType (2.0f * pixelScale + 0.5f));
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            const float x = area.getX() + area.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String (f, 0), (int) x + 3, (int) area.getBottom() - (int) (14 * pixelScale), 30, (int) (12 * pixelScale), juce::Justification::left);
        }
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
