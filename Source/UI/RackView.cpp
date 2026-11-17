// AuraRack — NextDSP · grille des 10 slots avec glisser-déposer.

#include "RackView.h"

namespace aura::ui
{
RackView::RackView (AuraRackProcessor& p) : processor (p)
{
    for (int s = 0; s < aura::kNumSlots; ++s)
    {
        slots[(std::size_t) s] = std::make_unique<SlotComponent> (s, processor, static_cast<SlotDragHost&> (*this));
        addAndMakeVisible (*slots[(std::size_t) s]);
    }
    shownPacked = processor.getSlotOrderPacked();
    displayOrder = previewOrder = aura::RackRouting::unpack (shownPacked);
}

juce::Rectangle<float> RackView::cellRect (int position) const
{
    const auto area = getLocalBounds().toFloat();
    const float w = (area.getWidth()  - (float) (kGap * (kColumns - 1))) / (float) kColumns;
    const float h = (area.getHeight() - (float) (kGap * (kRows - 1))) / (float) kRows;
    const int col = position % kColumns, row = position / kColumns;
    return { area.getX() + (float) col * (w + (float) kGap), area.getY() + (float) row * (h + (float) kGap), w, h };
}

int RackView::nearestCell (juce::Point<float> p) const
{
    int best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < aura::kNumSlots; ++i)
    {
        const float d = cellRect (i).getCentre().getDistanceFrom (p);
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

int RackView::getDisplayedPosition (int slot) const noexcept
{
    const auto& o = dragging ? previewOrder : displayOrder;
    for (int i = 0; i < aura::kNumSlots; ++i)
        if (o[(std::size_t) i] == slot) return i;
    return 0;
}

void RackView::layoutTargets (bool animate)
{
    const auto& order = dragging ? previewOrder : displayOrder;
    for (int pos = 0; pos < aura::kNumSlots; ++pos)
    {
        const int s = order[(std::size_t) pos];
        target[(std::size_t) s] = cellRect (pos);
        slots[(std::size_t) s]->setPosition (pos);
        if (! animate)
        {
            shown[(std::size_t) s] = target[(std::size_t) s];
            slots[(std::size_t) s]->setBounds (shown[(std::size_t) s].toNearestInt());
        }
    }
}

void RackView::resized() { layoutTargets (false); }

//==============================================================================
void RackView::tick (float dt)
{
    // 1. Ordre du moteur : une lecture atomique, comparée à ce qui est affiché.
    const auto packed = processor.getSlotOrderPacked();
    if (! dragging && packed != shownPacked)
    {
        shownPacked = packed;
        displayOrder = aura::RackRouting::unpack (packed);
        layoutTargets (true);
    }

    // 2. Animation des cartes vers leur cellule (amortissement exponentiel, indépendant de la cadence d'affichage).
    const float k = 1.0f - std::exp (-dt * 18.0f);
    for (int s = 0; s < aura::kNumSlots; ++s)
    {
        if (dragging && s == dragSlot) continue;   // la carte saisie suit la souris
        auto& cur = shown[(std::size_t) s];
        const auto& tgt = target[(std::size_t) s];
        if (cur == tgt) continue;
        cur = { cur.getX() + (tgt.getX() - cur.getX()) * k, cur.getY() + (tgt.getY() - cur.getY()) * k,
                cur.getWidth() + (tgt.getWidth() - cur.getWidth()) * k, cur.getHeight() + (tgt.getHeight() - cur.getHeight()) * k };
        if (std::abs (cur.getX() - tgt.getX()) < 0.3f && std::abs (cur.getY() - tgt.getY()) < 0.3f) cur = tgt;
        const auto bounds = cur.toNearestInt();
        if (bounds != slots[(std::size_t) s]->getBounds())
            slots[(std::size_t) s]->setBounds (bounds);
    }

    // 3. Les slots lisent leurs indicateurs.
    for (auto& s : slots) s->tick (dt);
}

//==============================================================================
void RackView::slotDragBegan (SlotComponent& s, const juce::MouseEvent& e)
{
    dragging = true;
    dragSlot = s.getSlot();
    dragFromPosition = getDisplayedPosition (dragSlot);
    dropPosition = dragFromPosition;
    previewOrder = displayOrder;
    grabOffset = e.getEventRelativeTo (this).position - s.getPosition().toFloat();
    s.toFront (false);
    s.setLifted (true);
}

void RackView::slotDragged (SlotComponent& s, const juce::MouseEvent& e)
{
    auto topLeft = e.getEventRelativeTo (this).position - grabOffset;
    const float w = shown[(std::size_t) dragSlot].getWidth(), h = shown[(std::size_t) dragSlot].getHeight();
    topLeft.x = juce::jlimit (-10.0f, (float) getWidth()  - w + 10.0f, topLeft.x);
    topLeft.y = juce::jlimit (-10.0f, (float) getHeight() - h + 10.0f, topLeft.y);
    shown[(std::size_t) dragSlot] = juce::Rectangle<float> (topLeft.x, topLeft.y, w, h);
    s.setBounds (shown[(std::size_t) dragSlot].toNearestInt());

    const int drop = nearestCell (shown[(std::size_t) dragSlot].getCentre());
    if (drop != dropPosition)
    {
        dropPosition = drop;
        previewOrder = aura::RackRouting::moved (displayOrder, dragFromPosition, dropPosition);
        layoutTargets (true);
    }
}

void RackView::slotDragEnded (SlotComponent& s, const juce::MouseEvent&)
{
    s.setLifted (false);
    const bool changed = dropPosition != dragFromPosition;
    if (changed)
    {
        processor.moveSlot (dragFromPosition, dropPosition);   // seule écriture de tout le glissement
        displayOrder = previewOrder;
        shownPacked = aura::RackRouting::pack (displayOrder);  // l'écart éventuel avec le moteur sera repris au tick suivant
    }
    dragging = false;
    dragSlot = -1;
    layoutTargets (true);
}

} // namespace aura::ui
