// AuraRack — NextDSP · façade du E9DS (de-esser)
//
// Plaque graphite, affichage de la bande sibilante : courbe de réponse VIVANTE (cloche dynamique = ce que le module fait
// réellement au spectre), courbe fantôme à pleine réduction (Range), barre de niveau de la bande face au seuil, valeur de GR.
// Contrôles : Threshold, Frequency, Width, Range, Release = p1…p5 ; MODE (Split / Wide), LISTEN (Off / On) = p6, p7 ;
// ACTIVE / BYPASS = le Bypass du slot. Deux mises en page : large (1000 × 385) et compacte (240 × 262).

#pragma once

#include "Faceplate.h"

namespace aura::ui
{
class E9dsFaceplate final : public Faceplate
{
public:
    E9dsFaceplate (juce::AudioProcessorValueTreeState&, int slotIndex);

    void tick (float dtSeconds, const FaceplateFeed&) override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWideWidth = 1000, kWideHeight = 385, kCompactWidth = 240, kCompactHeight = 262;

private:
    bool isWide() const noexcept { return (float) getWidth() >= 1.6f * (float) getHeight(); }
    double plain (int param) const;
    void paintDisplay (juce::Graphics&) const;

    juce::AudioProcessorValueTreeState& apvts;
    const int slot;
    std::array<PlateKnob, 5> knobs;
    PlateSwitch modeSwitch, listenSwitch, bypassSwitch;
    std::unique_ptr<juce::ButtonParameterAttachment> modeAttachment, listenAttachment, bypassAttachment;

    float pixelScale = 1.0f, shownReduction = 0.0f, bandDb = -120.0f;
    bool bypassed = false;
    double lastThreshold = 0.0, lastFreq = 0.0, lastWidth = 0.0, lastRange = 0.0;
    juce::Rectangle<float> plate, display;
    juce::Image brushed;
};

} // namespace aura::ui
