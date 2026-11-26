// AuraRack — NextDSP · briques communes des nouveaux modules (F976, D9X, AuraSculpt, AuraTone)
//
//  HarmonicStage : harmoniques 2 et 3 ADDITIFS, sans repliement à aucune fréquence d'échantillonnage.
//    Le terme non linéaire est calculé sur le signal passé par un passe-bas 4e ordre (fc = min(20 kHz, 0,45·fs/3)) : ses
//    harmoniques 2 et 3 restent sous Nyquist, rien ne se replie (44,1 comme 384 kHz). Le chemin principal reste intact : à
//    coefficients nuls la sortie est l'entrée, bit pour bit. H2 ≈ c2·u/2, H3 ≈ c3·u²/4 pour un signal d'amplitude u·ref (u < 1) ; au-delà, u est borné à 3 : le terme reste fini à tout niveau.
//  SoftKnee : plafond à genou doux (identité jusqu'au genou, puis tanh C¹ vers le plafond), ADAA du 1er ordre sous 176,4 kHz
//    appliqué au seul résidu non linéaire (la zone linéaire reste une identité exacte).
//    Plafond « doux » : sous surcharge extrême (+12 dB) la crête peut dépasser le plafond d'environ 1 dB (mesuré).

#pragma once

#include "CompCommon.h"

namespace aura::compdetail
{
/** Borne douce (|v| < 3) : identité à 1 % près jusqu'à |v| = 0,25, ≈ 0,95·v à |v| = 1. Les termes en v² et v³ restent finis quel que
    soit le niveau d'entrée (un signal flottant de +60 dBFS ne fait pas exploser le cube). */
inline double boundedHarmonicInput (double v) noexcept { return v / std::sqrt (1.0 + v * v * (1.0 / 9.0)); }

struct HarmonicStage
{
    static double bounded (double v) noexcept { return boundedHarmonicInput (v); }

    void prepare (double sampleRate) noexcept
    {
        const double fc = std::min (20000.0, 0.45 * sampleRate / 3.0);
        lp1.setup (dsp::TptSvf::Type::lowPass, fc, 0.70710678118654752, 0.0, sampleRate);
        lp2.setup (dsp::TptSvf::Type::lowPass, fc, 0.70710678118654752, 0.0, sampleRate);
        dcCoef = alpha (1.0 / (2.0 * dsp::kPi * 10.0), sampleRate);
        reset();
    }
    void reset() noexcept { lp1.reset(); lp2.reset(); dc[0] = dc[1] = 0.0; }

    /** x + ref·(−c3·u³ + c2·(u² − moyenne)), u = passe-bas(x)/ref. Retourne x tel quel si c2 = c3 = 0. */
    double process (double x, int ch, double ref, double c2, double c3) noexcept
    {
        if (c2 == 0.0 && c3 == 0.0) return x;
        const double u = bounded (lp2.processSample (lp1.processSample (x, ch), ch) / ref), sq = u * u;
        dc[ch] += dcCoef * (sq - dc[ch]);
        return x + ref * (-c3 * sq * u + c2 * (sq - dc[ch]));
    }

    dsp::TptSvf lp1, lp2;
    double dc[2] { 0.0, 0.0 }, dcCoef = 0.0;
};

struct SoftKnee
{
    void prepare (double sampleRate, double ceilingLin, double kneeLin) noexcept
    {
        adaa = sampleRate < dsp::kOversamplingOffRate;
        ceiling = ceilingLin; knee = std::min (kneeLin, 0.999 * ceilingLin);
        w = ceiling - knee;
        reset();
    }
    void reset() noexcept { xPrev[0] = xPrev[1] = 0.0; pPrev[0] = pPrev[1] = 0.0; }

    double f (double v) const noexcept
    {
        const double a = std::abs (v);
        return a <= knee ? v : std::copysign (knee + w * std::tanh ((a - knee) / w), v);
    }
    /** Résidu non linéaire r(x) = f(x) − x (nul dans la zone linéaire) et sa primitive paire R. */
    double residual (double v) const noexcept { return f (v) - v; }
    double prim (double x) const noexcept
    {
        const double d = std::abs (x) - knee;
        return d <= 0.0 ? 0.0 : w * w * logCosh (d / w) - 0.5 * d * d;
    }
    /** ADAA du 1er ordre appliqué au seul RÉSIDU : y = x + (R(x) − R(x1)) / (x − x1). La zone linéaire est une identité exacte
        (aucun demi-échantillon de retard, aucune perte d'aigus) ; seule la partie non linéaire, source du repliement, est lissée. */
    double process (double x, int ch) noexcept
    {
        double y;
        if (! adaa || (std::abs (x) <= knee && std::abs (xPrev[ch]) <= knee))
            y = f (x);
        else
        {
            const double p = prim (x), dx = x - xPrev[ch];
            y = x + (std::abs (dx) > 1.0e-9 ? (p - pPrev[ch]) / dx : residual (0.5 * (x + xPrev[ch])));
        }
        xPrev[ch] = x; pPrev[ch] = prim (x);
        // Pas de rognage final (il ajouterait du repliement) : le résidu lissé, retardé d'un demi-échantillon, peut dépasser
        // le plafond d'environ 1 dB sous surcharge extrême (mesuré : +0,9 dB à +12 dB de surcharge).
        return y;
    }

    bool adaa = true;
    double ceiling = 1.0, knee = 0.7, w = 0.3, xPrev[2] { 0.0, 0.0 }, pPrev[2] { 0.0, 0.0 };
};
} // namespace aura::compdetail
