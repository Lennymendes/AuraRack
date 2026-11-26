// AuraRack — NextDSP · barre globale : Input / Output / Master Mix, routage Sidechain, fréquence, latence, niveaux, CPU.

#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"
#include "SettingsPanel.h"

namespace aura::ui
{
/** Potentiomètre compact : bouton + nom + valeur sur deux lignes. */
class BarKnob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    BarKnob (juce::RangedAudioParameter&, const juce::String& name);
    void resized() override;

private:
    juce::Slider slider;
    juce::Label nameLabel, valueLabel;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

/** Bouton ⚙️ dessiné en vectoriel (aucune police d'icônes). */
class GearButton final : public juce::Button
{
public:
    GearButton() : juce::Button ("settings") {}
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (down ? colours::panelHigh : (highlighted ? colours::panelHigh.brighter (0.06f) : colours::panel));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colours::stroke);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        const auto c = r.getCentre();
        const float outer = r.getWidth() * 0.30f, inner = outer * 0.80f;
        juce::Path gear;
        constexpr int teeth = 8;
        for (int i = 0; i < teeth * 2; ++i)
        {
            const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) (teeth * 2) - 0.12f;
            const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (float) (teeth * 2) - 0.12f;
            const float rad = (i % 2 == 0) ? outer : inner;
            for (float a : { a0 + 0.06f, a1 - 0.06f })
            {
                const juce::Point<float> p (c.x + rad * std::sin (a), c.y - rad * std::cos (a));
                if (gear.isEmpty()) gear.startNewSubPath (p); else gear.lineTo (p);
            }
        }
        gear.closeSubPath();
        gear.addEllipse (c.x - outer * 0.38f, c.y - outer * 0.38f, outer * 0.76f, outer * 0.76f);
        gear.setUsingNonZeroWinding (false);
        g.setColour (highlighted ? colours::text : colours::textDim);
        g.fillPath (gear);
    }
};

/** Bouton plat de la barre d'outils : texte, état « actif » (accent), chevron optionnel (menu). */
class ToolButton final : public juce::Button
{
public:
    explicit ToolButton (const juce::String& name) : juce::Button (name) {}

    void setActive (bool a) { if (a != active) { active = a; repaint(); } }
    void setLabel (const juce::String& t) { if (t != label) { label = t; repaint(); } }
    void setChevron (bool c) { chevron = c; }
    void setAccent (juce::Colour c) { accent = c; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (active ? accent.withAlpha (0.20f) : (down ? colours::panelHigh : (highlighted ? colours::panelHigh.brighter (0.06f) : colours::panelHigh)));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (active ? accent : colours::stroke);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, active ? 1.4f : 1.0f);
        g.setColour (active ? colours::text : (highlighted ? colours::text : colours::textDim));
        g.setFont (NextLookAndFeel::font (12.0f, true));
        auto t = getLocalBounds().reduced (8, 0);
        if (chevron)
        {
            auto c = t.removeFromRight (14).toFloat();
            juce::Path p;
            p.addTriangle (c.getCentreX() - 4.0f, c.getCentreY() - 2.0f, c.getCentreX() + 4.0f, c.getCentreY() - 2.0f, c.getCentreX(), c.getCentreY() + 3.0f);
            g.fillPath (p);
        }
        g.drawText (label, t, chevron ? juce::Justification::centredLeft : juce::Justification::centred, true);
    }

private:
    juce::String label;
    bool active = false, chevron = false;
    juce::Colour accent { 0xff6aa6ff };
};

class TopBar final : public juce::Component
{
public:
    static constexpr int kRow1Height = 62, kHeight = 96;

    explicit TopBar (AuraRackProcessor&);

    /** Mode « infos » : info-bulles sur les éléments de la barre. */
    void setInfoMode (bool on);

    void tick (float dtSeconds);
    void refreshLanguage() noexcept { shownPreset = -2; }   // le libellé du preset suit la langue

    /** Pour les tests : ouvre / lit les contrôles de la deuxième rangée. */
    juce::PopupMenu buildPresetMenu() const;
    ToolButton& getAbButton (int which) { return which == 0 ? abA : abB; }
    ToolButton& getCopyButton() { return abCopy; }
    ToolButton& getPresetButton() { return presetButton; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AuraRackProcessor& processor;

    BarKnob inputKnob, outputKnob, mixKnob;
    GearButton gearButton;

    // Rangée 2 : presets, A / B, auto-gain du rack, utilitaire stéréo, latence totale.
    ToolButton presetButton { "preset" }, abA { "A" }, abB { "B" }, abCopy { "copy" };
    juce::ToggleButton rackAgcToggle { "AUTO GAIN" }, monoToggle { "MONO" };
    BarKnob widthKnob;
    juce::ComboBox bassBox;
    std::unique_ptr<juce::ButtonParameterAttachment> rackAgcAttachment, monoAttachment;
    std::unique_ptr<juce::ComboBoxParameterAttachment> bassAttachment;
    juce::Rectangle<int> row2, bassCaption, corrArea, latencyArea;
    int shownAb = -1, shownPreset = -2, presetClock = 0;
    bool shownModified = false;
    float correlation = 1.0f;
    int shownLatencyTotal = -1;
    void refreshToolTexts();
    juce::ComboBox sidechainBox;
    std::unique_ptr<juce::ComboBoxParameterAttachment> sidechainAttachment;

    LevelBallistics inL, inR, outL, outR;
    float cpu = 0.0f;
    double shownRate = 0.0;
    int shownLatency = -1;
    bool shownSidechain = false;

    juce::Rectangle<float> inBars, outBars, cpuBar;
    juce::Rectangle<int> infoArea, sidechainArea;
};

} // namespace aura::ui
