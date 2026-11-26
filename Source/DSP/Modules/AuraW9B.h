// AuraRack — NextDSP · W9BMK1
// Compresseur optique à lampes inspiré du Tube-Tech CL 1B, conçu pour le DXD (352,8 / 384 kHz) et correct à toute
// fréquence d'échantillonnage.
//
//   Contrôles : Input, Threshold, Ratio, Attack, Release, Output, Auto Focus + Focus (+ le Bypass du slot).
//
//   Chaîne : Input ─► détecteur (clé filtrée 30 Hz, panneau EL) ─► courbe à genou doux ─► cellule optique
//            ─► gain de compression ─► étage « lampe » ─► Output.
//
//   - Détection liée en stéréo sur la crête des deux canaux, après le gain d'entrée : Input pousse le signal dans le
//     compresseur ET dans l'étage lampe, comme sur l'appareil.
//   - Cellule optique à deux voies et mémoire (CompCommon.h) : Attack règle l'allumage de la cellule (0,5 à 300 ms),
//     Release la récupération après une compression soutenue (5 ms à 7,5 s) ; après une compression brève la
//     récupération est jusqu'à 4 fois plus rapide (dépendance du programme, sans réglage supplémentaire).
//   - Enveloppes en double, constantes en secondes (expm1), courbe analytique : même réponse à 44,1 kHz et à 384 kHz.
//   - Étage lampe : tanh asymétrique à gain petit signal unitaire et blocage du continu. Évaluation directe en DXD
//     (aucun retard, aucun affaiblissement des aigus), ADAA du 1er ordre sous 176,4 kHz.
//   - AUTO FOCUS (FocusTracker.h) : en mode Auto, le Threshold est piloté par un suiveur lent qui vise une réduction de gain
//     moyenne (Focus, 1 à 12 dB). Le réglage manuel de Threshold est mis de côté (point de départ à l'activation, retrouvé
//     à la désactivation) ; le seuil effectif est publié par getMeterAux() pour l'affichage.
//   - Voyant : crête de sortie (takeStatusPeak) — s'allume quand la sortie approche 0 dBFS.
// Latence : 0.

#pragma once

#include "CompCommon.h"
#include "FocusTracker.h"

namespace aura
{
class AuraW9B final : public AuraModuleDSP
{
public:
    enum Param { input, threshold, ratio, attack, release, output, autoFocus, focus, numParams };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* modes[] = { "Manual", "Auto" };
        static constexpr ParamSpec specs[] = {
            { "Input",     "dB", -12.0, 24.0,   0.0,   S::linear },
            { "Threshold", "dB", -60.0, 0.0,    -18.0, S::linear },
            { "Ratio",     ":1", 1.0,   10.0,   4.0,   S::logarithmic },
            { "Attack",    "ms", 0.5,   300.0,  10.0,  S::logarithmic },
            { "Release",   "ms", 5.0,   7500.0, 200.0, S::logarithmic },
            { "Output",    "dB", -12.0, 24.0,   0.0,   S::linear },
            { "Auto Focus", "",  0.0,   1.0,    0.0,   S::choice, modes, 2 },
            { "Focus",     "dB", 1.0,   12.0,   3.0,   S::linear },
        };
        static const ModuleInfo i { ModuleType::w9b, "W9BMK1", "W9B", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case input:     inputSm.setTarget (v); break;
            case threshold: manualThreshold = v; thresholdSm.setTarget (v); break;
            case ratio:     ratioSm.setTarget (std::max (1.0, v)); break;
            case attack:    attackMs = v; configureCell(); break;
            case release:   releaseMs = v; configureCell(); break;
            case output:    outputSm.setTarget (v); break;
            case autoFocus: setAutoFocus (v >= 0.5); break;
            case focus:     tracker.setTargetReduction (v); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        for (auto* s : { &inputSm, &thresholdSm, &ratioSm, &outputSm })
            s->reset (s->getTarget());
        cell.reset();
        tube.reset();
        keyHp.reset();
        elPower = 0.0;
        lastReduction = 0.0;
        statusPeak = 0.0;
        tracker.reset (manualThreshold);
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const double gIn = dsp::dbToGain (inputSm.next());
            const double xl = L[i] * gIn, xr = R[i] * gIn;

            // Clé : crête des deux canaux, filtrée à 30 Hz (pas de pompage sur les infra-graves), puis « panneau EL ».
            const double level = std::max (std::abs (keyHp.processSample (xl, 0)), std::abs (keyHp.processSample (xr, 1)));
            elPower += elCoef * (level * level - elPower);
            if (elPower < 1.0e-30) elPower = 0.0;
            const double xDb = dsp::gainToDb (std::sqrt (2.0 * elPower));   // référencé crête (+3 dB pour un sinus)

            const double ratioNow = ratioSm.next();
            double thresholdNow = thresholdSm.next();
            if (autoOn)
            {
                tracker.process (xDb, lastReduction, ratioNow, kKneeDb);
                thresholdNow = tracker.getThreshold();
            }
            const double target = compdetail::kneeReduction (xDb, thresholdNow, ratioNow, kKneeDb);
            const double reduction = cell.next (target);
            lastReduction = reduction;

            const double gOut = dsp::dbToGain (outputSm.next());
            const double gComp = dsp::dbToGain (-reduction);
            const double yl = tube.process (xl * gComp, 0) * gOut;
            const double yr = tube.process (xr * gComp, 1) * gOut;
            L[i] = yl;
            R[i] = yr;
            statusPeak = std::max (statusPeak, std::max (std::abs (yl), std::abs (yr)));
        }
    }

    double getGainReductionDb() const noexcept override { return lastReduction; }

    double takeStatusPeak() noexcept override
    {
        const double p = statusPeak;
        statusPeak = 0.0;
        return p;
    }

    /** Seuil effectif (dB) : celui du suiveur en mode Auto, le réglage manuel sinon. */
    double getMeterAux() const noexcept override { return autoOn ? tracker.getThreshold() : manualThreshold; }

    bool isAutoFocus() const noexcept { return autoOn; }
    const compdetail::FocusTracker& getTracker() const noexcept { return tracker; }

    /** Courbe statique (genou 10 dB) pour un niveau détecté en dB — utilisée par les tests. */
    double gainReductionFor (double xDb) const noexcept
    {
        return compdetail::kneeReduction (xDb, thresholdSm.getTarget(), ratioSm.getTarget(), kKneeDb);
    }

private:
    static constexpr double kKneeDb = 10.0;

    void prepareInternal() override
    {
        for (auto* s : { &inputSm, &thresholdSm, &ratioSm, &outputSm })
            s->prepare (sampleRate, 20.0);
        cell.prepare (sampleRate);
        tracker.prepare (sampleRate);
        tube.prepare (sampleRate);
        keyHp.setup (dsp::TptSvf::Type::highPass, 30.0, 0.70710678118654752, 0.0, sampleRate);
        configureCell();
    }

    void setAutoFocus (bool on) noexcept
    {
        if (on == autoOn) return;
        if (on)
            tracker.reset (thresholdSm.getCurrent());               // part du seuil manuel : aucun saut
        else
        {
            thresholdSm.reset (tracker.getThreshold());             // repart du seuil effectif et glisse vers le manuel (20 ms)
            thresholdSm.setTarget (manualThreshold);
        }
        autoOn = on;
    }

    void configureCell() noexcept
    {
        cell.fixedBlend = -1.0;
        cell.attackDepthDb = 24.0;
        cell.memKneeDb = 6.0;
        cell.setTimes (attackMs * 0.001, releaseMs * 0.00025, releaseMs * 0.001);
        // Panneau EL : 0,5 à 2 ms selon l'attaque (ne suit pas la forme d'onde des graves).
        elCoef = compdetail::alpha (std::clamp (0.25 * attackMs * 0.001, 5.0e-4, 2.0e-3), sampleRate);
    }

    double attackMs = 10.0, releaseMs = 200.0, manualThreshold = -18.0;
    bool autoOn = false;
    double elCoef = 0.0, elPower = 0.0, lastReduction = 0.0, statusPeak = 0.0;

    dsp::OnePoleSmoother inputSm, thresholdSm, ratioSm, outputSm;
    compdetail::OptoCell cell;
    compdetail::TubeStage tube;
    compdetail::FocusTracker tracker;
    dsp::TptSvf keyHp;
};

} // namespace aura
