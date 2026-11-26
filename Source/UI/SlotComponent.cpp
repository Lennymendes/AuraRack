// AuraRack — NextDSP · un slot du rack (carte).

#include "SlotComponent.h"

namespace aura::ui
{
using namespace colours;

namespace
{
    /** Modules qui rapportent une réduction de gain (les autres n'ont pas d'oscilloscope). */
    bool hasGainReduction (aura::ModuleType t) { return t == aura::ModuleType::comp || t == aura::ModuleType::duck || t == aura::ModuleType::limit; }
}

SlotComponent::SlotComponent (int slotIndex, AuraRackProcessor& p, SlotDragHost& host)
    : slot (slotIndex), processor (p), dragHost (host)
{
    auto& apvts = processor.getValueTreeState();

    for (int t = 0; t < aura::kNumModuleTypes; ++t)
        typeBox.addItem (aura::moduleInfo ((aura::ModuleType) t).name, t + 1);
    typeBox.onChange = [this]
    {
        if (updatingCombo) return;
        processor.setSlotModule (slot, (aura::ModuleType) (typeBox.getSelectedId() - 1));   // le sélecteur n'écrit que par l'API du processeur
    };
    addAndMakeVisible (typeBox);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::ToggleButton::tickColourId, reduction);
    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (aura::params::bypass (slot)), bypassButton, nullptr);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (scope);

    for (int i = 0; i < aura::kMaxModuleParams; ++i)
    {
        knobs[(std::size_t) i].bind (*apvts.getParameter (aura::params::generic (slot, i)));
        addAndMakeVisible (knobs[(std::size_t) i]);
    }
    mixKnob.bind (*apvts.getParameter (aura::params::mix (slot)));
    inKnob.bind  (*apvts.getParameter (aura::params::inTrim (slot)));
    outKnob.bind (*apvts.getParameter (aura::params::outTrim (slot)));
    mixKnob.setName ("Mix");
    inKnob.setName ("In");
    outKnob.setName ("Out");
    for (auto* k : { &mixKnob, &inKnob, &outKnob })
        addAndMakeVisible (*k);

    typeChanged();
}

void SlotComponent::setPosition (int newPosition)
{
    if (newPosition == position) return;
    position = newPosition;
    repaint (headerArea());
}

void SlotComponent::setLifted (bool shouldLift)
{
    if (lifted == shouldLift) return;
    lifted = shouldLift;
    repaint();
}

//==============================================================================
void SlotComponent::typeChanged()
{
    shownType = processor.getSlotModule (slot);
    const auto& info = aura::moduleInfo (shownType);
    const auto accent = colours::accentFor (shownType);

    updatingCombo = true;
    typeBox.setSelectedId ((int) shownType + 1, juce::dontSendNotification);
    updatingCombo = false;

    for (int i = 0; i < aura::kMaxModuleParams; ++i)
    {
        auto& k = knobs[(std::size_t) i];
        if (i < (int) info.params.size())
            k.setName (info.params[(std::size_t) i].name);
        k.setAccent (accent);
    }
    for (auto* k : { &mixKnob, &inKnob, &outKnob })
        k->setAccent (accent);

    if (shownType == aura::ModuleType::w9b)
    {
        if (faceplate == nullptr)
        {
            faceplate = std::make_unique<W9bFaceplate> (processor.getValueTreeState(), slot);
            addAndMakeVisible (*faceplate);
        }
    }
    else
        faceplate.reset();

    shownMask = ~0u;   // force applyVisibility
    applyVisibility();
    repaint();
}

void SlotComponent::applyVisibility()
{
    const auto& info = aura::moduleInfo (shownType);
    std::uint32_t mask = 0;
    if (shownType != aura::ModuleType::none)
    {
        double variant = 0.0;
        if (info.variantParam >= 0)
            if (auto* raw = processor.getValueTreeState().getRawParameterValue (aura::params::generic (slot, info.variantParam)))
                variant = info.params[(std::size_t) info.variantParam].toPlain (raw->load());
        mask = info.visibleMask (variant);
    }
    if (mask == shownMask) return;
    shownMask = mask;

    const bool hasModule = shownType != aura::ModuleType::none;
    const bool plate = shownType == aura::ModuleType::w9b;   // la façade porte ses propres contrôles (dont l'unique Bypass)
    for (int i = 0; i < aura::kMaxModuleParams; ++i)
        knobs[(std::size_t) i].setVisible (! plate && (mask & (1u << i)) != 0);
    mixKnob.setVisible (hasModule);                          // Mix = compression parallèle, utile aussi pour le W9BMK1
    inKnob.setVisible (hasModule && ! plate);                // le W9BMK1 a son propre Input / Output : pas de doublon
    outKnob.setVisible (hasModule && ! plate);
    bypassButton.setVisible (hasModule && ! plate);
    scope.setVisible (hasModule && ! plate && hasGainReduction (shownType));
    resized();
    repaint();
}

void SlotComponent::tick (float dt)
{
    if (processor.getSlotModule (slot) != shownType)
        typeChanged();
    else if (aura::moduleInfo (shownType).variantParam >= 0)
        applyVisibility();   // le choix d'algorithme change les potentiomètres affichés

    const bool nowBypassed = bypassButton.getToggleState();
    if (nowBypassed != bypassed) { bypassed = nowBypassed; repaint(); }

    if (faceplate != nullptr)
    {
        const auto& m = processor.getMeters();
        faceplate->tick (dt, m.gainReductionDb[(std::size_t) slot].load (std::memory_order_relaxed),
                         m.slotStatus[(std::size_t) slot].exchange (0.0f, std::memory_order_relaxed));
        return;
    }

    if (! scope.isVisible()) return;

    // Réduction de gain : lecture d'un atomique, lissage d'affichage léger, historique à 60 points/s.
    const float gr = processor.getMeters().gainReductionDb[(std::size_t) slot].load (std::memory_order_relaxed);
    smoothedReduction += (gr - smoothedReduction) * (1.0f - std::exp (-dt * 40.0f));
    pushClock += dt;
    while (pushClock >= 1.0f / 60.0f)
    {
        pushClock -= 1.0f / 60.0f;
        scope.push (smoothedReduction < 0.002f ? 0.0f : smoothedReduction);
    }
}

//==============================================================================
void SlotComponent::resized()
{
    auto r = getLocalBounds().reduced (10, 0);
    auto header = r.removeFromTop (kHeaderHeight).withTrimmedTop (5).withTrimmedBottom (3);
    header.removeFromLeft (34);   // poignée + numéro de position
    bypassButton.setBounds (header.removeFromRight (40));
    header.removeFromRight (6);
    typeBox.setBounds (header);

    r.removeFromTop (4);
    if (scope.isVisible())
    {
        scopeArea = r.removeFromTop (40);
        scope.setBounds (scopeArea);
        r.removeFromTop (4);
    }

    footerArea = r.removeFromBottom (58).withTrimmedBottom (4);
    knobArea = r;

    // Potentiomètres du module : seuls les visibles, 3 par ligne.
    constexpr int cols = 3;
    int visible = 0;
    for (auto& k : knobs) visible += k.isVisible() ? 1 : 0;
    const int rows = std::max (1, (visible + cols - 1) / cols);
    const int cellW = knobArea.getWidth() / cols, cellH = juce::jlimit (60, 76, knobArea.getHeight() / rows);
    int n = 0;
    for (int i = 0; i < aura::kMaxModuleParams; ++i)
    {
        auto& k = knobs[(std::size_t) i];
        if (! k.isVisible()) continue;
        k.setBounds (knobArea.getX() + (n % cols) * cellW, knobArea.getY() + (n / cols) * cellH, cellW, cellH);
        ++n;
    }

    if (faceplate != nullptr)
    {
        faceplate->setBounds (knobArea);
        mixKnob.setBounds (footerArea.withSizeKeepingCentre (footerArea.getWidth() / 3, footerArea.getHeight()));
        return;
    }

    const int fw = footerArea.getWidth() / 3;
    mixKnob.setBounds (footerArea.getX(), footerArea.getY(), fw, footerArea.getHeight());
    inKnob.setBounds  (footerArea.getX() + fw, footerArea.getY(), fw, footerArea.getHeight());
    outKnob.setBounds (footerArea.getX() + 2 * fw, footerArea.getY(), fw, footerArea.getHeight());
}

void SlotComponent::paint (juce::Graphics& g)
{
    const auto accent = colours::accentFor (shownType);
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    if (lifted)
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 18, { 0, 8 }).drawForRectangle (g, getLocalBounds().reduced (4));
    }

    g.setColour (lifted ? panelHigh : panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (lifted ? accent : stroke);
    g.drawRoundedRectangle (r, 10.0f, lifted ? 1.5f : 1.0f);

    // Filet d'accent du module.
    if (shownType != aura::ModuleType::none)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 14.0f, r.getY(), r.getWidth() - 28.0f, 2.5f), 1.25f);
    }

    // Poignée de glissement et numéro de position.
    g.setColour (textDim);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col)
            g.fillEllipse (13.0f + (float) col * 4.5f, 12.0f + (float) row * 4.5f, 2.2f, 2.2f);

    if (shownType == aura::ModuleType::none)
    {
        g.setColour (textDim);
        g.setFont (NextLookAndFeel::font (12.5f));
        g.drawFittedText ("Slot vide\nChoisir un module", getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (20), juce::Justification::centred, 2);
    }

    g.setColour (textDim);
    g.setFont (NextLookAndFeel::font (10.5f, true));
    g.drawText (juce::String (position + 1).paddedLeft ('0', 2), 26, 6, 18, kHeaderHeight - 10, juce::Justification::centredLeft);
}

void SlotComponent::paintOverChildren (juce::Graphics& g)
{
    if (bypassed && shownType != aura::ModuleType::none)
    {
        g.setColour (background.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f).withTrimmedTop ((float) kHeaderHeight), 10.0f);
    }
}

juce::MouseCursor SlotComponent::getMouseCursor()
{
    return dragging ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor;
}

void SlotComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! headerArea().contains (e.getPosition())) return;   // seul l'en-tête sert de poignée
    dragging = true;
    dragHost.slotDragBegan (*this, e);
}

void SlotComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging) dragHost.slotDragged (*this, e);
}

void SlotComponent::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging) return;
    dragging = false;
    dragHost.slotDragEnded (*this, e);
}

} // namespace aura::ui
