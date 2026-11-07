// AuraRack — NextDSP · AuraComp
// Compresseur VCA / Opto, détecteur Peak ou RMS, genou doux (courbe de Giannoulis, Massberg & Reiss),
// lissage de la réduction de gain dans le domaine dB (attaque / relâchement découplés).
// Mode Opto : relâchement dépendant du programme (plus la compression est forte, plus il est lent).
// Clé : entrée du module ou bus Sidechain.

#pragma once

#include "../AuraModuleDSP.h"

namespace aura
{
class AuraComp final : public AuraModuleDSP
{
public:
    enum Param { threshold, ratio, attack, release, knee, makeup, detector, style, keySource, numParams };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* detectors[] = { "Peak", "RMS" };
        static constexpr const char* styles[]    = { "VCA", "Opto" };
        static constexpr const char* keys[]      = { "Interne", "Sidechain" };
        static constexpr ParamSpec specs[] = {
            { "Threshold", "dB", -60.0, 0.0,    -18.0, S::linear },
            { "Ratio",     ":1", 1.0,   20.0,   4.0,   S::logarithmic },
            { "Attack",    "ms", 0.01,  100.0,  10.0,  S::logarithmic },
            { "Release",   "ms", 5.0,   2000.0, 120.0, S::logarithmic },
            { "Knee",      "dB", 0.0,   24.0,   6.0,   S::linear },
            { "Makeup",    "dB", -12.0, 24.0,   0.0,   S::linear },
            { "Detector",  "",   0.0,   1.0,    0.0,   S::choice, detectors, 2 },
            { "Style",     "",   0.0,   1.0,    0.0,   S::choice, styles, 2 },
            { "Key",       "",   0.0,   1.0,    0.0,   S::choice, keys, 2 },
        };
        static const ModuleInfo i { ModuleType::comp, "AuraComp", "COMP", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case threshold: thresholdDb = v; break;
            case ratio:     ratioValue = std::max (1.0, v); break;
            case attack:    attackMs = v; attackCoef = dsp::timeCoef (v, sampleRate); break;
            case release:   releaseMs = v; releaseCoef = dsp::timeCoef (v, sampleRate); break;
            case knee:      kneeDb = std::max (0.0, v); break;
            case makeup:    makeupSmooth.setTarget (v); break;
            case detector:  useRms = v >= 0.5; break;
            case style:     opto = v >= 0.5; break;
            case keySource: useSidechain = v >= 0.5; break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        envDb = 0.0;
        rmsState = 0.0;
        makeupSmooth.reset (makeupSmooth.getTarget());
    }

    void process (double* L, double* R, int n, const SidechainBlock& key) noexcept override
    {
        const bool external = useSidechain && key.available;
        const double* kL = external ? key.left  : L;
        const double* kR = external ? key.right : R;
        const double optoAttackCoef = dsp::timeCoef (std::max (attackMs, 2.0), sampleRate);

        for (int i = 0; i < n; ++i)
        {
            // Détection liée en stéréo (même gain sur L et R : l'image ne bouge pas).
            double level;
            if (useRms)
            {
                const double sq = 0.5 * (kL[i] * kL[i] + kR[i] * kR[i]);
                rmsState = sq + rmsCoef * (rmsState - sq);
                level = std::sqrt (std::max (rmsState, 0.0));
            }
            else
            {
                level = std::max (std::abs (kL[i]), std::abs (kR[i]));
            }

            const double target = gainReductionFor (dsp::gainToDb (level));

            double aCoef = opto ? optoAttackCoef : attackCoef;
            double rCoef = releaseCoef;
            if (opto)
            {
                // Relâchement à deux temps de l'optocoupleur : rapide à faible réduction, lent à forte réduction.
                const double depth = std::min (1.0, envDb / 10.0);
                rCoef = dsp::timeCoef (releaseMs * (0.4 + 1.6 * depth), sampleRate);
            }
            const double c = target > envDb ? aCoef : rCoef;
            envDb = target + c * (envDb - target);

            const double g = dsp::dbToGain (makeupSmooth.next() - envDb);
            L[i] *= g;
            R[i] *= g;
        }
    }

    double getGainReductionDb() const noexcept override { return envDb; }

    /** Courbe statique : réduction (dB, >= 0) pour un niveau d'entrée en dB. */
    double gainReductionFor (double xDb) const noexcept
    {
        const double over = xDb - thresholdDb;
        const double slope = 1.0 / ratioValue - 1.0;
        double yDb;
        if (kneeDb > 0.0 && 2.0 * std::abs (over) <= kneeDb)
        {
            const double t = over + 0.5 * kneeDb;
            yDb = xDb + slope * t * t / (2.0 * kneeDb);
        }
        else if (over > 0.0)
        {
            yDb = thresholdDb + over / ratioValue;
        }
        else
        {
            yDb = xDb;
        }
        return std::max (0.0, xDb - yDb);
    }

private:
    void prepareInternal() override
    {
        rmsCoef = dsp::timeCoef (10.0, sampleRate);   // fenêtre RMS 10 ms
        makeupSmooth.prepare (sampleRate, 20.0);
    }

    double thresholdDb = -18.0, ratioValue = 4.0, kneeDb = 6.0;
    double attackMs = 10.0, releaseMs = 120.0, attackCoef = 0.0, releaseCoef = 0.0, rmsCoef = 0.0;
    double envDb = 0.0, rmsState = 0.0;
    bool useRms = false, opto = false, useSidechain = false;
    dsp::OnePoleSmoother makeupSmooth;
};

} // namespace aura
