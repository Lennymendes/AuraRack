// AuraRack — NextDSP · E9DS
// De-esser à haute précision, conçu pour le DXD (352,8 / 384 kHz) et correct à toute fréquence d'échantillonnage.
//
// NOTE D'ORIGINE : le cahier des charges évoque le « E9DS du lecteur AURA », dont aucune spécification n'est dans
// le dépôt ni dans les documents du projet. Ce module est donc une conception AuraRack à part entière, pas un portage.
//
//   Contrôles : Threshold, Frequency, Width, Range, Release, Mode (Split / Wide), Listen (+ Mix, Bypass du slot).
//
//   Chaîne : entrée ─► bande sibilante (passe-bande TPT à Q réglable, liée en stéréo) ─► enveloppe crête (attaque
//            ≈ 0,1 ms, relâchement 4 ms) ─► courbe statique (6:1, genou 6 dB, saturation douce à « Range »)
//            ─► lissage dans le domaine dB (attaque 0,25 ms, relâchement réglable) ─► gain.
//
//   - SPLIT (défaut) : seule la bande détectée est atténuée, y = x − (1 − g)·bande. Le passe-bande étant de phase nulle
//     à sa fréquence centrale, c'est une cloche dynamique : les graves, le corps et l'air hors bande restent intacts.
//     C'est le mode qui évite le zozotement et préserve le timbre.
//   - WIDE : gain appliqué à tout le signal (pour un bus qui s'agresse dans tout le haut du spectre).
//   - LISTEN : la sortie est la bande de détection (pour caler Frequency / Width à l'oreille).
//   - Range : réduction maximale (dB). Le gain de réduction ne dépasse jamais cette valeur : pas de trou dans le signal.
//   - Constantes de temps en secondes (expm1), calcul en double : même réponse à 44,1 kHz et à 384 kHz. Latence 0.
//   - getGainReductionDb() = réduction courante ; getMeterAux() = niveau de la bande sibilante (dBFS crête).

#pragma once

#include "CompCommon.h"

namespace aura
{
class AuraE9DS final : public AuraModuleDSP
{
public:
    enum Param { threshold, frequency, width, range, release, mode, listen, numParams };
    enum Mode { split = 0, wide = 1 };

    static constexpr double kRatio = 6.0, kKneeDb = 6.0;
    static constexpr double kDetAttackS = 0.0001, kDetReleaseS = 0.004, kGrAttackS = 0.00025;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* modes[]   = { "Split", "Wide" };
        static constexpr const char* listens[] = { "Off", "On" };
        static constexpr ParamSpec specs[] = {
            { "Threshold", "dB", -60.0, 0.0,     -30.0, S::linear },
            { "Frequency", "Hz", 2000.0, 14000.0, 6500.0, S::logarithmic },
            { "Width",     "Q",  0.5,  4.0,      1.4,   S::logarithmic },
            { "Range",     "dB", 0.0,  18.0,     8.0,   S::linear },
            { "Release",   "ms", 10.0, 300.0,    60.0,  S::logarithmic },
            { "Mode",      "",   0.0,  1.0,      0.0,   S::choice, modes, 2 },
            { "Listen",    "",   0.0,  1.0,      0.0,   S::choice, listens, 2 },
        };
        static const ModuleInfo i { ModuleType::e9ds, "E9DS", "E9DS", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case threshold: thresholdDb = v; break;
            case frequency: freqHz = v; updateFilter(); break;
            case width:     q = v; updateFilter(); break;
            case range:     rangeDb = v; break;
            case release:   grRelease = compdetail::alpha (v * 0.001, sampleRate); break;
            case mode:      wideMode = v >= 0.5; break;
            case listen:    listenOn = v >= 0.5; break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        band.reset();
        env = 0.0;
        reductionDb = 0.0;
        lastBandDb = -120.0;
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const double bl = band.processSample (L[i], 0), br = band.processSample (R[i], 1);
            const double a = std::max (std::abs (bl), std::abs (br));
            env += (a > env ? detAttack : detRelease) * (a - env);
            if (env < 1.0e-30) env = 0.0;
            const double xDb = dsp::gainToDb (env);

            double target = compdetail::kneeReduction (xDb, thresholdDb, kRatio, kKneeDb);
            if (rangeDb > 0.0) target = rangeDb * std::tanh (target / rangeDb); else target = 0.0;
            reductionDb += (target > reductionDb ? grAttack : grRelease) * (target - reductionDb);
            if (reductionDb < 1.0e-9) reductionDb = 0.0;

            if (listenOn) { L[i] = bl; R[i] = br; continue; }

            const double g = dsp::dbToGain (-reductionDb);
            if (wideMode) { L[i] *= g; R[i] *= g; }
            else          { L[i] -= (1.0 - g) * bl; R[i] -= (1.0 - g) * br; }
        }
        lastBandDb = dsp::gainToDb (env);
    }

    double getGainReductionDb() const noexcept override { return reductionDb; }
    double getMeterAux() const noexcept override { return std::max (lastBandDb, -120.0); }

    /** Réduction statique (dB) pour un niveau de bande donné — utilisée par les tests. */
    double staticReductionFor (double bandDb) const noexcept
    {
        const double t = compdetail::kneeReduction (bandDb, thresholdDb, kRatio, kKneeDb);
        return rangeDb > 0.0 ? rangeDb * std::tanh (t / rangeDb) : 0.0;
    }

private:
    void prepareInternal() override
    {
        detAttack  = compdetail::alpha (kDetAttackS, sampleRate);
        detRelease = compdetail::alpha (kDetReleaseS, sampleRate);
        grAttack   = compdetail::alpha (kGrAttackS, sampleRate);
        grRelease  = compdetail::alpha (0.060, sampleRate);
        updateFilter();
    }

    void updateFilter() noexcept
    {
        band.setup (dsp::TptSvf::Type::bandPass, freqHz, q, 0.0, sampleRate);   // gain unité et phase nulle au centre
    }

    double thresholdDb = -30.0, freqHz = 6500.0, q = 1.4, rangeDb = 8.0;
    bool wideMode = false, listenOn = false;
    double detAttack = 0.0, detRelease = 0.0, grAttack = 0.0, grRelease = 0.0;
    double env = 0.0, reductionDb = 0.0, lastBandDb = -120.0;
    dsp::TptSvf band;
};

} // namespace aura
