// AuraRack — NextDSP · module de mesure LUFS

#include "LoudnessBar.h"
#include "I18n.h"

namespace aura::ui
{
using namespace colours;

namespace
{
constexpr float kBarMin = -40.0f, kBarMax = 0.0f;
juce::String lufsText (float v) { return v <= -119.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94\xe2\x80\x94")) : juce::String (v, 1); }
}

LoudnessBar::LoudnessBar (AuraRackProcessor& p) : processor (p)
{
    resetButton.setButtonText (i18n::tr (i18n::Key::lufsReset));
    resetButton.onClick = [this] { processor.resetLoudness(); };
    addAndMakeVisible (resetButton);
}

void LoudnessBar::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    resetButton.setBounds (r.removeFromRight (120).withSizeKeepingCentre (120, 28));
    r.removeFromRight (16);
    barArea = r.removeFromRight (260).toFloat().withSizeKeepingCentre (260.0f, 10.0f);
}

void LoudnessBar::tick (float dt)
{
    const auto& l = processor.getLoudness();
    const float m = l.momentary.load (std::memory_order_relaxed), s = l.shortTerm.load (std::memory_order_relaxed),
                i = l.integrated.load (std::memory_order_relaxed), r = l.range.load (std::memory_order_relaxed),
                pk = l.peakDb.load (std::memory_order_relaxed);
    const float newBar = bar + (std::max (s, kBarMin) - bar) * (1.0f - std::exp (-dt * 12.0f));
    const bool changed = std::abs (m - momentary) > 0.05f || std::abs (s - shortTerm) > 0.05f || std::abs (i - integrated) > 0.05f
                      || std::abs (r - range) > 0.05f || std::abs (pk - peak) > 0.05f || std::abs (newBar - bar) > 0.05f;
    momentary = m; shortTerm = s; integrated = i; range = r; peak = pk; bar = newBar;
    if (resetButton.getButtonText() != i18n::tr (i18n::Key::lufsReset)) resetButton.setButtonText (i18n::tr (i18n::Key::lufsReset));
    if (changed) repaint();
}

void LoudnessBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r.reduced (0.5f), 8.0f);
    g.setColour (stroke);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    struct Cell { i18n::Key key; juce::String value; juce::String unit; juce::Colour colour; };
    const Cell cells[] = {
        { i18n::Key::momentary,  lufsText (momentary),  "LUFS", text },
        { i18n::Key::shortTerm,  lufsText (shortTerm),  "LUFS", text },
        { i18n::Key::integrated, lufsText (integrated), "LUFS", levelOk },
        { i18n::Key::range,      range > 0.0f ? juce::String (range, 1) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94\xe2\x80\x94")), "LU", text },
        { i18n::Key::peak,       lufsText (peak),       "dBFS", peak > -1.0f && peak > -119.0f ? levelOver : text },
    };
    auto area = getLocalBounds().reduced (14, 6).withWidth (getWidth() - 14 - 120 - 16 - 260 - 28);
    const int cw = area.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
    {
        auto c = area.withX (area.getX() + i * cw).withWidth (cw);
        g.setColour (textDim);
        g.setFont (NextLookAndFeel::font (9.5f, true));
        g.drawText (i18n::tr (cells[i].key) + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + cells[i].unit, c.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (cells[i].colour);
        g.setFont (NextLookAndFeel::font (22.0f, true));
        g.drawText (cells[i].value, c, juce::Justification::centredLeft);
    }

    // Barre de sonie court terme.
    auto pos = [this] (float v) { return barArea.getX() + barArea.getWidth() * juce::jlimit (0.0f, 1.0f, (v - kBarMin) / (kBarMax - kBarMin)); };
    g.setColour (panelHigh);
    g.fillRoundedRectangle (barArea, 5.0f);
    g.setColour (bar > -14.0f + 0.5f ? levelHot : levelOk);
    g.fillRoundedRectangle (barArea.withRight (std::max (barArea.getX() + 2.0f, pos (bar))), 5.0f);
    g.setColour (text.withAlpha (0.8f));
    for (float ref : { -23.0f, -14.0f })
    {
        g.drawVerticalLine ((int) pos (ref), barArea.getY() - 4.0f, barArea.getBottom() + 4.0f);
        g.setColour (textDim);
        g.setFont (NextLookAndFeel::font (9.0f, true));
        g.drawText (juce::String ((int) ref), (int) pos (ref) - 16, (int) barArea.getBottom() + 4, 32, 11, juce::Justification::centred);
        g.setColour (text.withAlpha (0.8f));
    }
}

} // namespace aura::ui
