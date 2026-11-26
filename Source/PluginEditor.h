// AuraRack — NextDSP · éditeur du plugin.
//
// Disposition type T-RackS :
//   - barre globale (Input / Output / Master Mix, Sidechain, niveaux, CPU, ⚙️) ;
//   - ZONE HAUTE « module actif » : le module sélectionné en grand (façade W9BMK1 / L9A / E9DS, ou grands potentiomètres) ;
//   - module LUFS optionnel (mode mastering), seulement si activé dans ⚙️ ;
//   - BANDE BASSE « chaîne » : les 10 slots dans l'ordre du signal, glisser-déposer, ajout / retrait / bypass rapides.
//
// État d'interface : réglages (langue, aide, LUFS, module sélectionné) = AuraRackProcessor::UiSettings, sauvegardés avec la
// session. L'éditeur les relit à chaque image : une restauration d'état ou un changement depuis le panneau ⚙️ se propage seul.
//
// Rendu vectoriel pur. Une seule horloge d'affichage (VBlankAttachment) : elle lit des atomiques, ne repeint que ce qui change,
// et se suspend d'elle-même quand la fenêtre est masquée.

#pragma once

#include "PluginProcessor.h"
#include "UI/ChainStrip.h"
#include "UI/InspectorView.h"
#include "UI/LoudnessBar.h"
#include "UI/NextLookAndFeel.h"
#include "UI/TopBar.h"

class AuraRackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AuraRackEditor (AuraRackProcessor&);
    ~AuraRackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kDesignWidth = 1280, kDesignHeight = 780, kTopBarHeight = 96, kStripHeight = 132;

    /** Contenu à taille fixe (1280 × 780), mis à l'échelle en bloc quand la fenêtre est redimensionnée. */
    struct Content final : juce::Component
    {
        explicit Content (AuraRackProcessor&);
        void tick (float dtSeconds);
        void paint (juce::Graphics&) override;
        void resized() override;

        /** Applique des réglages d'interface (langue, aide, LUFS, sélection) à tous les éléments. */
        void applySettings (const AuraRackProcessor::UiSettings&);
        void selectSlot (int slot);

        AuraRackProcessor& processor;
        aura::ui::TopBar topBar;
        aura::ui::InspectorView inspector;
        aura::ui::LoudnessBar loudness;
        aura::ui::ChainStrip strip;
        AuraRackProcessor::UiSettings shown;
        bool settingsApplied = false;
        juce::Rectangle<int> inspectorCaption, stripCaption;
    };

    Content& getContent() noexcept { return content; }

    /** Pour les tests : une image d'horloge. */
    void tickForTesting (float dtSeconds) { content.tick (dtSeconds); }

private:
    void onVBlank (double timestampSeconds);

    AuraRackProcessor& processor;
    aura::ui::NextLookAndFeel lookAndFeel;
    Content content;
    juce::TooltipWindow tooltipWindow { this, 350 };   // n'affiche que ce que le mode « infos » a renseigné
    juce::VBlankAttachment vblank;
    double lastTimestamp = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuraRackEditor)
};
