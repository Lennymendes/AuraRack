// AuraRack — NextDSP · compensation de gain automatique (auto-gain)
//
// Garde le niveau perçu : le niveau RMS en sortie d'un module (ou du rack) est ramené, lentement, à celui de son entrée.
// Mesure : puissance moyenne (RMS, pas LUFS) sur les deux voies, constante de temps 0,4 s ; correction lissée sur 1 s,
// limitée à ±12 dB, appliquée par rampe linéaire sur chaque tranche (aucun saut). Porte à -60 dBFS : sur un silence
// ou un creux la correction est GELÉE (pas de pompage ni de remontée du bruit). Le gain acquis est conservé quand le
// module est contourné (bypass) ; seul le retrait de l'option ramène le gain à 0 dB. Option désactivée : le signal n'est
// pas touché (aucune multiplication) et rien n'est mesuré (coût nul).
//
// Usage : feedInput() sur l'entrée du module, puis processOutput() sur sa sortie (en place).

#pragma once

#include "AuraDspCore.h"

namespace aura::dsp
{
class LevelMatcher
{
public:
    static constexpr double kMaxDb = 12.0, kGatePower = 1.0e-6, kOutGatePower = 1.0e-9;   // -60 dBFS RMS, -90 dBFS RMS
    static constexpr double kMeasureSeconds = 0.4, kCorrectionSeconds = 1.0;

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        powerCoef = 1.0 - std::exp (-1.0 / (kMeasureSeconds * sampleRate));
        reset (0.0);
    }

    /** Remet mesure ET gain (gainDb = 0 : neutre). */
    void reset (double gainDb) noexcept
    {
        pIn = pOut = 0.0;
        correctionDb = gainDb;
        applied = std::abs (gainDb) < 1.0e-9 ? 1.0 : dbToGain (gainDb);
    }

    /** Remet la mesure à zéro en gardant le gain acquis (arrêt / reprise de lecture). */
    void resetMeasurement() noexcept { pIn = pOut = 0.0; }

    double getCorrectionDb() const noexcept { return correctionDb; }
    bool isNeutral() const noexcept { return applied == 1.0; }

    void feedInput (const double* L, const double* R, int n, bool enabled) noexcept
    {
        if (! enabled) return;
        double p = pIn;
        for (int i = 0; i < n; ++i)
            p += powerCoef * (0.5 * (L[i] * L[i] + R[i] * R[i]) - p);
        pIn = p;
    }

    void processOutput (double* L, double* R, int n, bool enabled) noexcept
    {
        if (! enabled)
        {
            if (applied == 1.0) return;
            // Retrait de l'option : retour à 0 dB par rampe sur la tranche.
            ramp (L, R, n, applied, 1.0);
            applied = 1.0;
            correctionDb = 0.0;
            return;
        }

        double p = pOut;
        for (int i = 0; i < n; ++i)
            p += powerCoef * (0.5 * (L[i] * L[i] + R[i] * R[i]) - p);
        pOut = p;

        if (pIn > kGatePower && pOut > kOutGatePower)
        {
            const double target = std::clamp (10.0 * std::log10 (pIn / pOut), -kMaxDb, kMaxDb);
            // Le gain est déjà appliqué à la sortie mesurée dans les tranches précédentes ? Non : on mesure AVANT le gain.
            // La cible est donc le gain total nécessaire ; on avance vers elle avec la constante de temps de correction.
            correctionDb += (target - correctionDb) * (1.0 - std::exp (-(double) n / (kCorrectionSeconds * sr)));
        }
        const double next = std::abs (correctionDb) < 1.0e-9 ? 1.0 : dbToGain (correctionDb);
        if (next == applied && applied == 1.0) return;
        ramp (L, R, n, applied, next);
        applied = next;
    }

private:
    static void ramp (double* L, double* R, int n, double g0, double g1) noexcept
    {
        const double step = (g1 - g0) / (double) std::max (1, n);
        double g = g0;
        for (int i = 0; i < n; ++i)
        {
            g += step;
            L[i] *= g;
            R[i] *= g;
        }
    }

    double sr = 48000.0, powerCoef = 0.0;
    double pIn = 0.0, pOut = 0.0;
    double correctionDb = 0.0, applied = 1.0;
};

} // namespace aura::dsp
