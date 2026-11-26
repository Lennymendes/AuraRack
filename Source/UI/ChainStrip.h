// AuraRack — NextDSP · bande de chaîne (zone basse, style T-RackS)
//
// Dix tuiles alignées, dans l'ORDRE DE LA CHAÎNE AUDIO (gauche -> droite). Chaque tuile représente un slot :
//   clic            : sélectionne le module (il s'affiche en grand dans la zone haute) ;
//   glisser         : réordonne la chaîne en temps réel (aperçu animé ; un seul moveSlot() au relâchement) ;
//   pastille LED    : active / bypass (paramètre Bypass du slot, annulable, automatisable) ;
//   × (coin)        : retire le module ; tuile vide : « + » ouvre le menu d'ajout.
//
// Liaison à l'ordre du moteur sans data race (même contrat que l'ancienne grille) :
//   - un SEUL mot atomique de 64 bits est lu (getSlotOrderPacked), comparé à celui affiché, resynchronisé en animant ;
//   - pendant un glissement, rien n'est écrit ; au relâchement, un seul moveSlot() (CAS côté moteur).
// La sélection suit l'IDENTITÉ du slot (0 … 9), pas sa position : réordonner ne change pas le module affiché en haut.

#pragma once

#include "../PluginProcessor.h"
#include "NextLookAndFeel.h"

#include <functional>

namespace aura::ui
{
class ChainStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kGap = 8;

    explicit ChainStrip (AuraRackProcessor&);

    /** Appelé quand l'utilisateur choisit un slot (clic, ajout). */
    std::function<void (int slot)> onSelect;

    void setSelected (int slot);
    void setInfoMode (bool on);

    /** Une fois par image : resynchronise l'ordre, anime les tuiles, lit bypass / réduction de gain. */
    void tick (float dtSeconds);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;

    //==== Pour les tests =====================================================
    int getDisplayedPosition (int slot) const noexcept;
    juce::Rectangle<float> getTileBounds (int slot) const noexcept { return shown[(std::size_t) slot]; }
    juce::Rectangle<float> getRemoveRect (int slot) const noexcept;
    juce::Rectangle<float> getBypassRect (int slot) const noexcept;
    void showAddMenuForTesting (int slot, int moduleTypeToPick);   // applique le choix sans afficher le menu

private:
    enum class Hit { none, body, bypass, remove };

    juce::Rectangle<float> cellRect (int position) const;
    int nearestCell (juce::Point<float> p) const;
    int slotAt (juce::Point<float> p) const;
    Hit hitTest (int slot, juce::Point<float> p) const;
    void layoutTargets (bool animate);
    void paintTile (juce::Graphics&, int slot, juce::Rectangle<float> r, bool lifted) const;
    void openModuleMenu (int slot);
    void toggleBypass (int slot);
    void updateTooltip (juce::Point<float> p);

    AuraRackProcessor& processor;
    int selected = 0;
    bool infoMode = false;

    std::uint64_t shownPacked = 0;
    aura::SlotOrder displayOrder = aura::RackRouting::identity(), previewOrder = aura::RackRouting::identity();
    std::array<juce::Rectangle<float>, aura::kNumSlots> shown {}, target {};
    std::array<aura::ModuleType, aura::kNumSlots> types {};
    std::array<bool, aura::kNumSlots> bypassed {};
    std::array<float, aura::kNumSlots> reduction {};

    // Interaction.
    int pressedSlot = -1, dragFromPosition = 0, dropPosition = 0;
    Hit pressedHit = Hit::none;
    bool dragging = false;
    juce::Point<float> grabOffset;
};

} // namespace aura::ui
