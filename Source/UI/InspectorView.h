// AuraRack — NextDSP · zone haute (« inspection ») : le module sélectionné, en grand
//
// Dix cartes SlotComponent (une par slot, liées à l'identité du slot), une seule visible à la fois, en mode inspection :
// les modules à façade (W9BMK1, L9A, E9DS) y occupent toute la zone, les autres affichent de grands potentiomètres homogènes.
// Réordonner la chaîne ne change pas la carte affichée : la sélection porte sur l'identité du slot.

#pragma once

#include "SlotComponent.h"

namespace aura::ui
{
class InspectorView final : public juce::Component
{
public:
    explicit InspectorView (AuraRackProcessor& p)
    {
        for (int s = 0; s < aura::kNumSlots; ++s)
        {
            cards[(std::size_t) s] = std::make_unique<SlotComponent> (s, p, noDrag);
            cards[(std::size_t) s]->setInspectorMode (true);
            addChildComponent (*cards[(std::size_t) s]);
        }
        setSelected (0);
    }

    void setSelected (int slot)
    {
        selected = juce::jlimit (0, aura::kNumSlots - 1, slot);
        for (int s = 0; s < aura::kNumSlots; ++s)
            cards[(std::size_t) s]->setVisible (s == selected);
    }

    int getSelected() const noexcept { return selected; }
    SlotComponent& getCard (int slot) noexcept { return *cards[(std::size_t) slot]; }

    void tick (float dt) { cards[(std::size_t) selected]->tick (dt); }   // seule la carte visible lit les indicateurs

    void resized() override
    {
        for (auto& c : cards) c->setBounds (getLocalBounds());
    }

private:
    struct NoDrag final : SlotDragHost
    {
        void slotDragBegan (SlotComponent&, const juce::MouseEvent&) override {}
        void slotDragged (SlotComponent&, const juce::MouseEvent&) override {}
        void slotDragEnded (SlotComponent&, const juce::MouseEvent&) override {}
    } noDrag;

    std::array<std::unique_ptr<SlotComponent>, aura::kNumSlots> cards;
    int selected = 0;
};

} // namespace aura::ui
