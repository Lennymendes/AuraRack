// AuraRack — NextDSP · F976
// Compresseur FET à contre-réaction, inspiré des 1176 (Rev A « Blue Stripe » et Rev D/E « Blackface »), version DXD
// (352,8 / 384 kHz, double 64 bits). Conception AuraRack : constantes calibrées par mesure, pas par comparaison avec le matériel.
//
//   Contrôles : Input (0 … +30 dB), Output (−36 … +12 dB), Attack (20 … 800 µs, en µs réelles : le potard affiche la durée),
//   Release (50 … 1100 ms, dépendant du programme), Ratio 4 / 8 / 12 / 20 : 1 ou ALL (tous boutons enfoncés), Révision
//   (Rev A / Rev D-E), SC HPF (Off / 60 / 90 / 130 / 200 Hz, détecteur seulement), Meter (GR / +4 / +8, AFFICHAGE seulement :
//   choix de la grandeur lue sur le VU, comme sur le matériel ; aucun effet audio). Le Mix et le Bypass sont ceux du slot.
//
//   ÉTALONNAGE : 0 dBu = −18 dBFS. Le SEUIL EST FIXE (+8 dBu = −10 dBFS à l'entrée de la cellule) : c'est Input qui pousse le
//   signal dans la cellule, donc qui règle la quantité de compression (comme le potard Input d'un 1176) ; Output ajuste ensuite
//   le niveau. Au défaut (Input 10, Output −10) le niveau sous le seuil est inchangé.
//
//   Chaîne : Input ─► étage d'entrée saturant (harmoniques 2 / 3, croissants avec le niveau, sans repliement) ─► cellule FET
//            (gain commandé par le détecteur de crête, voie de clé HPF, liaison stéréo = max) ─► Output.
//
//   - Loi statique à contre-réaction : rapport R = 1 + gain de boucle ; réduction = (1 − 1/R)·dépassement, genou 6 dB (Rev A)
//     ou 3 dB (Rev D/E, plus tranchant). Le détecteur voit le signal d'ENTRÉE de la cellule : la loi est l'équivalent
//     statique de la boucle, stable à toute fréquence d'échantillonnage.
//   - Attack : constante de temps de la réduction, en µs (20 µs minimum : à 44,1 kHz, 1 échantillon = 22,7 µs ; la
//     cellule reste stable et sans clic car le gain est lissé). C'est la constante de temps exacte de la réduction : durée pour atteindre 63 % de la réduction finale sur un échelon.
//   - Release : deux voies (rapide = 0,25 × Release, lente = Release), la voie lente pèse davantage après une compression
//     soutenue : dépendant du programme.
//   - ALL (tous boutons) : seuil −3 dB, rapport croissant avec le dépassement (12 → 30:1), attaque et relâchement × 0,6,
//     coloration × 1,6 : écrasement agressif et pompage musical.
//   - Rev A : coloration plus riche (H2 + H3), plus de genou ; Rev D/E : plus propre et plus « punchy ».
//   - Latence 0.

#pragma once

#include "ColourStage.h"

namespace aura
{
class AuraF976 final : public AuraModuleDSP
{
public:
    enum Param { input, output, attack, release, ratio, revision, scHpf, meter, numParams };

    static constexpr double kThresholdDbfs = -10.0, kDbuToDbfs = -18.0;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* ratios[] = { "4:1", "8:1", "12:1", "20:1", "ALL" };
        static constexpr const char* revs[]   = { "Rev A", "Rev D/E" };
        static constexpr const char* hpfs[]   = { "Off", "60 Hz", "90 Hz", "130 Hz", "200 Hz" };
        static constexpr const char* meters[] = { "GR", "+4", "+8" };
        static constexpr ParamSpec specs[] = {
            { "Input",   "dB",        0.0, 30.0, 10.0, S::linear },
            { "Output",  "dB",        -36.0, 12.0, -10.0, S::linear },
            { "Attack",  "\xc2\xb5s", 20.0, 800.0, 200.0, S::logarithmic },
            { "Release", "ms",        50.0, 1100.0, 400.0, S::logarithmic },
            { "Ratio",   "",          0.0, 4.0, 0.0, S::choice, ratios, 5 },
            { "Revision", "",         0.0, 1.0, 1.0, S::choice, revs, 2 },
            { "SC HPF",  "",          0.0, 4.0, 0.0, S::choice, hpfs, 5 },
            { "Meter",   "",          0.0, 2.0, 0.0, S::choice, meters, 3 },
        };
        static const ModuleInfo i { ModuleType::f976, "F976", "F976", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    /** Réduction statique (dB) pour un niveau d'entrée de cellule (dBFS), hors temps : utilisée par les tests. */
    static double staticReduction (double levelDb, int ratioIndex, bool revA) noexcept
    {
        const bool all = ratioIndex == 4;
        const double thr = kThresholdDbfs - (all ? 3.0 : 0.0), knee = revA ? 6.0 : 3.0;
        return reductionFor (levelDb, thr, ratioIndex, knee);
    }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case input:    inGain.setTarget (dsp::dbToGain (v)); break;
            case output:   outGain.setTarget (dsp::dbToGain (v)); break;
            case attack:   attackUs = v; updateTimes(); break;
            case release:  releaseMs = v; updateTimes(); break;
            case ratio:    ratioIndex = std::clamp ((int) std::lround (v), 0, 4); updateTimes(); break;
            case revision: revA = std::lround (v) == 0; break;
            case scHpf:    { const int i = std::clamp ((int) std::lround (v), 0, 4);
                             if (i > 0) { static constexpr double f[] = { 0.0, 60.0, 90.0, 130.0, 200.0 }; hp.setup (dsp::TptSvf::Type::highPass, f[i], 0.70710678118654752, 0.0, sampleRate); }
                             hpOn = i > 0; break; }
            case meter:    break;   // affichage seulement
            default: break;
        }
    }

    void reset() noexcept override
    {
        inGain.reset (inGain.getTarget()); outGain.reset (outGain.getTarget());
        hp.reset(); colour.reset(); cell.reset();
        env = 0.0; reduction = 0.0; vu = 0.0; statusPeak = 0.0;
        gainSm = 1.0;
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        const bool all = ratioIndex == 4;
        const double thr = kThresholdDbfs - (all ? 3.0 : 0.0), knee = revA ? 6.0 : 3.0;
        const double c2 = (revA ? 0.020 : 0.004) * (all ? 1.6 : 1.0), c3 = (revA ? 0.045 : 0.010) * (all ? 1.6 : 1.0);
        for (int i = 0; i < n; ++i)
        {
            const double gi = inGain.next(), go = outGain.next();
            double xl = L[i] * gi, xr = R[i] * gi;
            // Étage d'entrée saturant (additif, sans repliement) : la coloration croît avec le niveau poussé par Input.
            xl = colour.process (xl, 0, kColourRef, c2, c3);
            xr = colour.process (xr, 1, kColourRef, c2, c3);

            // Détecteur de crête (clé HPF), liaison stéréo = max, relâchement de crête 1,5 ms.
            double kl = xl, kr = xr;
            if (hpOn) { kl = hp.processSample (xl, 0); kr = hp.processSample (xr, 1); }
            env = std::max ({ std::abs (kl), std::abs (kr), env * peakDecay });
            const double levelDb = env > 1.0e-9 ? dsp::gainToDb (env) : -180.0;
            const double target = levelDb > thr - 0.5 * knee ? reductionFor (levelDb, thr, ratioIndex, knee) : 0.0;
            reduction = cell.next (target);
            // Le gain suit la réduction (lissé à 1/3 de l'attaque minimale : aucun clic, aucun saut).
            const double gTarget = reduction > 1.0e-9 ? dsp::dbToGain (-reduction) : 1.0;
            gainSm += gainCoef * (gTarget - gainSm);
            const double g = gainSm * go;
            xl *= g; xr *= g;
            L[i] = xl; R[i] = xr;
            vu += vuCoef * (0.5 * (xl * xl + xr * xr) - vu);
            statusPeak = std::max (statusPeak, std::max (std::abs (xl), std::abs (xr)));
        }
        lastReduction = reduction;
    }

    double getGainReductionDb() const noexcept override { return lastReduction; }
    double takeStatusPeak() noexcept override { const double p = statusPeak; statusPeak = 0.0; return p; }
    /** Niveau de sortie à balistique VU (dBFS RMS) pour les positions +4 / +8 du Meter. */
    double getMeterAux() const noexcept override { return vu > 1.0e-14 ? 10.0 * std::log10 (vu) : -140.0; }

private:
    static constexpr double kColourRef = 0.5;   // −6 dBFS

    static double reductionFor (double levelDb, double thr, int ratioIdx, double knee) noexcept
    {
        static constexpr double ratios[] = { 4.0, 8.0, 12.0, 20.0 };
        if (ratioIdx == 4)
        {
            const double o = std::max (0.0, levelDb - thr);
            const double r = 12.0 + 0.9 * std::min (o, 20.0);   // 12:1 qui monte vers 30:1
            return compdetail::kneeReduction (levelDb, thr, r, knee);
        }
        return compdetail::kneeReduction (levelDb, thr, ratios[ratioIdx], knee);
    }

    void prepareInternal() override
    {
        inGain.prepare (sampleRate, 20.0); outGain.prepare (sampleRate, 20.0);
        colour.prepare (sampleRate);
        hp.setup (dsp::TptSvf::Type::highPass, 60.0, 0.70710678118654752, 0.0, sampleRate);
        peakDecay = std::exp (-1.0 / (0.0015 * sampleRate));
        vuCoef = compdetail::alpha (0.108, sampleRate);   // VU : ≈ 300 ms à 99 %
        cell.prepare (sampleRate);
        cell.attackDepthDb = 1.0e9;   // Attack = constante de temps exacte (pas d'accélération avec la profondeur)
        cell.memKneeDb = 8.0;
        updateTimes();
    }

    void updateTimes() noexcept
    {
        const bool all = ratioIndex == 4;
        const double att = attackUs * 1.0e-6 * (all ? 0.6 : 1.0);
        const double rel = releaseMs * 1.0e-3 * (all ? 0.6 : 1.0);
        cell.setTimes (att, 0.25 * rel, rel);
        gainCoef = compdetail::alpha (std::max (att / 3.0, 3.0e-6), sampleRate);
    }

    int ratioIndex = 0;
    bool revA = false, hpOn = false;
    double attackUs = 200.0, releaseMs = 400.0;
    double env = 0.0, reduction = 0.0, lastReduction = 0.0, vu = 0.0, statusPeak = 0.0, peakDecay = 0.0, vuCoef = 0.0, gainSm = 1.0, gainCoef = 0.5;

    dsp::OnePoleSmoother inGain, outGain;
    dsp::TptSvf hp;
    compdetail::HarmonicStage colour;
    compdetail::OptoCell cell;
};

} // namespace aura
