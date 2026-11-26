// AuraRack — NextDSP · D9X
// Compresseur / « distorteur » à 8 rapports, inspiré des Distressor (EL8-X), version DXD (352,8 / 384 kHz, double 64 bits).
// Conception AuraRack : constantes calibrées par mesure, pas par comparaison avec le matériel.
//
//   Contrôles : Input (0 … +30 dB), Ratio (1:1 · 2:1 · 3:1 · 4:1 · 6:1 · 10:1 Opto · 20:1 · NUKE), Attack (0,05 … 30 ms),
//   Release (50 ms … 3 s), Distortion (Clean / Dist 2 / Dist 3), Detector HPF (Off / 60 / 120 / 240 Hz), Band Emphasis
//   (0 … +12 dB à 6 kHz, détecteur seulement), British (Off / On), Auto Focus (Off / 3 / 6 / 9 dB de réduction moyenne visée),
//   Output (−24 … +24 dB). Le Mix et le Bypass sont ceux du slot.
//
//   ÉTALONNAGE : 0 dBu = −18 dBFS. SEUIL FIXE −18 dBFS à l'entrée de la cellule : Input pousse le signal (donc la compression),
//   Output règle le niveau. Auto Focus (FocusTracker, comme le W9BMK1) déplace lentement le seuil pour viser la réduction moyenne.
//
//   Chaîne : Input ─► cellule (détecteur de crête : HPF + Band Emphasis, liaison stéréo = max) ─► étage de distorsion
//            (Dist 2 / Dist 3 : additif, sans repliement, plus profond avec le niveau et la réduction) ─► Output.
//
//   Rapports (chacun avec son genou et ses constantes de temps) :
//     1:1  aucune compression : chemin analogique + modes de distorsion seulement ;
//     2:1 (genou 10 dB, plus lent), 3:1 (8 dB), 4:1 (6 dB), 6:1 (4 dB), 20:1 (2 dB, plus rapide) : courbes à genou doux ;
//     10:1 Opto : courbe « optique » (rapport croissant de 1:1 vers 10:1 sans coude) et relâchement lent à deux voies
//                 dépendant du programme (cellule OptoCell) ;
//     NUKE : écrêtage de crête (rapport ∞, genou 0, attaque ≤ 10 µs, pas d'anticipation : une transitoire instantanée peut
//            dépasser de quelques dB pendant ≈ 0,1 ms, annoncé honnêtement).
//   British : courbe plus agressive à tous les rapports (rapport effectif × (1 + 0,08·dépassement), attaque × 0,5, relâchement
//   × 0,6, genou ÷ 2, coloration × 1,3).
//   Distorsion : Dist 2 = harmonique 2 (chaleur « lampe », asymétrique) ; Dist 3 = harmonique 3 (compression « bande »).
//   Les lampes THD 1 % / 3 % lisent la distorsion harmonique de l'étage pour la crête courante (H2 = c2·u/2, H3 = c3·u²/4, mêmes
//   coefficients que le traitement ; vérifiée par mesure à ±40 %).
//   Latence 0.

#pragma once

#include "ColourStage.h"
#include "FocusTracker.h"

namespace aura
{
class AuraD9X final : public AuraModuleDSP
{
public:
    enum Param { input, ratio, attack, release, distortion, detHpf, bandEmphasis, british, autoFocus, output, numParams };
    enum Ratio { r1, r2, r3, r4, r6, r10Opto, r20, nuke };

    static constexpr double kThresholdDbfs = -18.0;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* ratios[] = { "1:1", "2:1", "3:1", "4:1", "6:1", "10:1 Opto", "20:1", "NUKE" };
        static constexpr const char* dists[]  = { "Clean", "Dist 2", "Dist 3" };
        static constexpr const char* hpfs[]   = { "Off", "60 Hz", "120 Hz", "240 Hz" };
        static constexpr const char* brits[]  = { "Off", "On" };
        static constexpr const char* autos[]  = { "Off", "3 dB", "6 dB", "9 dB" };
        static constexpr ParamSpec specs[] = {
            { "Input",     "dB",  0.0, 30.0, 6.0, S::linear },
            { "Ratio",     "",    0.0, 7.0, 3.0, S::choice, ratios, 8 },
            { "Attack",    "ms",  0.05, 30.0, 2.0, S::logarithmic },
            { "Release",   "ms",  50.0, 3000.0, 300.0, S::logarithmic },
            { "Distortion", "",   0.0, 2.0, 0.0, S::choice, dists, 3 },
            { "Detector HPF", "", 0.0, 3.0, 0.0, S::choice, hpfs, 4 },
            { "Band Emphasis", "dB", 0.0, 12.0, 0.0, S::linear },
            { "British",   "",    0.0, 1.0, 0.0, S::choice, brits, 2 },
            { "Auto Focus", "",   0.0, 3.0, 0.0, S::choice, autos, 4 },
            { "Output",    "dB",  -24.0, 24.0, 0.0, S::linear },
        };
        static const ModuleInfo i { ModuleType::d9x, "D9X", "D9X", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    struct Curve { double ratio, knee, attackScale, releaseScale; bool opto; };
    static Curve curveFor (int r) noexcept
    {
        switch (r)
        {
            case r1:     return { 1.0, 0.0, 1.0, 1.0, false };
            case r2:     return { 2.0, 10.0, 1.3, 1.2, false };
            case r3:     return { 3.0, 8.0, 1.15, 1.1, false };
            case r4:     return { 4.0, 6.0, 1.0, 1.0, false };
            case r6:     return { 6.0, 4.0, 0.9, 0.95, false };
            case r10Opto: return { 10.0, 0.0, 1.4, 1.0, true };
            case r20:    return { 20.0, 2.0, 0.8, 0.9, false };
            default:     return { 1.0e6, 0.0, 0.2, 0.7, false };   // NUKE
        }
    }

    /** Réduction statique (dB) à un niveau de cellule donné (dBFS), seuil `thr`, hors temps : utilisée par les tests. */
    static double staticReduction (double levelDb, double thr, int r, bool brit) noexcept
    {
        const auto c = curveFor (r);
        if (r == r1) return 0.0;
        const double o = std::max (0.0, levelDb - thr);
        if (c.opto) return optoReduction (o);
        const double ratioEff = brit ? c.ratio * (1.0 + 0.08 * std::min (o, 25.0)) : c.ratio;
        return compdetail::kneeReduction (levelDb, thr, ratioEff, brit ? 0.5 * c.knee : c.knee);
    }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case input:        inGain.setTarget (dsp::dbToGain (v)); break;
            case ratio:        ratioIndex = std::clamp ((int) std::lround (v), 0, 7); updateTimes(); break;
            case attack:       attackMs = v; updateTimes(); break;
            case release:      releaseMs = v; updateTimes(); break;
            case distortion:   distMode = std::clamp ((int) std::lround (v), 0, 2); break;
            case detHpf:       { const int i = std::clamp ((int) std::lround (v), 0, 3); static constexpr double f[] = { 0.0, 60.0, 120.0, 240.0 };
                                 if (i > 0) hp.setup (dsp::TptSvf::Type::highPass, f[i], 0.70710678118654752, 0.0, sampleRate);
                                 hpOn = i > 0; break; }
            case bandEmphasis: emphDb = v; if (v > 0.0) emph.setup (dsp::TptSvf::Type::bell, 6000.0, 0.8, v, sampleRate); break;
            case british:      brit = v >= 0.5; updateTimes(); break;
            case autoFocus:    { const int i = std::clamp ((int) std::lround (v), 0, 3);
                                 const bool on = i > 0;
                                 if (on) tracker.setTargetReduction (3.0 * i);
                                 if (on && ! autoOn) tracker.reset (kThresholdDbfs);
                                 autoOn = on; break; }
            case output:       outGain.setTarget (dsp::dbToGain (v)); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        inGain.reset (inGain.getTarget()); outGain.reset (outGain.getTarget());
        hp.reset(); emph.reset(); colour.reset(); opto.reset(); tracker.reset (kThresholdDbfs);
        env = 0.0; reduction = 0.0; red = 0.0; gainSm = 1.0; distSig = 0.0; thdEnv = 0.0; thdPct = 0.0; statusPeak = 0.0;
        thresholdNow = kThresholdDbfs;
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        const auto c = curveFor (ratioIndex);
        const bool comp = ratioIndex != r1;
        const double knee = brit ? 0.5 * c.knee : c.knee;
        const double colourBoost = brit ? 1.3 : 1.0;
        for (int i = 0; i < n; ++i)
        {
            const double gi = inGain.next(), go = outGain.next();
            double xl = L[i] * gi, xr = R[i] * gi;

            double red2 = 0.0;
            if (comp)
            {
                double kl = xl, kr = xr;
                if (hpOn) { kl = hp.processSample (xl, 0); kr = hp.processSample (xr, 1); }
                if (emphDb > 0.0) { kl = emph.processSample (kl, 0); kr = emph.processSample (kr, 1); }
                env = std::max ({ std::abs (kl), std::abs (kr), env * peakDecay });
                const double levelDb = env > 1.0e-9 ? dsp::gainToDb (env) : -180.0;
                if (autoOn)
                {
                    tracker.process (levelDb, red, c.opto ? 4.0 : std::min (c.ratio, 30.0), knee);
                    thresholdNow = tracker.getThreshold();
                }
                else
                    thresholdNow = kThresholdDbfs;
                double target = 0.0;
                if (levelDb > thresholdNow - 0.5 * knee - 1.0e-9)
                    target = staticReduction (levelDb, thresholdNow, ratioIndex, brit);
                red = c.opto ? opto.next (target) : cellStep (target);
                const double gTarget = red > 1.0e-9 ? dsp::dbToGain (-red) : 1.0;
                gainSm += gainCoef * (gTarget - gainSm);
                red2 = red;
                xl *= gainSm; xr *= gainSm;
            }
            else
                red = 0.0;

            // Étage de distorsion (additif, sans repliement) : plus profond avec le niveau (u = x/ref) et la réduction.
            double yl = xl, yr = xr;
            thdEnv = std::max ({ std::abs (xl), std::abs (xr), thdEnv * thdDecay });
            double c2 = 0.0, c3 = 0.0;
            if (distMode != 0)
            {
                const double depth = (1.0 + 0.15 * red2) * colourBoost;
                c2 = distMode == 1 ? kC2 * depth : 0.12 * kC2 * depth;
                c3 = distMode == 2 ? kC3 * depth : 0.10 * kC3 * depth;
                yl = colour.process (xl, 0, kRef, c2, c3);
                yr = colour.process (xr, 1, kRef, c2, c3);
            }
            // Lampes THD : distorsion harmonique de l'étage pour la crête courante (H2 = c2·u/2, H3 = c3·u²/4), lissée 0,15 s.
            {
                const double u = thdEnv / kRef, h2 = 0.5 * c2 * u, h3 = 0.25 * c3 * u * u;
                distSig += thdCoef * (100.0 * std::sqrt (h2 * h2 + h3 * h3) - distSig);
            }
            yl *= go; yr *= go;
            L[i] = yl; R[i] = yr;
            statusPeak = std::max (statusPeak, std::max (std::abs (yl), std::abs (yr)));
        }
        lastReduction = red;
        thdPct = distSig;
    }

    double getGainReductionDb() const noexcept override { return lastReduction; }
    double takeStatusPeak() noexcept override { const double p = statusPeak; statusPeak = 0.0; return p; }
    /** Distorsion harmonique ajoutée, en % (lampes 1 % et 3 % de la façade). */
    double getMeterAux() const noexcept override { return thdPct; }
    double getThresholdDb() const noexcept { return thresholdNow; }

private:
    static constexpr double kRef = 0.3, kC2 = 0.08, kC3 = 0.12;   // −10,5 dBFS : H2 ≈ 4 %, H3 ≈ 3 % à u = 1 (profondeur 1)

    /** Courbe « optique » : rapport 1:1 au seuil, croissant vers 10:1 sans coude. */
    static double optoReduction (double o) noexcept
    {
        constexpr double w = 8.0, s = 0.9;   // pente finale 0,9 = rapport 10:1
        return o > 0.0 ? s * (o - w * std::atan (o / w)) * 1.0 : 0.0;
    }

    double cellStep (double target) noexcept
    {
        const double aCoef = target > reduction ? aAtt : aRel;
        reduction += aCoef * (target - reduction);
        if (target <= 0.0 && reduction < 1.0e-12) reduction = 0.0;
        return reduction;
    }

    void prepareInternal() override
    {
        inGain.prepare (sampleRate, 20.0); outGain.prepare (sampleRate, 20.0);
        colour.prepare (sampleRate);
        hp.setup (dsp::TptSvf::Type::highPass, 60.0, 0.70710678118654752, 0.0, sampleRate);
        emph.setup (dsp::TptSvf::Type::bell, 6000.0, 0.8, 6.0, sampleRate);
        peakDecay = std::exp (-1.0 / (0.002 * sampleRate));
        thdCoef = compdetail::alpha (0.15, sampleRate);
        thdDecay = std::exp (-1.0 / (0.005 * sampleRate));
        tracker.prepare (sampleRate);
        opto.prepare (sampleRate);
        opto.attackDepthDb = 1.0e9;
        opto.memKneeDb = 2.5;
        updateTimes();
    }

    void updateTimes() noexcept
    {
        const auto c = curveFor (ratioIndex);
        const double bAtt = brit ? 0.5 : 1.0, bRel = brit ? 0.6 : 1.0;
        double att = attackMs * 1.0e-3 * c.attackScale * bAtt, rel = releaseMs * 1.0e-3 * c.releaseScale * bRel;
        if (ratioIndex == nuke) att = std::min (att, 10.0e-6);
        aAtt = compdetail::alpha (std::max (att, 1.0e-6), sampleRate);
        aRel = compdetail::alpha (rel, sampleRate);
        opto.setTimes (std::max (att, 1.0e-6), 0.4 * rel, 8.0 * rel);   // voie rapide 0,4 × Release, voie lente 8 × Release (LA-2A : queue de plusieurs secondes)
        gainCoef = compdetail::alpha (std::max (std::min (att, 1.0e-3) / 3.0, 2.0e-6), sampleRate);
    }

    int ratioIndex = r4, distMode = 0;
    bool brit = false, autoOn = false, hpOn = false;
    double attackMs = 2.0, releaseMs = 300.0, emphDb = 0.0;
    double env = 0.0, reduction = 0.0, red = 0.0, lastReduction = 0.0, gainSm = 1.0, aAtt = 0.5, aRel = 0.001, gainCoef = 0.5;
    double peakDecay = 0.0, thdDecay = 0.0, thdEnv = 0.0, thdCoef = 0.0, distSig = 0.0, thdPct = 0.0, statusPeak = 0.0, thresholdNow = kThresholdDbfs;

    dsp::OnePoleSmoother inGain, outGain;
    dsp::TptSvf hp, emph;
    compdetail::HarmonicStage colour;
    compdetail::OptoCell opto;
    compdetail::FocusTracker tracker;
};

} // namespace aura
