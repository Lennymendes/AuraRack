// AuraRack — NextDSP · façade du W9BMK1
//
// Métal anodisé bleu profond brossé, potards en aluminium usiné à anneau de LED ambre, VU-mètre rétro-éclairé de
// réduction de gain, voyant de crête, AUTO FOCUS (MAN / AUTO + cible FOCUS) et un seul interrupteur ACTIVE / BYPASS.
//
// Chaque contrôle est un vrai paramètre : Input, Thresh, Ratio, Attack, Release, Output, Focus, Auto Focus = p1…p8 du slot,
// ACTIVE / BYPASS = le Bypass du slot. En mode AUTO : le potard THRESH n'est plus réglable et affiche le seuil effectif
// calculé par le moteur (le réglage manuel est conservé et revient en MAN) ; FOCUS s'allume (estompé en MAN) et son repère
// apparaît sur le VU-mètre : l'aiguille converge vers lui.
//
// Deux mises en page : large (façade de rack, 1000 × 385) et compacte (carte de slot, 240 × 262).

#pragma once

#include "Faceplate.h"

namespace aura::ui
{
class W9bFaceplate final : public Faceplate
{
public:
    W9bFaceplate (juce::AudioProcessorValueTreeState&, int slotIndex);

    void tick (float dtSeconds, const FaceplateFeed&) override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWideWidth = 1000, kWideHeight = 385, kCompactWidth = 240, kCompactHeight = 262;

private:
    enum Cell { cInput, cThresh, cRatio, cFocus, cAttack, cRelease, cOutput, numCells };

    bool isWide() const noexcept { return (float) getWidth() >= 1.6f * (float) getHeight(); }
    void applyModeLabels();
    static double reductionPosition (double db) noexcept;   // 0 dB -> 0, 20 dB -> 1 (échelle dilatée près de 0)

    std::array<PlateKnob, numCells> knobs;
    PlateSwitch autoSwitch, bypassSwitch;
    std::unique_ptr<juce::ButtonParameterAttachment> autoAttachment, bypassAttachment;

    DialScale scale;
    VuDial vu;
    float pixelScale = 1.0f, lamp = 0.0f;
    bool bypassed = false, autoOn = false;
    juce::Rectangle<float> plate, logoArea, lampArea, autoLabelArea;
    juce::Image brushed;
    juce::Font logoFont { juce::FontOptions() };
};

} // namespace aura::ui
