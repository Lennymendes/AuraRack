// AuraRack — NextDSP · kit commun des façades de module (W9BMK1, L9A, et les suivantes)
//
// Tout est vectoriel (Path / Graphics) : une façade ne contient que des éléments LIÉS À UN PARAMÈTRE RÉEL
// (PlateKnob, PlateSwitch) ou des indicateurs alimentés par le moteur (VuDial, PlateLamp). Rien de décoratif n'est actif.
// Ajouter un module = un thème (PlateTheme), une mise en page, et une ligne dans Faceplate::create().

#pragma once

#include "../PluginProcessor.h"
#include "NextLookAndFeel.h"

#include <optional>

namespace aura::ui
{
/** Matériaux et couleurs d'une façade. */
struct PlateTheme
{
    juce::Colour top, middle, bottom;        // dégradé de la plaque
    juce::Colour engrave;                    // textes gravés
    juce::Colour dot;                        // trame de points autour du VU-mètre
    juce::Colour led { 0xffffc531 };         // LED allumée / affichages de valeur
    juce::Colour ledDim { 0xff3a3018 };
    juce::Colour alert { 0xffff7a5c };       // état « bypass »
    juce::Colour trackFill { 0xff04122e };   // fond des interrupteurs à glissière
    bool darkCap = false;                    // capuchon des potards : noir (true) ou aluminium brossé (false)
    float grain = 1.0f;                      // intensité du brossage
};

const PlateTheme& blueAnodisedTheme();   // W9BMK1
const PlateTheme& silverPanelTheme();    // L9A

/** Police réduite pour que `text` tienne dans `maxWidth` (inchangée si elle tient déjà). */
inline juce::Font fitFont (juce::Font f, const juce::String& text, float maxWidth)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (f, text, 0.0f, 0.0f);
    const float w = glyphs.getBoundingBox (0, -1, true).getWidth();
    return w > maxWidth && w > 0.0f ? f.withHeight (std::max (5.0f, f.getHeight() * maxWidth / w)) : f;
}

//==============================================================================
/** Potard usiné : anneau de LED (valeur), capuchon, repère. Nom au-dessus, valeur dessous. */
class PlateKnob final : public juce::Slider
{
public:
    explicit PlateKnob (const PlateTheme&);
    void bind (juce::RangedAudioParameter&);
    void setName (const juce::String& n) { name = n.toUpperCase(); repaint(); }

    /** Affiche une valeur imposée par le moteur (seuil effectif de l'Auto Focus) à la place du paramètre. */
    void setLive (std::optional<double> normalised, const juce::String& text);
    /** Potard sans effet dans l'état courant du module : estompé (reste réglable). */
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }

    void paint (juce::Graphics&) override;

private:
    const PlateTheme& theme;
    juce::String name, liveText;
    std::optional<double> live;
    bool dimmed = false;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

/** Interrupteur à glissière à deux états nommés (état = paramètre lié). */
class PlateSwitch final : public juce::ToggleButton
{
public:
    PlateSwitch (const PlateTheme&, juce::String offLabel, juce::String onLabel, bool onIsAlert);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void setLabels (juce::String off, juce::String on) { offText = std::move (off); onText = std::move (on); repaint(); }

private:
    const PlateTheme& theme;
    juce::String offText, onText;
    bool alertOnState;   // true : l'état « on » est un état d'exception (BYPASS) -> couleur d'alerte
};

//==============================================================================
struct DialTick { double value; juce::String label; bool major; bool warm; };

/** Échelle d'un cadran : valeur -> position [0, 1] sur l'arc, graduations, légende. */
struct DialScale
{
    std::function<double (double)> position;
    std::vector<DialTick> ticks;
    juce::String caption;
    bool reversed = false;   // true : 0 à droite, l'aiguille part vers la gauche (réduction de gain)
};

/** VU-mètre rétro-éclairé : cadran, aiguille à ressort amorti, vitre, repère de cible optionnel. */
class VuDial
{
public:
    void setGeometry (juce::Rectangle<float> frame, float pixelScale);
    void setScale (const DialScale* s) { scale = s; }
    void setMarker (std::optional<double> value) { marker = value; }
    /** Fait avancer l'aiguille ; renvoie true si elle a bougé (il faut repeindre la fenêtre). */
    bool advance (float dtSeconds, double value);
    void snap (double value);

    void paintFace (juce::Graphics&) const;
    void paintGlass (juce::Graphics&) const;
    juce::Rectangle<int> getWindow() const { return window.getSmallestIntegerContainer(); }
    juce::Rectangle<float> getFrame() const { return frame; }

private:
    float angleFor (double pos) const noexcept;
    juce::Point<float> polarFromPivot (float r, float angle) const noexcept { return { pivot.x + r * std::sin (angle), pivot.y - r * std::cos (angle) }; }

    const DialScale* scale = nullptr;
    std::optional<double> marker;
    juce::Rectangle<float> frame, window;
    juce::Point<float> pivot;
    float arcRadius = 0.0f, px = 1.0f;
    double needle = 0.0, velocity = 0.0;
    static constexpr float kSpanDeg = 31.0f;
};

//==============================================================================
// Éléments partagés de la plaque.
juce::Image makeBrushedPlate (int w, int h, const PlateTheme&, juce::Point<float> dotCentre, float dotReach, float pitch);
void drawPlate (juce::Graphics&, juce::Rectangle<float> r, const PlateTheme&, const juce::Image& brushed, float pixelScale);
void drawScrews (juce::Graphics&, juce::Rectangle<float> r, float pixelScale);
void drawRedLamp (juce::Graphics&, juce::Rectangle<float> area, float brightness);

} // namespace aura::ui
