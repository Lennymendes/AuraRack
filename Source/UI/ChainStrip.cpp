// AuraRack — NextDSP · bande de chaîne (zone basse)

#include "ChainStrip.h"
#include "I18n.h"

namespace aura::ui
{
using namespace colours;

namespace
{
constexpr int kRemoveAction = 100, kBypassAction = 101;
bool hasGainReduction (aura::ModuleType t)
{
    return t == aura::ModuleType::comp || t == aura::ModuleType::duck || t == aura::ModuleType::limit
        || t == aura::ModuleType::w9b || t == aura::ModuleType::l9a || t == aura::ModuleType::e9ds;
}
}

ChainStrip::ChainStrip (AuraRackProcessor& p) : processor (p)
{
    shownPacked = processor.getSlotOrderPacked();
    displayOrder = previewOrder = aura::RackRouting::unpack (shownPacked);
    for (int s = 0; s < aura::kNumSlots; ++s) types[(std::size_t) s] = processor.getSlotModule (s);
}

//==============================================================================
juce::Rectangle<float> ChainStrip::cellRect (int position) const
{
    const auto area = getLocalBounds().toFloat();
    const float w = (area.getWidth() - (float) (kGap * (aura::kNumSlots - 1))) / (float) aura::kNumSlots;
    return { area.getX() + (float) position * (w + (float) kGap), area.getY(), w, area.getHeight() };
}

int ChainStrip::nearestCell (juce::Point<float> p) const
{
    int best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < aura::kNumSlots; ++i)
    {
        const float d = std::abs (cellRect (i).getCentreX() - p.x);
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

int ChainStrip::getDisplayedPosition (int slot) const noexcept
{
    const auto& o = dragging ? previewOrder : displayOrder;
    for (int i = 0; i < aura::kNumSlots; ++i)
        if (o[(std::size_t) i] == slot) return i;
    return 0;
}

juce::Rectangle<float> ChainStrip::getRemoveRect (int slot) const noexcept
{
    const auto r = shown[(std::size_t) slot];
    return { r.getRight() - 26.0f, r.getY() + 6.0f, 20.0f, 20.0f };
}

juce::Rectangle<float> ChainStrip::getBypassRect (int slot) const noexcept
{
    const auto r = shown[(std::size_t) slot];
    return { r.getX() + 8.0f, r.getBottom() - 30.0f, 26.0f, 24.0f };
}

int ChainStrip::slotAt (juce::Point<float> p) const
{
    int found = -1;
    for (int s = 0; s < aura::kNumSlots; ++s)
        if (shown[(std::size_t) s].contains (p) && (found < 0 || (dragging && s == pressedSlot))) found = s;
    return found;
}

ChainStrip::Hit ChainStrip::hitTest (int slot, juce::Point<float> p) const
{
    if (slot < 0) return Hit::none;
    if (types[(std::size_t) slot] != aura::ModuleType::none)
    {
        if (getRemoveRect (slot).contains (p)) return Hit::remove;
        if (getBypassRect (slot).contains (p)) return Hit::bypass;
    }
    return Hit::body;
}

void ChainStrip::layoutTargets (bool animate)
{
    const auto& order = dragging ? previewOrder : displayOrder;
    for (int pos = 0; pos < aura::kNumSlots; ++pos)
    {
        const int s = order[(std::size_t) pos];
        target[(std::size_t) s] = cellRect (pos);
        if (! animate || shown[(std::size_t) s].isEmpty()) shown[(std::size_t) s] = target[(std::size_t) s];
    }
}

void ChainStrip::resized() { layoutTargets (false); }

void ChainStrip::setSelected (int slot)
{
    slot = juce::jlimit (0, aura::kNumSlots - 1, slot);
    if (slot == selected) return;
    selected = slot;
    repaint();
}

void ChainStrip::setInfoMode (bool on)
{
    if (on == infoMode) return;
    infoMode = on;
    if (! on) setTooltip ({});
    repaint();
}

//==============================================================================
void ChainStrip::tick (float dt)
{
    bool dirty = false;

    const auto packed = processor.getSlotOrderPacked();   // UNE lecture atomique
    if (! dragging && packed != shownPacked)
    {
        shownPacked = packed;
        displayOrder = aura::RackRouting::unpack (packed);
        layoutTargets (true);
    }

    const float k = 1.0f - std::exp (-dt * 18.0f);
    for (int s = 0; s < aura::kNumSlots; ++s)
    {
        if (dragging && s == pressedSlot) continue;   // la tuile saisie suit la souris
        auto& cur = shown[(std::size_t) s];
        const auto& tgt = target[(std::size_t) s];
        if (cur == tgt) continue;
        cur = { cur.getX() + (tgt.getX() - cur.getX()) * k, cur.getY() + (tgt.getY() - cur.getY()) * k,
                cur.getWidth() + (tgt.getWidth() - cur.getWidth()) * k, cur.getHeight() + (tgt.getHeight() - cur.getHeight()) * k };
        if (std::abs (cur.getX() - tgt.getX()) < 0.3f && std::abs (cur.getY() - tgt.getY()) < 0.3f) cur = tgt;
        dirty = true;
    }

    const auto& m = processor.getMeters();
    auto& apvts = processor.getValueTreeState();
    for (int s = 0; s < aura::kNumSlots; ++s)
    {
        const auto t = processor.getSlotModule (s);
        const bool byp = apvts.getRawParameterValue (aura::params::bypass (s))->load() > 0.5f;
        float gr = hasGainReduction (t) && ! byp ? m.gainReductionDb[(std::size_t) s].load (std::memory_order_relaxed) : 0.0f;
        gr = reduction[(std::size_t) s] + (gr - reduction[(std::size_t) s]) * (1.0f - std::exp (-dt * 25.0f));
        if (gr < 0.01f) gr = 0.0f;
        if (t != types[(std::size_t) s] || byp != bypassed[(std::size_t) s] || std::abs (gr - reduction[(std::size_t) s]) > 0.01f) dirty = true;
        types[(std::size_t) s] = t;
        bypassed[(std::size_t) s] = byp;
        reduction[(std::size_t) s] = gr;
    }
    if (dirty) repaint();
}

//==============================================================================
void ChainStrip::paintTile (juce::Graphics& g, int slot, juce::Rectangle<float> r, bool lifted) const
{
    const auto type = types[(std::size_t) slot];
    const bool empty = type == aura::ModuleType::none, byp = bypassed[(std::size_t) slot], sel = slot == selected;
    const auto accent = accentFor (type);

    if (lifted) juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 14, { 0, 6 }).drawForRectangle (g, r.toNearestInt().reduced (4));
    g.setColour (lifted ? panelHigh : (sel ? panelHigh.brighter (0.05f) : panel));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (sel || lifted ? (empty ? textDim : accent) : stroke);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, sel || lifted ? 1.6f : 1.0f);

    g.setColour (textDim);
    g.setFont (NextLookAndFeel::font (10.0f, true));
    g.drawText (juce::String (getDisplayedPosition (slot) + 1).paddedLeft ('0', 2), r.withTrimmedLeft (10.0f).withHeight (24.0f).toNearestInt(), juce::Justification::centredLeft);

    if (empty)
    {
        g.setColour (textDim.withAlpha (0.8f));
        g.setFont (NextLookAndFeel::font (28.0f));
        g.drawText ("+", r.toNearestInt(), juce::Justification::centred);
        g.setFont (NextLookAndFeel::font (10.0f));
        g.drawText (i18n::tr (i18n::Key::addModule), r.withTrimmedTop (r.getHeight() * 0.68f).reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centred);
        return;
    }

    g.setColour (accent.withAlpha (byp ? 0.35f : 1.0f));
    g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 10.0f, r.getY(), r.getWidth() - 20.0f, 2.5f), 1.25f);

    const auto& info = aura::moduleInfo (type);
    g.setColour (byp ? textDim : text);
    g.setFont (NextLookAndFeel::font (15.0f, true));
    g.drawFittedText (info.name, r.withTrimmedTop (28.0f).withTrimmedBottom (46.0f).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1);

    // Réduction de gain : filet en bas de tuile ; en mode Infos, la valeur en dB.
    const float gr = reduction[(std::size_t) slot];
    auto bar = juce::Rectangle<float> (r.getX() + 40.0f, r.getBottom() - 22.0f, r.getWidth() - 50.0f, 5.0f);
    g.setColour (panelHigh);
    g.fillRoundedRectangle (bar, 2.5f);
    if (gr > 0.0f)
    {
        g.setColour (colours::reduction);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, gr / 18.0f)), 2.5f);
    }
    if (infoMode)
    {
        g.setColour (textDim);
        g.setFont (NextLookAndFeel::font (10.0f, true));
        g.drawText (hasGainReduction (type) ? (gr > 0.0f ? "GR -" + juce::String (gr, 1) + " dB" : juce::String ("GR 0.0 dB")) : juce::String (info.shortName),
                    r.withTrimmedTop (r.getHeight() * 0.50f).withHeight (16.0f).toNearestInt(), juce::Justification::centred);
    }

    // Pastille d'activation (LED) et croix de retrait.
    const auto led = getBypassRect (slot).withSizeKeepingCentre (12.0f, 12.0f).translated (r.getX() - shown[(std::size_t) slot].getX(), r.getY() - shown[(std::size_t) slot].getY());
    g.setColour (byp ? stroke.brighter (0.2f) : levelOk);
    g.fillEllipse (led);
    if (! byp) { g.setColour (levelOk.withAlpha (0.25f)); g.fillEllipse (led.expanded (3.0f)); }
    const auto x = getRemoveRect (slot).withSizeKeepingCentre (9.0f, 9.0f).translated (r.getX() - shown[(std::size_t) slot].getX(), r.getY() - shown[(std::size_t) slot].getY());
    g.setColour (textDim);
    g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
    g.drawLine (x.getX(), x.getBottom(), x.getRight(), x.getY(), 1.4f);
}

void ChainStrip::paint (juce::Graphics& g)
{
    // Flèches de signal entre les tuiles : lecture gauche -> droite = sens du signal.
    g.setColour (stroke);
    for (int pos = 0; pos + 1 < aura::kNumSlots; ++pos)
    {
        const auto a = cellRect (pos), b = cellRect (pos + 1);
        const float y = a.getCentreY(), x0 = a.getRight() + 1.0f, x1 = b.getX() - 1.0f;
        g.drawLine (x0, y, x1, y, 1.0f);
    }
    const auto& order = dragging ? previewOrder : displayOrder;
    for (int pos = 0; pos < aura::kNumSlots; ++pos)
    {
        const int s = order[(std::size_t) pos];
        if (dragging && s == pressedSlot) continue;
        paintTile (g, s, shown[(std::size_t) s], false);
    }
    if (dragging) paintTile (g, pressedSlot, shown[(std::size_t) pressedSlot], true);   // la tuile saisie passe au-dessus
}

//==============================================================================
juce::MouseCursor ChainStrip::getMouseCursor()
{
    return dragging ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor;
}

void ChainStrip::updateTooltip (juce::Point<float> p)
{
    if (! infoMode) { setTooltip ({}); return; }
    const int s = slotAt (p);
    if (s < 0) { setTooltip ({}); return; }
    const auto t = types[(std::size_t) s];
    switch (hitTest (s, p))
    {
        case Hit::bypass: setTooltip (i18n::tr (i18n::Key::helpBypassTile)); break;
        case Hit::remove: setTooltip (i18n::tr (i18n::Key::helpRemoveTile)); break;
        default:
            setTooltip (t == aura::ModuleType::none ? i18n::tr (i18n::Key::helpEmptyTile)
                                                    : juce::String (aura::moduleInfo (t).name) + " \xe2\x80\x94 " + i18n::tr (i18n::descriptionKey (t))
                                                        + "\n" + i18n::tr (i18n::Key::dragToReorder));
    }
}

void ChainStrip::mouseMove (const juce::MouseEvent& e) { updateTooltip (e.position); }

void ChainStrip::toggleBypass (int slot)
{
    if (auto* p = processor.getValueTreeState().getParameter (aura::params::bypass (slot)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void ChainStrip::showAddMenuForTesting (int slot, int moduleTypeToPick)
{
    processor.setSlotModule (slot, (aura::ModuleType) moduleTypeToPick);
    types[(std::size_t) slot] = processor.getSlotModule (slot);
    selected = slot;
    if (onSelect) onSelect (slot);
}

void ChainStrip::openModuleMenu (int slot)
{
    juce::PopupMenu menu;
    const auto current = processor.getSlotModule (slot);
    menu.addSectionHeader (i18n::tr (current == aura::ModuleType::none ? i18n::Key::addModule : i18n::Key::chooseModule));
    for (int t = 1; t < aura::kNumModuleTypes; ++t)
        menu.addItem (t, juce::String (aura::moduleInfo ((aura::ModuleType) t).name), true, (int) current == t);
    if (current != aura::ModuleType::none)
    {
        menu.addSeparator();
        menu.addItem (kBypassAction, i18n::tr (bypassed[(std::size_t) slot] ? i18n::Key::enableModule : i18n::Key::bypassModule));
        menu.addItem (kRemoveAction, i18n::tr (i18n::Key::removeModule));
    }
    juce::Component::SafePointer<ChainStrip> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (shown[(std::size_t) slot].toNearestInt())),
                        [safe, slot] (int id)
                        {
                            if (safe == nullptr || id == 0) return;
                            if (id == kRemoveAction)      safe->processor.setSlotModule (slot, aura::ModuleType::none);
                            else if (id == kBypassAction) safe->toggleBypass (slot);
                            else                          safe->showAddMenuForTesting (slot, id);
                        });
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    pressedSlot = slotAt (e.position);
    pressedHit = hitTest (pressedSlot, e.position);
    dragging = false;
    if (pressedSlot >= 0 && e.mods.isPopupMenu()) { openModuleMenu (pressedSlot); pressedSlot = -1; pressedHit = Hit::none; }
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (pressedSlot < 0 || pressedHit != Hit::body || types[(std::size_t) pressedSlot] == aura::ModuleType::none) return;

    if (! dragging)
    {
        if (e.getDistanceFromDragStart() < 5) return;
        dragging = true;
        dragFromPosition = getDisplayedPosition (pressedSlot);
        dropPosition = dragFromPosition;
        previewOrder = displayOrder;
        grabOffset = e.getMouseDownPosition().toFloat() - shown[(std::size_t) pressedSlot].getTopLeft();
    }

    auto& r = shown[(std::size_t) pressedSlot];
    const float w = r.getWidth(), h = r.getHeight();
    float x = juce::jlimit (-6.0f, (float) getWidth() - w + 6.0f, e.position.x - grabOffset.x);
    const float y = juce::jlimit (-8.0f, 8.0f, e.position.y - grabOffset.y);   // léger soulèvement vertical, la tuile reste dans la bande
    r = { x, y, w, h };

    const int drop = nearestCell (r.getCentre());
    if (drop != dropPosition)
    {
        dropPosition = drop;
        previewOrder = aura::RackRouting::moved (displayOrder, dragFromPosition, dropPosition);
        layoutTargets (true);
    }
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    const int slot = pressedSlot;
    const auto hit = pressedHit;
    pressedSlot = -1;
    pressedHit = Hit::none;

    if (dragging)
    {
        dragging = false;
        if (dropPosition != dragFromPosition)
        {
            processor.moveSlot (dragFromPosition, dropPosition);   // seule écriture de tout le glissement
            displayOrder = previewOrder;
            shownPacked = aura::RackRouting::pack (displayOrder);  // l'écart éventuel avec le moteur sera repris au tick suivant
        }
        pressedSlot = -1;
        layoutTargets (true);
        repaint();
        return;
    }

    if (slot < 0 || slot != slotAt (e.position) || hitTest (slot, e.position) != hit) return;   // relâché ailleurs : annulé
    if (e.mods.isPopupMenu()) return;

    switch (hit)
    {
        case Hit::bypass: toggleBypass (slot); break;
        case Hit::remove: processor.setSlotModule (slot, aura::ModuleType::none); break;
        case Hit::body:
            if (types[(std::size_t) slot] == aura::ModuleType::none) openModuleMenu (slot);
            else { selected = slot; if (onSelect) onSelect (slot); repaint(); }
            break;
        case Hit::none: break;
    }
}

} // namespace aura::ui
