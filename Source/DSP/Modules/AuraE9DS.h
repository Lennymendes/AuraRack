// AuraRack — NextDSP · E9DS
// De-esser à haute précision, conçu pour le DXD (352,8 / 384 kHz) et correct à toute fréquence d'échantillonnage.
//
// NOTE D'ORIGINE : le cahier des charges évoque le « E9DS du lecteur AURA », dont aucune spécification n'est dans
// le dépôt ni dans les documents du projet. Ce module est donc une conception AuraRack à part entière, pas un portage.
//
//   Contrôles : Threshold, Frequency, Width, Range, Release, Mode (Split / Wide), Listen, Lookahead (+ Mix, Bypass du slot).
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
//   - LOOKAHEAD (Off / 0,5 / 1 / 2 ms) : le détecteur voit le signal tel qu'il arrive, l'audio (et la bande) sont retardés :
//     la réduction est déjà en place quand le « s » ou le « t » très bref atteint la sortie. La latence annoncée à l'hôte est
//     exactement ce retard ; le moteur compense le Dry du slot. Sous le seuil la sortie est l'entrée retardée, bit pour bit.
//     Changer de cran fait un fondu de 1,5 ms (sortie → 0 → sortie) autour du changement de retard : pas de clic.
//   - Constantes de temps en secondes (expm1), calcul en double : même réponse à 44,1 kHz et à 384 kHz. Latence 0 sans Lookahead.
//   - getGainReductionDb() = réduction courante ; getMeterAux() = niveau de la bande sibilante (dBFS crête).

#pragma once

#include "CompCommon.h"

namespace aura
{
class AuraE9DS final : public AuraModuleDSP
{
public:
    enum Param { threshold, frequency, width, range, release, mode, listen, lookahead, numParams };
    enum Mode { split = 0, wide = 1 };

    static constexpr double kRatio = 6.0, kKneeDb = 6.0;
    static constexpr double kDetAttackS = 0.0001, kDetReleaseS = 0.004, kGrAttackS = 0.00025;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* modes[]   = { "Split", "Wide" };
        static constexpr const char* listens[] = { "Off", "On" };
        static constexpr const char* looks[]   = { "Off", "0.5 ms", "1 ms", "2 ms" };
        static constexpr ParamSpec specs[] = {
            { "Threshold", "dB", -60.0, 0.0,     -30.0, S::linear },
            { "Frequency", "Hz", 2000.0, 14000.0, 6500.0, S::logarithmic },
            { "Width",     "Q",  0.5,  4.0,      1.4,   S::logarithmic },
            { "Range",     "dB", 0.0,  18.0,     8.0,   S::linear },
            { "Release",   "ms", 10.0, 300.0,    60.0,  S::logarithmic },
            { "Mode",      "",   0.0,  1.0,      0.0,   S::choice, modes, 2 },
            { "Listen",    "",   0.0,  1.0,      0.0,   S::choice, listens, 2 },
            { "Lookahead", "",   0.0,  3.0,      0.0,   S::choice, looks, 4 },
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
            case lookahead: lookIndex = std::clamp ((int) std::lround (v), 0, 3); targetDelay = delayFor (lookIndex); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        band.reset();
        env = 0.0;
        reductionDb = 0.0;
        lastBandDb = -120.0;
        for (auto* b : { &bufL, &bufR, &bandL, &bandR }) std::fill (b->begin(), b->end(), 0.0);
        writePos = 0;
        delay = targetDelay;
        fadeState = 0;
        fadeGain = 1.0;
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

            // Lookahead : l'audio et la bande sont lus `delay` échantillons en arrière, le détecteur a déjà vu le présent.
            bufL[writePos] = L[i]; bufR[writePos] = R[i]; bandL[writePos] = bl; bandR[writePos] = br;
            const std::size_t rp = (writePos - (std::size_t) delay) & mask;
            const double dl = bufL[rp], dr = bufR[rp], dbl = bandL[rp], dbr = bandR[rp];
            writePos = (writePos + 1) & mask;

            double yl, yr;
            if (listenOn) { yl = dbl; yr = dbr; }
            else
            {
                const double g = dsp::dbToGain (-reductionDb);
                if (wideMode) { yl = dl * g; yr = dr * g; }
                else          { yl = dl - (1.0 - g) * dbl; yr = dr - (1.0 - g) * dbr; }
            }

            // Changement de cran : fondu de sortie, changement du retard à zéro, fondu d'entrée.
            if (fadeState != 0 || delay != targetDelay)
            {
                if (fadeState == 0) fadeState = 1;
                if (fadeState == 1)
                {
                    fadeGain -= fadeStep;
                    if (fadeGain <= 0.0) { fadeGain = 0.0; delay = targetDelay; fadeState = 2; }
                }
                else
                {
                    fadeGain += fadeStep;
                    if (fadeGain >= 1.0) { fadeGain = 1.0; fadeState = 0; }
                }
                yl *= fadeGain; yr *= fadeGain;
            }
            L[i] = yl;
            R[i] = yr;
        }
        lastBandDb = dsp::gainToDb (env);
    }

    int getLatencySamples() const noexcept override { return targetDelay; }
    int getMaxLatencySamples() const noexcept override { return delayFor (3); }

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
        fadeStep = 1.0 / std::max (1.0, 0.0015 * sampleRate);
        std::size_t size = 16;
        while ((int) size < delayFor (3) + 2) size <<= 1;
        for (auto* b : { &bufL, &bufR, &bandL, &bandR }) b->assign (size, 0.0);
        mask = size - 1;
        writePos = 0;
        delay = targetDelay = delayFor (lookIndex);
        updateFilter();
    }

    int delayFor (int index) const noexcept
    {
        static constexpr double ms[] = { 0.0, 0.5, 1.0, 2.0 };
        return (int) std::lround (ms[std::clamp (index, 0, 3)] * 0.001 * sampleRate);
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

    int lookIndex = 0, delay = 0, targetDelay = 0, fadeState = 0;
    double fadeGain = 1.0, fadeStep = 0.01;
    std::vector<double> bufL, bufR, bandL, bandR;
    std::size_t writePos = 0, mask = 15;
};

} // namespace aura
