// AuraRack — NextDSP · module de mesure LUFS (mode mastering)
//
// Momentané / Court terme / Intégré / LRA / Crête (BS.1770-4, EBU R128), barre de sonie court terme avec repères -23 et -14 LUFS.
// Affichée seulement quand la mesure est activée dans le menu ⚙️ (désactivée : aucun calcul côté audio, aucune place prise).

#pragma once

#include "../PluginProcessor.h"
#include "NextLookAndFeel.h"

namespace aura::ui
{
class LoudnessBar final : public juce::Component
{
public:
    static constexpr int kHeight = 58;

    explicit LoudnessBar (AuraRackProcessor&);

    void tick (float dtSeconds);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AuraRackProcessor& processor;
    juce::TextButton resetButton;
    float momentary = -120.0f, shortTerm = -120.0f, integrated = -120.0f, range = 0.0f, peak = -120.0f, bar = -120.0f;
    juce::Rectangle<float> barArea;
};

} // namespace aura::ui
