// AuraRack — NextDSP · un slot du rack (carte).

#pragma once

#include "../PluginProcessor.h"
#include "Faceplate.h"
#include "Widgets.h"

namespace aura::ui
{
class SlotComponent;

/** Le rack écoute le glisser-déposer des en-têtes de slot. */
struct SlotDragHost
{
    virtual ~SlotDragHost() = default;
    virtual void slotDragBegan (SlotComponent&, const juce::MouseEvent&) = 0;
    virtual void slotDragged (SlotComponent&, const juce::MouseEvent&) = 0;
    virtual void slotDragEnded (SlotComponent&, const juce::MouseEvent&) = 0;
};

/**
    Carte d'un slot. Elle est liée à l'IDENTITÉ du slot (0 … 9 = paramètres sN_*), pas à sa position :
    réordonner ne fait que déplacer son rectangle, les liaisons aux paramètres restent valables.
*/
class SlotComponent final : public juce::Component
{
public:
    static constexpr int kHeaderHeight = 32;

    SlotComponent (int slotIndex, AuraRackProcessor&, SlotDragHost&);

    int getSlot() const noexcept { return slot; }
    void setPosition (int position);
    void setLifted (bool lifted);

    /** Synchronise la vue sur le moteur (thread de messages, une fois par image). */
    void tick (float dtSeconds);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;

private:
    void typeChanged();
    void applyVisibility();
    juce::Rectangle<int> headerArea() const { return getLocalBounds().removeFromTop (kHeaderHeight); }

    const int slot;
    AuraRackProcessor& processor;
    SlotDragHost& dragHost;

    int position = 0;
    bool lifted = false, dragging = false, bypassed = false, updatingCombo = false;
    aura::ModuleType shownType = (aura::ModuleType) -1;
    std::uint32_t shownMask = 0;

    juce::ComboBox typeBox;
    juce::ToggleButton bypassButton { "BYP" };
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
    GainReductionScope scope;
    std::array<ParamKnob, aura::kMaxModuleParams> knobs;
    ParamKnob mixKnob, inKnob, outKnob;
    std::unique_ptr<Faceplate> faceplate;   // W9BMK1, L9A : la façade remplace les potentiomètres génériques

    float smoothedReduction = 0.0f, pushClock = 0.0f;
    juce::Rectangle<int> knobArea, footerArea, scopeArea;
};

} // namespace aura::ui
