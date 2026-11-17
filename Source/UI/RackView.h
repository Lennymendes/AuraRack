// AuraRack — NextDSP · grille des 10 slots avec glisser-déposer.
//
// Liaison à l'ordre du moteur sans data race :
//   - la vue ne lit l'ordre qu'au travers d'UN mot atomique de 64 bits (getSlotOrderPacked) : toujours cohérent ;
//   - elle compare ce mot à celui qu'elle affiche ; s'il diffère (restauration d'état, annulation…) elle se resynchronise
//     en animant, sans verrou ;
//   - pendant un glissement, rien n'est écrit : l'aperçu est purement visuel. Au relâchement, un seul appel
//     moveSlot() (compare-and-swap côté moteur) ; le thread audio n'est jamais bloqué.

#pragma once

#include "SlotComponent.h"

namespace aura::ui
{
class RackView final : public juce::Component, private SlotDragHost
{
public:
    static constexpr int kColumns = 5, kRows = 2, kGap = 10;

    explicit RackView (AuraRackProcessor&);

    /** Appelé à chaque image (VBlank) : synchronise l'ordre, anime les cartes, met à jour les slots. */
    void tick (float dtSeconds);

    void resized() override;

    /** Pour les tests : position (0 … 9) affichée d'un slot. */
    int getDisplayedPosition (int slot) const noexcept;
    SlotComponent& getSlotComponent (int slot) noexcept { return *slots[(std::size_t) slot]; }

private:
    juce::Rectangle<float> cellRect (int position) const;
    int nearestCell (juce::Point<float> p) const;
    void layoutTargets (bool animate);

    void slotDragBegan (SlotComponent&, const juce::MouseEvent&) override;
    void slotDragged (SlotComponent&, const juce::MouseEvent&) override;
    void slotDragEnded (SlotComponent&, const juce::MouseEvent&) override;

    AuraRackProcessor& processor;
    std::array<std::unique_ptr<SlotComponent>, aura::kNumSlots> slots;

    std::uint64_t shownPacked = 0;
    aura::SlotOrder displayOrder = aura::RackRouting::identity();
    aura::SlotOrder previewOrder = aura::RackRouting::identity();
    std::array<juce::Rectangle<float>, aura::kNumSlots> shown {}, target {};

    bool dragging = false;
    int dragSlot = -1, dragFromPosition = 0, dropPosition = 0;
    juce::Point<float> grabOffset;
};

} // namespace aura::ui
