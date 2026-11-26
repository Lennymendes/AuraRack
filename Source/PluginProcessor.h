// AuraRack — NextDSP
// Processeur du plugin : bus principal + bus Sidechain VST3 / AU, traitement natif 64 bits.

#pragma once

#include "Parameters.h"
#include "Presets.h"

#include <juce_audio_processors/juce_audio_processors.h>

class AuraRackProcessor final : public juce::AudioProcessor,
                                private juce::Timer
{
public:
    AuraRackProcessor();
    ~AuraRackProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    bool supportsDoublePrecisionProcessing() const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==== API de l'interface (message thread) ===========================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    /** Change le module d'un slot : paramètres génériques remis aux valeurs par défaut du module. */
    void setSlotModule (int slot, aura::ModuleType type);
    aura::ModuleType getSlotModule (int slot) const noexcept { return engine.getModuleType (slot); }

    /** Nouvel ordre des slots (glisser-déposer), appliqué sans décrochage audio. */
    void setSlotOrder (const aura::SlotOrder& order);
    aura::SlotOrder getSlotOrder() const noexcept { return engine.getOrder(); }
    std::uint64_t getSlotOrderPacked() const noexcept { return engine.getOrderPacked(); }

    /** Déplace un slot de la position `from` à la position `to` (indices dans l'ordre actuel). */
    void moveSlot (int fromPosition, int toPosition);

    //==== Instantanés, A / B et presets (message thread) =================================
    /** Tous les paramètres (valeurs normalisées par identifiant) + ordre des slots. Modules, réglages et ordre se
        restaurent ensemble : c'est l'unité de base des comparaisons A / B et des presets. */
    struct RackSnapshot
    {
        std::vector<std::pair<juce::String, float>> values;
        aura::SlotOrder order = aura::RackRouting::identity();
        bool valid = false;
    };
    RackSnapshot captureSnapshot() const;
    void applySnapshot (const RackSnapshot&);

    /** A / B : deux configurations complètes du rack. 0 = A, 1 = B. Bascule en un clic ; B part d'une copie de A. */
    int getAbActive() const noexcept { return abActive; }
    bool isAbStored (int which) const noexcept { return which >= 0 && which < 2 && abSlots[(std::size_t) which].valid; }
    void selectAb (int which);
    void copyToOtherAb();   // copie la configuration courante dans l'autre emplacement

    /** Presets d'usine (Presets.h). setPresetIndex(-1) = aucun. */
    static int getNumFactoryPresets() noexcept { return (int) aura::presets::factory().size(); }
    void loadFactoryPreset (int index);
    int getPresetIndex() const noexcept { return presetIndex; }
    bool isPresetModified() const;   // le rack s'écarte-t-il du preset chargé ?
    void clearRack();                // rack vide (tous les slots à « Vide », réglages globaux par défaut)

    //==== Réglages d'interface (sauvegardés avec la session, jamais automatisables) =====
    struct UiSettings
    {
        int language = 0;           // index de langue (voir UI/I18n.h)
        bool info = false;          // aide contextuelle + indicateurs techniques
        bool loudness = false;      // mesure LUFS (mode mastering)
        int selectedSlot = 0;       // module affiché dans la zone haute
        bool operator== (const UiSettings&) const = default;
    };
    UiSettings getUiSettings() const;
    void setUiSettings (const UiSettings&);

    /** Sonie : activée par les réglages, mesurée en sortie du rack (voir DSP/LoudnessMeter.h). */
    const aura::LoudnessMeter::Readout& getLoudness() const noexcept { return engine.getLoudness(); }
    void resetLoudness() noexcept { engine.requestLoudnessReset(); }
    bool isLoudnessEnabled() const noexcept { return engine.isLoudnessEnabled(); }

    const aura::RackEngine::Meters& getMeters() const noexcept { return engine.getMeters(); }
    float getCpuLoad() const noexcept { return cpuLoad.load (std::memory_order_relaxed); }
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }
    bool isSidechainConnected() const noexcept { return sidechainConnected.load (std::memory_order_relaxed); }

private:
    template <typename Sample>
    void processAnyPrecision (juce::AudioBuffer<Sample>& buffer);

    void processDouble (double* L, double* R, int n, const double* scL, const double* scR, bool scOn) noexcept;

    void timerCallback() override;
    void syncModulesFromParameters (bool resetParametersToModuleDefaults);
    void applyModuleDefaults (int slot, aura::ModuleType type);
    void bindEngine();

    static BusesProperties makeBuses();

    aura::RackEngine engine;   // déclaré avant l'APVTS : l'affichage des paramètres le consulte
    juce::AudioProcessorValueTreeState apvts;

    // Tampons de conversion float <-> double et de mono -> stéréo (alloués dans prepareToPlay).
    juce::AudioBuffer<double> work;
    int workCapacity = 0;

    std::atomic<float> cpuLoad { 0.0f };
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<bool> sidechainConnected { false };

    RackSnapshot buildPresetSnapshot (const aura::presets::Preset&) const;
    void applySnapshotLocked (const RackSnapshot&);
    static bool snapshotsEqual (const RackSnapshot&, const RackSnapshot&) noexcept;

    std::array<RackSnapshot, 2> abSlots;
    int abActive = 0;
    int presetIndex = -1;
    RackSnapshot presetBaseline;

    std::mutex stateMutex;   // setStateInformation <-> timer (jamais le thread audio)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuraRackProcessor)
};
