// AuraRack — NextDSP · éditeur du plugin.
//
// Rendu vectoriel pur (Graphics / Path). Une seule horloge d'affichage (VBlankAttachment) pour toute l'interface :
// elle lit des atomiques, ne repeint que les zones qui changent, et se suspend d'elle-même quand la fenêtre est masquée.

#pragma once

#include "PluginProcessor.h"
#include "UI/NextLookAndFeel.h"
#include "UI/RackView.h"
#include "UI/TopBar.h"

class AuraRackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AuraRackEditor (AuraRackProcessor&);
    ~AuraRackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Pour les tests : une image d'horloge. */
    void tickForTesting (float dtSeconds) { content.tick (dtSeconds); }

    static constexpr int kDesignWidth = 1280, kDesignHeight = 780, kTopBarHeight = 62;

private:
    /** Contenu à taille fixe (1280 × 780), mis à l'échelle en bloc quand la fenêtre est redimensionnée. */
    struct Content final : juce::Component
    {
        explicit Content (AuraRackProcessor& p) : topBar (p), rack (p)
        {
            addAndMakeVisible (topBar);
            addAndMakeVisible (rack);
        }
        void tick (float dt) { topBar.tick (dt); rack.tick (dt); }
        void paint (juce::Graphics& g) override { g.fillAll (aura::ui::colours::background); }
        void resized() override
        {
            topBar.setBounds (0, 0, getWidth(), kTopBarHeight);
            rack.setBounds (getLocalBounds().withTrimmedTop (kTopBarHeight).reduced (14, 14));
        }
        aura::ui::TopBar topBar;
        aura::ui::RackView rack;
    };

    void onVBlank (double timestampSeconds);

    AuraRackProcessor& processor;
    aura::ui::NextLookAndFeel lookAndFeel;
    Content content;
    juce::VBlankAttachment vblank;
    double lastTimestamp = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuraRackEditor)
};
