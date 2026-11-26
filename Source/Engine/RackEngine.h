// AuraRack — NextDSP
// Moteur du rack : 10 slots en série, réordonnables à chaud, sans verrou côté audio.
//
// Threads :
//   - message thread : setModuleType(), setOrder(), collectGarbage() (allocations, destructions) ;
//   - thread audio   : process() — aucune allocation, aucun verrou, aucun appel système ;
//   - prepare() / releaseResources() : audio arrêté (contrat JUCE), protégés par structMutex contre
//     le message thread uniquement.
//
// Échanges :
//   - module : le message thread construit et prépare un ModuleBox, le publie par std::atomic<ModuleBox*>
//     (exchange) ; le thread audio l'adopte au bloc suivant, fond l'ancien et le nouveau en 5 ms, puis
//     renvoie l'ancien au message thread par une FIFO SPSC pour destruction ;
//   - ordre : les 10 indices sont empaquetés dans un std::atomic<uint64_t> (4 bits par slot) ;
//     le thread audio applique le nouvel ordre au creux d'un fondu de 2,5 ms + 2,5 ms ;
//   - paramètres : pointeurs vers les std::atomic<float> de l'APVTS, lus une fois par tranche.

#pragma once

#include "../DSP/LoudnessMeter.h"
#include "../DSP/ModuleRegistry.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace aura
{
inline constexpr int kNumSlots = 10;

using SlotOrder = std::array<int, kNumSlots>;

//==============================================================================
/** Empaquetage lock-free de l'ordre des slots : un mot de 64 bits, 4 bits par position. */
struct RackRouting
{
    static constexpr SlotOrder identity() noexcept { return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }; }

    static bool isValid (const SlotOrder& o) noexcept
    {
        unsigned seen = 0;
        for (int s : o)
        {
            if (s < 0 || s >= kNumSlots || (seen & (1u << s)) != 0) return false;
            seen |= 1u << s;
        }
        return true;
    }

    /** Ordre obtenu en retirant la position `from` et en l'insérant à la position `to` (les autres glissent). */
    static SlotOrder moved (SlotOrder o, int from, int to) noexcept
    {
        if (from < 0 || from >= kNumSlots || to < 0 || to >= kNumSlots || from == to) return o;
        const int slot = o[(std::size_t) from];
        if (from < to)
            for (int i = from; i < to; ++i) o[(std::size_t) i] = o[(std::size_t) i + 1];
        else
            for (int i = from; i > to; --i) o[(std::size_t) i] = o[(std::size_t) i - 1];
        o[(std::size_t) to] = slot;
        return o;
    }

    static std::uint64_t pack (const SlotOrder& o) noexcept
    {
        std::uint64_t v = 0;
        for (int i = 0; i < kNumSlots; ++i)
            v |= (std::uint64_t) (o[(std::size_t) i] & 0xF) << (4 * i);
        return v;
    }

    static SlotOrder unpack (std::uint64_t v) noexcept
    {
        SlotOrder o {};
        for (int i = 0; i < kNumSlots; ++i)
            o[(std::size_t) i] = (int) ((v >> (4 * i)) & 0xF);
        return o;
    }
};

//==============================================================================
/** Sources des paramètres d'un slot (valeurs atomiques de l'APVTS, ou du banc d'essai). */
struct SlotParameterSources
{
    const std::atomic<float>* bypass  = nullptr;   // 0 / 1
    const std::atomic<float>* mix     = nullptr;   // 0 … 100 %
    const std::atomic<float>* inTrim  = nullptr;   // -24 … +24 dB
    const std::atomic<float>* outTrim = nullptr;   // -24 … +24 dB
    std::array<const std::atomic<float>*, kMaxModuleParams> params {};   // normalisés [0, 1]
};

struct GlobalParameterSources
{
    const std::atomic<float>* inputGain  = nullptr;   // dB
    const std::atomic<float>* outputGain = nullptr;   // dB
    const std::atomic<float>* masterMix  = nullptr;   // %
    const std::atomic<float>* sidechainSource = nullptr;   // 0 = bus Sidechain VST3, 1 = entrée du rack
};

enum class SidechainSource { externalBus = 0, rackInput = 1 };

//==============================================================================
/**
    Chemin Dry retardé de la latence du chemin Wet. Quand la latence change, l'ancienne et la nouvelle
    prise sont fondues linéairement en 5 ms : aucune discontinuité.
*/
class CompensatedDry
{
public:
    void prepare (double sampleRate, int maxLatency);
    void reset() noexcept;
    void setLatency (int samples) noexcept;
    int getLatency() const noexcept { return target; }
    bool isFading() const noexcept { return fade.isRamping(); }

    /** Pousse in, écrit la version retardée dans out (in et out peuvent être confondus). */
    void process (const double* inL, const double* inR, double* outL, double* outR, int n) noexcept;

private:
    std::array<dsp::DelayLine, 2> lines;
    dsp::LinearRamp fade;
    int from = 0, target = 0, maxLatency = 0;
};

//==============================================================================
class RackEngine
{
public:
    static constexpr int kChunk = 256;              // taille de tranche interne (lissages, tampons)
    static constexpr double kBypassFadeMs = 5.0;
    static constexpr double kSwapFadeMs   = 5.0;
    static constexpr double kReorderFadeMs = 2.5;   // descente, puis remontée

    RackEngine();
    ~RackEngine();

    RackEngine (const RackEngine&) = delete;
    RackEngine& operator= (const RackEngine&) = delete;

    void bindSlot (int slot, const SlotParameterSources& sources) noexcept;
    void bindGlobal (const GlobalParameterSources& sources) noexcept;

    //==== Audio arrêté ============================================================
    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources();

    //==== Thread audio (ou audio arrêté) ===========================================
    /** Vide les états (queues de Delay / Reverb, enveloppes, fondus) sans rien allouer : O(1) par ligne de retard. */
    void reset() noexcept;

    /** Traite n échantillons stéréo en place. scL / scR : bus Sidechain (nullptr si absent). */
    void process (double* left, double* right, int numSamples,
                  const double* scL, const double* scR, bool sidechainConnected) noexcept;

    //==== Message thread ==========================================================
    void setModuleType (int slot, ModuleType type);
    ModuleType getModuleType (int slot) const noexcept { return (ModuleType) slotsMeta[(std::size_t) slot].type.load(); }

    void setOrder (const SlotOrder& order) noexcept;
    SlotOrder getOrder() const noexcept { return RackRouting::unpack (requestedOrder.load (std::memory_order_acquire)); }

    /** Même ordre, en un seul mot : une lecture atomique cohérente (comparaison bon marché côté interface). */
    std::uint64_t getOrderPacked() const noexcept { return requestedOrder.load (std::memory_order_acquire); }

    /** Déplace une position vers une autre par compare-and-swap : sûr même si une restauration d'état
        change l'ordre au même instant (la boucle repart de la nouvelle valeur). Renvoie false si rien ne change. */
    bool moveSlot (int fromPosition, int toPosition) noexcept
    {
        auto current = requestedOrder.load (std::memory_order_acquire);
        for (;;)
        {
            const auto next = RackRouting::pack (RackRouting::moved (RackRouting::unpack (current), fromPosition, toPosition));
            if (next == current) return false;
            if (requestedOrder.compare_exchange_weak (current, next, std::memory_order_acq_rel, std::memory_order_acquire))
                return true;
        }
    }

    /** Détruit les modules rendus par le thread audio. */
    void collectGarbage();

    /** Latence totale des modules demandés (pour setLatencySamples). */
    int getRequestedLatency() const noexcept;

    //==== Indicateurs (lecture depuis n'importe quel thread) ======================
    struct Meters
    {
        // Crêtes en maintien : le thread audio ne fait que monter (un seul écrivain), l'interface lit et remet à zéro
        // avec exchange(0). Au pire une crête s'affiche deux fois ; aucune n'est perdue entre deux images.
        mutable std::array<std::atomic<float>, 2> inputPeak {}, outputPeak {};
        std::array<std::atomic<float>, kNumSlots> gainReductionDb {};
        mutable std::array<std::atomic<float>, kNumSlots> slotStatus {};   // crête de sortie du module (voyant), lue par exchange(0)
        std::array<std::atomic<float>, kNumSlots> slotAux {};   // valeur d'affichage du module (seuil W9BMK1, niveau VU L9A) : dernière valeur
        std::atomic<int> activeLatency { 0 };
        std::atomic<bool> sidechainActive { false };
    };
    const Meters& getMeters() const noexcept { return meters; }

    //==== Sonie (LUFS) : mesure en sortie du rack, désactivée par défaut (coût nul) ==
    void setLoudnessEnabled (bool on) noexcept { loudnessEnabled.store (on, std::memory_order_relaxed); }
    bool isLoudnessEnabled() const noexcept { return loudnessEnabled.load (std::memory_order_relaxed); }
    void setLoudnessMono (bool mono) noexcept { loudnessMono.store (mono, std::memory_order_relaxed); }
    void requestLoudnessReset() noexcept { loudnessResetRequested.store (true, std::memory_order_release); }
    const LoudnessMeter::Readout& getLoudness() const noexcept { return loudness.getReadout(); }

    double getSampleRate() const noexcept { return sampleRate; }

private:
    struct ModuleBox
    {
        ModuleType type = ModuleType::none;
        std::unique_ptr<AuraModuleDSP> module;
        int latency = 0;
    };

    /** Partie lue par le message thread (atomiques). */
    struct SlotMeta
    {
        std::atomic<int> type { 0 };
        std::atomic<int> latency { 0 };
    };

    /** Partie détenue par le thread audio. */
    struct Slot
    {
        std::atomic<ModuleBox*> incoming { nullptr };
        ModuleBox* current = nullptr;
        ModuleBox* previous = nullptr;   // en cours de fondu sortant
        ModuleBox* retirePending = nullptr;

        SlotParameterSources sources;
        std::array<float, kMaxModuleParams> lastNorm {};
        bool paramsDirty = true;
        bool wasBypassed = false;

        dsp::LinearRamp active;    // 1 = actif, 0 = bypass (5 ms)
        dsp::LinearRamp swap;      // poids du nouveau module (5 ms)
        dsp::OnePoleSmoother mix, inTrim, outTrim;
        CompensatedDry dry;
    };

    void processChunk (double* L, double* R, int n, const double* scL, const double* scR, bool scOn) noexcept;
    void processSlot (Slot& slot, int slotIndex, double* L, double* R, int n, const SidechainBlock& key) noexcept;
    void adoptIncoming (Slot& slot, int slotIndex) noexcept;
    void applyModuleParameters (Slot& slot) noexcept;
    void retire (Slot& slot, ModuleBox* box) noexcept;

    static float readOr (const std::atomic<float>* p, float fallback) noexcept
    {
        return p != nullptr ? p->load (std::memory_order_relaxed) : fallback;
    }

    /** Gain d'un paramètre en dB ; à moins de 1e-5 dB de 0, exactement 1 (valeurs de l'hôte arrondies en float). */
    static double gainFromDb (float db) noexcept
    {
        return std::abs (db) < 1.0e-5f ? 1.0 : dsp::dbToGain ((double) db);
    }

    /** Proportion d'un paramètre en % ; à moins de 1e-5 des bornes, exactement 0 ou 1. */
    static double proportionFromPercent (float percent) noexcept
    {
        const double v = std::clamp ((double) percent / 100.0, 0.0, 1.0);
        return v > 1.0 - 1.0e-5 ? 1.0 : (v < 1.0e-5 ? 0.0 : v);
    }

    // FIFO SPSC audio -> message thread des modules à détruire.
    static constexpr std::size_t kGarbageCapacity = 64;
    std::array<ModuleBox*, kGarbageCapacity> garbage {};
    std::atomic<std::size_t> garbageWrite { 0 }, garbageRead { 0 };
    bool pushGarbage (ModuleBox* box) noexcept;

    std::array<Slot, kNumSlots> slots;
    std::array<SlotMeta, kNumSlots> slotsMeta;
    GlobalParameterSources globals;

    std::atomic<std::uint64_t> requestedOrder { RackRouting::pack (RackRouting::identity()) };
    std::uint64_t activeOrderPacked = RackRouting::pack (RackRouting::identity());
    SlotOrder activeOrder = RackRouting::identity();
    dsp::LinearRamp reorderGain;

    dsp::OnePoleSmoother inputGain, outputGain, masterMix;
    CompensatedDry masterDry;

    // Tampons de travail (alloués dans prepare).
    std::vector<double> rackInL, rackInR, dryL, dryR, wetL, wetR, prevL, prevR, rawL, rawR, gainIn, reorderBuf;

    std::mutex structMutex;   // message thread <-> prepare(), jamais pris par le thread audio
    double sampleRate = 0.0;
    int maxModuleLatencySamples = 0;
    std::atomic<bool> prepared { false };

    Meters meters;
    LoudnessMeter loudness;
    std::atomic<bool> loudnessEnabled { false }, loudnessMono { false }, loudnessResetRequested { false };
};

} // namespace aura
