// AuraRack — NextDSP · barre globale : Input / Output / Master Mix, routage Sidechain, fréquence, latence, niveaux, CPU.

#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"

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

class TopBar final : public juce::Component
{
public:
    explicit TopBar (AuraRackProcessor&);

    void tick (float dtSeconds);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AuraRackProcessor& processor;

    BarKnob inputKnob, outputKnob, mixKnob;
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
