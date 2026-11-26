// AuraRack — NextDSP · L9A
// Amplificateur de nivellement à cellule optique, inspiré du LA-2A, conçu pour le DXD (352,8 / 384 kHz) et correct à
// toute fréquence d'échantillonnage.
//
//   Contrôles : Peak Reduction, Gain, Mode (Comp / Limit), Meter (GR / Output) (+ le Bypass du slot).
//
//   Chaîne : entrée ─► détecteur (clé liée stéréo, HP 30 Hz, panneau EL) ─► courbe softplus (Peak Reduction = niveau
//            envoyé au détecteur, pas un seuil) ─► cellule optique à 2 voies + mémoire ─► gain de compression
//            ─► Gain (make-up) ─► étage « lampe » ─► sortie.
//
//   - COMP  : ratio effectif ≈ 3:1, genou doux de 10 dB, attaque 10 ms : nivellement musical.
//   - LIMIT : ratio effectif ≈ 10:1, genou de 4 dB, attaque 6 ms, voie rapide 45 ms : plafonnement plus franc.
//     Le passage de l'un à l'autre est lissé (20 ms) : pas de clic, la cellule garde son état.
//   - Cellule : mêmes constantes que l'Aura-Opto de l'AuraComp, relâchement dépendant du programme (≈ 55 ms après un
//     passage bref, plusieurs secondes après une compression soutenue), attaque qui accélère avec la profondeur.
//   - Meter : le module publie le niveau de sortie à balistique VU (deux pôles, ≈ 300 ms à 99 %, RMS en dBFS) par
//     getMeterAux() ; la réduction de gain par getGainReductionDb(). `meter` choisit ce que la façade affiche : c'est un
//     vrai paramètre (sauvegardé, automatisable) qui ne change pas le son.
//   - Constantes en secondes (expm1), enveloppes en double : même réponse à 44,1 kHz et à 384 kHz. Latence 0.

#pragma once

#include "CompCommon.h"

namespace aura
{
class AuraL9A final : public AuraModuleDSP
{
public:
    enum Param { peakReduction, gain, mode, meter, numParams };
    enum Mode { comp = 0, limit = 1 };
    enum Meter { gainReduction = 0, output = 1 };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* modes[]  = { "Comp", "Limit" };
        static constexpr const char* meters[] = { "GR", "Output" };
        static constexpr ParamSpec specs[] = {
            { "Peak Reduction", "%",  0.0,   100.0, 40.0, S::linear },
            { "Gain",           "dB", -12.0, 30.0,  0.0,  S::linear },
            { "Mode",           "",   0.0,   1.0,   0.0,  S::choice, modes, 2 },
            { "Meter",          "",   0.0,   1.0,   0.0,  S::choice, meters, 2 },
        };
        static const ModuleInfo i { ModuleType::l9a, "L9A", "L9A", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case peakReduction: driveSm.setTarget (0.6 * v); break;   // 0 … 60 dB envoyés au détecteur
            case gain:          gainSm.setTarget (v); break;
            case mode:          setMode (v >= 0.5 ? limit : comp); break;
            default: break;   // meter : affichage seulement
        }
    }

    void reset() noexcept override
    {
        for (auto* s : { &driveSm, &gainSm, &slopeSm, &widthSm })
            s->reset (s->getTarget());
        cell.reset();
        tube.reset();
        keyHp.reset();
        elPower = vuStage1 = vuStage2 = 0.0;
        lastReduction = 0.0;
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const double level = std::max (std::abs (keyHp.processSample (L[i], 0)), std::abs (keyHp.processSample (R[i], 1)));
            elPower += elCoef * (level * level - elPower);
            if (elPower < 1.0e-30) elPower = 0.0;
            const double xDb = dsp::gainToDb (std::sqrt (2.0 * elPower));   // référencé crête (+3 dB pour un sinus)

            const double width = widthSm.next();
            const double z = (xDb + driveSm.next() - kOriginDb) / width;
            const double reduction = cell.next (slopeSm.next() * width * compdetail::softplus (z));
            lastReduction = reduction;

            const double g = dsp::dbToGain (gainSm.next() - reduction);
            const double yl = tube.process (L[i] * g, 0), yr = tube.process (R[i] * g, 1);
            L[i] = yl;
            R[i] = yr;

            // Balistique VU sur la puissance de sortie : deux pôles de 45 ms (99 % en ≈ 300 ms).
            const double p = 0.5 * (yl * yl + yr * yr);
            vuStage1 += vuCoef * (p - vuStage1);
            vuStage2 += vuCoef * (vuStage1 - vuStage2);
        }
        if (vuStage1 < 1.0e-30) vuStage1 = 0.0;
        if (vuStage2 < 1.0e-30) vuStage2 = 0.0;
    }

    double getGainReductionDb() const noexcept override { return lastReduction; }

    /** Niveau de sortie à balistique VU, en dBFS RMS (-120 si silence). */
    double getMeterAux() const noexcept override
    {
        return vuStage2 > 1.0e-12 ? 10.0 * std::log10 (vuStage2) : -120.0;
    }

    /** Courbe statique du mode courant pour un niveau détecté (dB) — utilisée par les tests. */
    double gainReductionFor (double xDb) const noexcept
    {
        const double w = widthSm.getTarget();
        return slopeSm.getTarget() * w * compdetail::softplus ((xDb + driveSm.getTarget() - kOriginDb) / w);
    }

private:
    static constexpr double kOriginDb = 18.0;   // Peak Reduction = 0 : presque aucune réduction

    void prepareInternal() override
    {
        for (auto* s : { &driveSm, &gainSm, &slopeSm, &widthSm })
            s->prepare (sampleRate, 20.0);
        cell.prepare (sampleRate);
        tube.prepare (sampleRate);
        keyHp.setup (dsp::TptSvf::Type::highPass, 30.0, 0.70710678118654752, 0.0, sampleRate);
        elCoef = compdetail::alpha (0.002, sampleRate);
        vuCoef = compdetail::alpha (0.045, sampleRate);
        slopeSm.reset (1.0 - 1.0 / 3.0);
        widthSm.reset (10.0);
        setMode (comp, true);
    }

    void setMode (Mode m, bool force = false) noexcept
    {
        if (m == currentMode && ! force) return;
        currentMode = m;
        cell.fixedBlend = -1.0;
        cell.memKneeDb = 6.0;
        if (m == comp)
        {
            slopeSm.setTarget (1.0 - 1.0 / 3.0);  widthSm.setTarget (10.0);
            cell.attackDepthDb = 6.0;  cell.setTimes (0.010, 0.060, 2.5);
        }
        else
        {
            slopeSm.setTarget (1.0 - 1.0 / 10.0); widthSm.setTarget (4.0);
            cell.attackDepthDb = 6.0;  cell.setTimes (0.006, 0.045, 2.5);
        }
    }

    Mode currentMode = comp;
    double elCoef = 0.0, vuCoef = 0.0, elPower = 0.0, vuStage1 = 0.0, vuStage2 = 0.0, lastReduction = 0.0;

    dsp::OnePoleSmoother driveSm, gainSm, slopeSm, widthSm;
    compdetail::OptoCell cell;
    compdetail::TubeStage tube;
    dsp::TptSvf keyHp;
};

} // namespace aura
