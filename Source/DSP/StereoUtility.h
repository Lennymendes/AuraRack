// AuraRack — NextDSP · utilitaire Stéréo / Largeur / Mono grave (fin de rack)
//
// Travaille en Mid / Side : M = (L+R)/2, S = (L−R)/2.
//  - Width  0…200 % : gain du Side (0 % = mono, 100 % = inchangé, 200 % = Side doublé) ;
//  - Bass Mono : le Side passe par un passe-haut Linkwitz-Riley 4e ordre (2 × Butterworth TPT) : tout ce qui est sous la
//    fréquence choisie (60 / 100 / 150 / 250 Hz) devient mono, le Mid n'est PAS touché (aucune coloration de phase sur le centre) ;
//  - Mono : Side = 0 (contrôle de compatibilité mono).
// Tous les changements sont lissés (20 ms). Width = 100 %, Bass Mono Off, Mono Off : sortie bit-identique (rien n'est calculé).

#pragma once

#include "AuraDspCore.h"

namespace aura::dsp
{
class StereoUtility
{
public:
    static constexpr int kNumBassSettings = 5;
    static constexpr double kBassHz[kNumBassSettings] = { 0.0, 60.0, 100.0, 150.0, 250.0 };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        sideScale.prepare (sampleRate, 20.0);
        blend.prepare (sampleRate, 20.0);
        sideScale.reset (1.0);
        blend.reset (0.0);
        cutoffIndex = -1;
        hp1.reset(); hp2.reset();
    }

    void reset() noexcept { hp1.reset(); hp2.reset(); }

    /** widthPercent 0…200, bassIndex 0…4 (0 = Off), mono. À appeler une fois par tranche. */
    void setParameters (double widthPercent, int bassIndex, bool mono) noexcept
    {
        double w = std::clamp (widthPercent, 0.0, 200.0) / 100.0;
        if (std::abs (w - 1.0) < 1.0e-5) w = 1.0;   // les arrondis float de l'hôte ne cassent pas la transparence
        sideScale.setTarget (mono ? 0.0 : w);
        bassIndex = std::clamp (bassIndex, 0, kNumBassSettings - 1);
        blend.setTarget (bassIndex > 0 ? 1.0 : 0.0);
        if (bassIndex > 0 && bassIndex != cutoffIndex)
        {
            cutoffIndex = bassIndex;
            hp1.setup (TptSvf::Type::highPass, kBassHz[bassIndex], 0.70710678118654752, 0.0, sr);
            hp2.setup (TptSvf::Type::highPass, kBassHz[bassIndex], 0.70710678118654752, 0.0, sr);
        }
    }

    bool isIdentity() const noexcept
    {
        return sideScale.getTarget() == 1.0 && ! sideScale.isSmoothing() && blend.getTarget() == 0.0 && ! blend.isSmoothing();
    }

    void process (double* L, double* R, int n) noexcept
    {
        if (isIdentity())
        {
            hp1.reset(); hp2.reset();   // repart d'un état propre quand le mono grave est réactivé
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const double m = 0.5 * (L[i] + R[i]);
            double s = 0.5 * (L[i] - R[i]);
            const double b = blend.next();
            if (b > 0.0 || blend.getTarget() > 0.0)
            {
                const double sh = hp2.processSample (hp1.processSample (s, 0), 0);
                s += b * (sh - s);
            }
            s *= sideScale.next();
            L[i] = m + s;
            R[i] = m - s;
        }
    }

private:
    double sr = 48000.0;
    OnePoleSmoother sideScale, blend;
    TptSvf hp1, hp2;
    int cutoffIndex = -1;
};

} // namespace aura::dsp
