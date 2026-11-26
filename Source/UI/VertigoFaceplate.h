// AuraRack — NextDSP · façades du V9C3 (compresseur VCA) et du V9P2 (préampli double canal)
//
// Une seule classe, deux modules : plaque, affichage et contrôles décrits par une table.
//  V9C3 : bargraphe de réduction (zone fine jusqu'à 6 dB, puis jusqu'à 24 dB), seuil effectif en dBu (Auto Focus),
//         potards Threshold, Ratio, Attack, Release, Make-Up, SC HPF, Mode, Focus ; interrupteurs Detect, Auto Focus, Bypass.
//  V9P2 : barre de niveau d'entrée en dBu, voyant Signal (vert) / Clip (rouge dès +19 dBu, plafond +24 dBu) ;
//         potards Gain, Fine, HPF, Phase, R Offset ; interrupteurs Pad, Input, Link, Bypass.
// Mise en page large uniquement (1000 × 385) : c'est la seule utilisée par l'inspecteur.

#pragma once

#include "Faceplate.h"

namespace aura::ui
{
class VertigoFaceplate final : public Faceplate
{
public:
    VertigoFaceplate (aura::ModuleType, juce::AudioProcessorValueTreeState&, int slotIndex);

    void tick (float dtSeconds, const FaceplateFeed&) override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWideWidth = 1000, kWideHeight = 385;

private:
    struct SwitchDef { int param; const char* off; const char* on; bool alert; };

    void paintDisplay (juce::Graphics&) const;
    double plain (int param) const;

    const aura::ModuleType type;
    juce::AudioProcessorValueTreeState& apvts;
    const int slot;
    std::vector<std::unique_ptr<PlateKnob>> knobs;
    std::vector<std::unique_ptr<PlateSwitch>> switches;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> attachments;
    PlateSwitch* bypassSwitch = nullptr;

    float pixelScale = 1.0f, shownReduction = 0.0f, level = -120.0f, lamp = 0.0f;
    bool bypassed = false;
    double lastThreshold = 0.0, lastFocus = 0.0;
    bool autoFocus = false;
    juce::Rectangle<float> plate, display;
    juce::Image brushed;
};

} // namespace aura::ui
