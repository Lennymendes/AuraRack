// AuraRack — NextDSP · façade du W9BMK1
//
// Entièrement vectorielle (Path / Graphics), aucune image : métal anodisé bleu profond brossé, potards en aluminium
// usiné avec anneau de LED ambre, VU-mètre rétro-éclairé de réduction de gain, voyant de crête, un seul interrupteur.
//
// Chaque contrôle est lié à un vrai paramètre : Input / Threshold / Ratio / Attack / Release / Output = paramètres
// p1…p6 du slot ; l'interrupteur ACTIVE / BYPASS = le Bypass du slot. Le VU lit la réduction de gain du module,
// le voyant sa crête de sortie. Aucun élément décoratif actif.
//
// Deux mises en page, choisies selon le rapport largeur / hauteur : large (façade de rack, 1000 × 385) et compacte
// (carte de slot, 240 × 262).

#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"

namespace aura::ui
{
/** Potard en aluminium usiné : anneau de LED ambre (valeur), capuchon brossé, repère. Nom au-dessus, valeur dessous. */
class W9bKnob final : public juce::Slider
{
public:
    W9bKnob();
    void bind (juce::RangedAudioParameter&);
    void setName (const juce::String& n) { name = n.toUpperCase(); repaint(); }
    void paint (juce::Graphics&) override;

private:
    juce::String name;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

/** Interrupteur à glissière ACTIVE / BYPASS (état = paramètre Bypass du slot). */
class W9bSwitch final : public juce::ToggleButton
{
public:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class W9bFaceplate final : public juce::Component
{
public:
    W9bFaceplate (juce::AudioProcessorValueTreeState&, int slotIndex);

    /** Une fois par image : réduction de gain (dB, >= 0) et crête de sortie (linéaire, 0 = rien depuis la dernière image). */
    void tick (float dtSeconds, float reductionDb, float peakLinear);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWideWidth = 1000, kWideHeight = 385, kCompactWidth = 240, kCompactHeight = 262;

private:
    bool isWide() const noexcept { return (float) getWidth() >= 1.6f * (float) getHeight(); }
    void drawPlate (juce::Graphics&) const;
    void drawScrews (juce::Graphics&) const;
    void drawVu (juce::Graphics&) const;
    void drawLamp (juce::Graphics&) const;
    void drawLogo (juce::Graphics&) const;
    static double needlePosition (double reductionDb) noexcept;   // 0 (0 dB) … 1 (20 dB), échelle de lecture type LA-2A

    std::array<W9bKnob, 6> knobs;
    W9bSwitch bypassSwitch;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;

    // Géométrie (pixels de la façade), recalculée dans resized().
    float scale = 1.0f;
    juce::Rectangle<float> plate, vuFrame, vuWindow, lampArea, logoArea;
    juce::Point<float> pivot;
    float needleLength = 0.0f, arcRadius = 0.0f;

    // Indicateurs : aiguille à ressort amorti (masse-ressort, ~4 Hz), voyant à retombée douce.
    double needle = 0.0, needleVelocity = 0.0, needleTarget = 0.0;
    float lamp = 0.0f;
    bool bypassed = false;

    juce::Image brushed;   // texture de la plaque, régénérée au redimensionnement
    juce::Font logoFont { juce::FontOptions() };
};

} // namespace aura::ui
