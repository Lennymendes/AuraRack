// AuraRack — NextDSP · façade du L9A (amplificateur de nivellement opto, esprit LA-2A)
//
// Panneau argent brossé, VU-mètre ambre rétro-éclairé au centre, GAIN à gauche, PEAK REDUCTION à droite, trois
// interrupteurs à glissière : COMP / LIMIT, METER GR / OUT et ACTIVE / BYPASS.
//
// Chaque contrôle est un vrai paramètre : Peak Reduction, Gain, Mode, Meter = p1…p4 du slot ; ACTIVE / BYPASS = le Bypass du
// slot. Le VU affiche, selon METER, soit la réduction de gain (0 à droite), soit le niveau de sortie à balistique VU publié
// par le module (échelle VU, 0 VU = -18 dBFS RMS).
//
// Deux mises en page : large (1000 × 385) et compacte (240 × 262).

#pragma once

#include "Faceplate.h"

namespace aura::ui
{
class L9aFaceplate final : public Faceplate
{
public:
    L9aFaceplate (juce::AudioProcessorValueTreeState&, int slotIndex);

    void tick (float dtSeconds, const FaceplateFeed&) override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWideWidth = 1000, kWideHeight = 385, kCompactWidth = 240, kCompactHeight = 262;
    static constexpr double kVuReferenceDbfs = -18.0;   // 0 VU

private:
    bool isWide() const noexcept { return (float) getWidth() >= 1.6f * (float) getHeight(); }

    PlateKnob gainKnob, peakKnob;
    PlateSwitch modeSwitch, meterSwitch, bypassSwitch;
    std::unique_ptr<juce::ButtonParameterAttachment> modeAttachment, meterAttachment, bypassAttachment;

    DialScale grScale, vuScale;
    VuDial vu;
    float pixelScale = 1.0f;
    bool bypassed = false, showOutput = false;
    juce::Rectangle<float> plate, logoArea;
    juce::Image brushed;
};

} // namespace aura::ui
