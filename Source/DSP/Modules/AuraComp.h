// AuraRack — NextDSP · AuraComp
// Compresseur à trois algorithmes, conçu pour le DXD (352,8 / 384 kHz) et correct à toute fréquence.
//
//   Aura-Opto : leveling amplifier à cellule optique (esprit LA-2A). Gain + Peak Reduction.
//               Attaque et relâchement dépendants du programme.
//   Aura-Tube : compresseur optique à lampes (esprit CL 1B). Threshold, Ratio, Attack, Release, Makeup.
//               Étage de sortie « lampe » : saturation asymétrique douce (ADAA) + blocage du continu.
//   L-Comp    : « Looser Compressor », un seul potentiomètre (Crush) qui règle seuil, ratio et makeup.
//               Attaque ~50 µs, relâchement à deux temps, plafond doux de sécurité.
//
// Calcul d'enveloppe (toute la dynamique de gain est en double, en dB) :
//   - constantes de temps exprimées en secondes ; coefficient 1-exp(-1/(τ·fs)) calculé par expm1
//     (aucune annulation numérique, même pour 7,5 s à 384 kHz) ;
//   - temps dépendants du programme réévalués à chaque échantillon : la réponse ne dépend pas de fs ;
//   - courbes de transfert analytiques et lisses (softplus, genou quadratique), sans table ni interpolation ;
//   - étage « panneau électroluminescent » (moyenne de puissance, τ ~ 1 ms) avant la conversion en dB :
//     pas de modulation du gain à 2f par le redressement du signal ;
//   - états ramenés exactement à zéro en dessous de 1e-15 dB (pas de dénormalisés).
// Latence : 0. Clé : entrée du module ou bus Sidechain.

#pragma once

#include "CompCommon.h"

namespace aura
{

//==============================================================================
class AuraComp final : public AuraModuleDSP
{
public:
    enum Param { algorithm, keySource, peakReduction, gain, threshold, ratio, attack, release, makeup, crush, numParams };
    enum Algorithm { opto = 0, tube = 1, lComp = 2 };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* algos[] = { "Aura-Opto", "Aura-Tube", "L-Comp" };
        static constexpr const char* keys[]  = { "Interne", "Sidechain" };
        static constexpr ParamSpec specs[] = {
            { "Algorithm",      "",   0.0,   2.0,    0.0,   S::choice, algos, 3 },
            { "Key",            "",   0.0,   1.0,    0.0,   S::choice, keys, 2 },
            { "Peak Reduction", "%",  0.0,   100.0,  40.0,  S::linear },
            { "Gain",           "dB", -12.0, 30.0,   0.0,   S::linear },
            { "Threshold",      "dB", -60.0, 0.0,    -18.0, S::linear },
            { "Ratio",          ":1", 1.0,   10.0,   4.0,   S::logarithmic },
            { "Attack",         "ms", 0.5,   300.0,  10.0,  S::logarithmic },
            { "Release",        "ms", 5.0,   7500.0, 200.0, S::logarithmic },
            { "Makeup",         "dB", -12.0, 24.0,   0.0,   S::linear },
            { "Crush",          "%",  0.0,   100.0,  50.0,  S::linear },
        };
        // Paramètres visibles par algorithme (bit i = paramètre i).
        static constexpr std::uint32_t masks[] = {
            (1u << algorithm) | (1u << keySource) | (1u << peakReduction) | (1u << gain),
            (1u << algorithm) | (1u << keySource) | (1u << threshold) | (1u << ratio) | (1u << attack) | (1u << release) | (1u << makeup),
            (1u << algorithm) | (1u << keySource) | (1u << crush),
        };
        static const ModuleInfo i { ModuleType::comp, "AuraComp", "COMP", specs, 100.0, algorithm, masks };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case algorithm:
            {
                const int a = std::clamp ((int) std::lround (v), 0, 2);
                if (a != algo) { algo = a; configureCell(); snapSmoothers(); resetState(); }
                break;
            }
            case keySource:     useSidechain = v >= 0.5; break;
            case peakReduction: driveSm.setTarget (0.6 * v); break;                // 0 … 60 dB envoyés au détecteur
            case gain:          gainSm.setTarget (v); break;
            case threshold:     thresholdSm.setTarget (v); break;
            case ratio:         ratioSm.setTarget (std::max (1.0, v)); break;
            case attack:        attackMs = v; configureCell(); updateElCoef(); break;
            case release:       releaseMs = v; configureCell(); break;
            case makeup:        makeupSm.setTarget (v); break;
            case crush:         crushSm.setTarget (std::clamp (v, 0.0, 100.0) * 0.01); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        snapSmoothers();
        resetState();
    }

    void process (double* L, double* R, int n, const SidechainBlock& key) noexcept override
    {
        const bool external = useSidechain && key.available;
        const double* kL = external ? key.left  : L;
        const double* kR = external ? key.right : R;

        switch (algo)
        {
            case opto:  processOpto (L, R, kL, kR, n);  break;
            case tube:  processTube (L, R, kL, kR, n);  break;
            default:    processLComp (L, R, kL, kR, n); break;
        }
    }

    double getGainReductionDb() const noexcept override { return lastReduction; }

    /** Courbe statique du mode Tube (genou 10 dB) pour un niveau d'entrée en dB — utilisée par les tests. */
    double gainReductionFor (double xDb) const noexcept
    {
        return compdetail::kneeReduction (xDb, thresholdSm.getTarget(), ratioSm.getTarget(), kTubeKneeDb);
    }

private:
    static constexpr double kTubeKneeDb = 10.0;
    static constexpr double kLCompKneeDb = 4.0;
    // Aura-Opto : courbe softplus de largeur 10 dB, ratio effectif 3:1, origine +18 dBFS (Peak Reduction = 0 : presque rien).
    static constexpr double kOptoWidthDb = 10.0, kOptoSlope = 1.0 - 1.0 / 3.0, kOptoOriginDb = 18.0;

    void prepareInternal() override
    {
        for (auto* s : { &driveSm, &gainSm, &thresholdSm, &ratioSm, &makeupSm, &crushSm })
            s->prepare (sampleRate, 20.0);
        cell.prepare (sampleRate);
        tubeStage.prepare (sampleRate);
        keyHp.setup (dsp::TptSvf::Type::highPass, 60.0, 0.70710678118654752, 0.0, sampleRate);
        peakDecay = std::exp (-1.0 / (0.0005 * sampleRate));
        configureCell();
        updateElCoef();
    }

    void configureCell() noexcept
    {
        switch (algo)
        {
            case opto:
                cell.fixedBlend = -1.0; cell.attackDepthDb = 6.0; cell.memKneeDb = 6.0;
                cell.setTimes (0.010, 0.060, 2.5);
                elCoef = compdetail::alpha (0.002, sampleRate);
                break;
            case tube:
                cell.fixedBlend = -1.0; cell.attackDepthDb = 24.0; cell.memKneeDb = 6.0;
                cell.setTimes (attackMs * 0.001, releaseMs * 0.00025, releaseMs * 0.001);
                updateElCoef();
                break;
            default:
                cell.fixedBlend = 0.5; cell.attackDepthDb = 1.0e9; cell.memKneeDb = 6.0;
                cell.setTimes (0.00005, 0.015, 0.200);
                break;
        }
    }

    void updateElCoef() noexcept
    {
        if (algo == tube)
            elCoef = compdetail::alpha (std::clamp (0.25 * attackMs * 0.001, 5.0e-4, 2.0e-3), sampleRate);
    }

    void snapSmoothers() noexcept
    {
        for (auto* s : { &driveSm, &gainSm, &thresholdSm, &ratioSm, &makeupSm, &crushSm })
            s->reset (s->getTarget());
    }

    void resetState() noexcept
    {
        cell.reset();
        tubeStage.reset();
        ceiling.reset();
        keyHp.reset();
        elPower = 0.0;
        peakHold = 0.0;
        lastReduction = 0.0;
    }

    /** Niveau (dB) vu par la cellule : puissance lissée par le « panneau EL », référencée crête (+3 dB pour un sinus). */
    double electroLuminescentDb (double level) noexcept
    {
        elPower += elCoef * (level * level - elPower);
        if (elPower < 1.0e-30) elPower = 0.0;
        return dsp::gainToDb (std::sqrt (2.0 * elPower));
    }

    void processOpto (double* L, double* R, const double* kL, const double* kR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double xDb = electroLuminescentDb (std::max (std::abs (kL[i]), std::abs (kR[i])));
            const double z = (xDb + driveSm.next() - kOptoOriginDb) / kOptoWidthDb;
            const double reduction = cell.next (kOptoSlope * kOptoWidthDb * compdetail::softplus (z));
            lastReduction = reduction;
            const double g = dsp::dbToGain (gainSm.next() - reduction);
            L[i] *= g;
            R[i] *= g;
        }
    }

    void processTube (double* L, double* R, const double* kL, const double* kR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double xDb = electroLuminescentDb (std::max (std::abs (kL[i]), std::abs (kR[i])));
            const double target = compdetail::kneeReduction (xDb, thresholdSm.next(), ratioSm.next(), kTubeKneeDb);
            const double reduction = cell.next (target);
            lastReduction = reduction;
            const double g = dsp::dbToGain (makeupSm.next() - reduction);
            L[i] = tubeStage.process (L[i] * g, 0);
            R[i] = tubeStage.process (R[i] * g, 1);
        }
    }

    void processLComp (double* L, double* R, const double* kL, const double* kR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            // Clé filtrée à 60 Hz, détecteur à maintien de crête (τ 0,5 ms) : pas de modulation à 2f sur les graves.
            const double hl = keyHp.processSample (kL[i], 0);
            const double hr = keyHp.processSample (kR[i], 1);
            peakHold = std::max (std::max (std::abs (hl), std::abs (hr)), peakHold * peakDecay);
            if (peakHold < 1.0e-30) peakHold = 0.0;

            // Un seul bouton : seuil -6 … -46 dB, ratio 2 … 20:1, makeup automatique (85 % de la compensation du plafond).
            const double c = crushSm.next();
            const double thr = -6.0 - 40.0 * c;
            const double rat = 2.0 * std::pow (10.0, c);
            const double autoMakeup = -0.85 * thr * (1.0 - 1.0 / rat);

            const double target = compdetail::kneeReduction (dsp::gainToDb (peakHold), thr, rat, kLCompKneeDb);
            const double reduction = cell.next (target);
            lastReduction = reduction;
            const double g = dsp::dbToGain (autoMakeup - reduction);
            L[i] = ceiling.process (L[i] * g, 0);
            R[i] = ceiling.process (R[i] * g, 1);
        }
    }

    int algo = opto;
    bool useSidechain = false;
    double attackMs = 10.0, releaseMs = 200.0;
    double elCoef = 0.0, elPower = 0.0, peakDecay = 0.0, peakHold = 0.0, lastReduction = 0.0;

    dsp::OnePoleSmoother driveSm, gainSm, thresholdSm, ratioSm, makeupSm, crushSm;
    compdetail::OptoCell cell;
    compdetail::TubeStage tubeStage;
    compdetail::SoftCeiling ceiling;
    dsp::TptSvf keyHp;
};

} // namespace aura
