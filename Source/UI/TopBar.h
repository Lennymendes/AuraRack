// AuraRack — NextDSP · barre globale : Input / Output / Master Mix, routage Sidechain, fréquence, latence, niveaux, CPU.

#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"
#include "SettingsPanel.h"

namespace aura::ui
{
/** Potentiomètre compact : bouton + nom + valeur sur deux lignes. */
class BarKnob final : public juce::Component
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

class TopBar final : public juce::Component
{
public:
    explicit TopBar (AuraRackProcessor&);

    /** Mode « infos » : info-bulles sur les éléments de la barre. */
    void setInfoMode (bool on);

    void tick (float dtSeconds);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AuraRackProcessor& processor;

    BarKnob inputKnob, outputKnob, mixKnob;
    GearButton gearButton;
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
