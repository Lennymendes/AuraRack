// AuraRack — NextDSP · briques communes des compresseurs (AuraComp, W9BMK1)
// Enveloppes en double, constantes de temps en secondes (expm1), courbes analytiques lisses.

#pragma once

#include "../AuraModuleDSP.h"

namespace aura
{
namespace compdetail
{
/** 1 - exp(-1/(τ·fs)) sans annulation (τ en secondes). */
inline double alpha (double tauSeconds, double sampleRate) noexcept
{
    return -std::expm1 (-1.0 / (tauSeconds * sampleRate));
}

/** ln(1 + e^z), stable pour tout z. */
inline double softplus (double z) noexcept
{
    return z > 0.0 ? z + std::log1p (std::exp (-z)) : std::log1p (std::exp (z));
}

/** ln(cosh(u)), stable pour tout u. */
inline double logCosh (double u) noexcept
{
    const double a = std::abs (u);
    return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
}

/** Réduction de gain statique (dB, >= 0), genou quadratique de Giannoulis, Massberg & Reiss. */
inline double kneeReduction (double xDb, double thresholdDb, double ratio, double kneeDb) noexcept
{
    const double over = xDb - thresholdDb;
    const double slope = 1.0 / ratio - 1.0;
    double yDb;
    if (kneeDb > 0.0 && 2.0 * std::abs (over) <= kneeDb)
    {
        const double t = over + 0.5 * kneeDb;
        yDb = xDb + slope * t * t / (2.0 * kneeDb);
    }
    else if (over > 0.0)
        yDb = thresholdDb + over / ratio;
    else
        yDb = xDb;
    return std::max (0.0, xDb - yDb);
}

/**
    Cellule optique : réduction de gain en dB, deux voies (rapide / lente) et une mémoire.
    Le relâchement dépend de l'histoire : après une compression brève il suit la voie rapide,
    après une compression soutenue il se rapproche de la voie lente (plusieurs secondes).
    L'attaque accélère quand la cible est profonde (la cellule s'allume plus vite).
*/
struct OptoCell
{
    double tauAttack = 0.010, tauFast = 0.060, tauSlow = 2.5;   // secondes
    double attackDepthDb = 6.0;     // profondeur qui divise l'attaque par 2
    double memKneeDb = 6.0;         // mémoire (dB) pour laquelle la voie lente pèse 50 %
    double fixedBlend = -1.0;       // >= 0 : poids fixe de la voie lente (L-Comp)

    void prepare (double sr) noexcept { sampleRate = sr; update(); }

    void setTimes (double attackSec, double fastSec, double slowSec) noexcept
    {
        tauAttack = attackSec; tauFast = fastSec; tauSlow = slowSec;
        update();
    }

    void reset() noexcept { fast = slow = mem = 0.0; }

    double next (double target) noexcept
    {
        if (target > fast || target > slow)
        {
            const double a = compdetail::alpha (tauAttack / (1.0 + target / attackDepthDb), sampleRate);
            if (target > fast) fast += a * (target - fast); else fast += aFast * (target - fast);
            if (target > slow) slow += a * (target - slow); else slow += aSlow * (target - slow);
        }
        else
        {
            fast += aFast * (target - fast);
            slow += aSlow * (target - slow);
        }
        mem += (fast > mem ? aMemUp : aMemDown) * (fast - mem);

        if (target < 1.0e-15)
        {
            if (fast < 1.0e-15) fast = 0.0;
            if (slow < 1.0e-15) slow = 0.0;
            if (mem  < 1.0e-15) mem  = 0.0;
        }

        const double w = fixedBlend >= 0.0 ? fixedBlend : mem / (mem + memKneeDb);
        return (1.0 - w) * fast + w * slow;
    }

private:
    void update() noexcept
    {
        aFast    = compdetail::alpha (tauFast, sampleRate);
        aSlow    = compdetail::alpha (tauSlow, sampleRate);
        aMemUp   = compdetail::alpha (4.0, sampleRate);
        aMemDown = compdetail::alpha (6.0, sampleRate);
    }

    double sampleRate = 48000.0;
    double fast = 0.0, slow = 0.0, mem = 0.0;
    double aFast = 0.0, aSlow = 0.0, aMemUp = 0.0, aMemDown = 0.0;
};

/**
    Étage « lampe » : tanh asymétrique à gain petit signal unitaire.
    Sous 176,4 kHz : ADAA du 1er ordre (retire le repliement, coûte un demi-échantillon de retard et un léger
    affaiblissement des aigus). Au-dessus (DXD) : évaluation directe, sans retard ni affaiblissement — les
    harmoniques de cette courbe douce tombent bien avant le repliement, donc l'ADAA n'apporte plus rien.
*/
struct TubeStage
{
    static constexpr double a = 0.3, b = 0.12;

    void prepare (double sampleRate) noexcept
    {
        direct = sampleRate >= dsp::kOversamplingOffRate;
        dcR = std::exp (-2.0 * dsp::kPi * 5.0 / sampleRate);
        k0 = a * (1.0 - std::tanh (b) * std::tanh (b));
        tb = std::tanh (b);
    }
    void reset() noexcept { for (auto& v : x1) v = 0.0; for (auto& v : dcX) v = 0.0; for (auto& v : dcY) v = 0.0; }

    double process (double x, int ch) noexcept
    {
        const auto c = (std::size_t) ch;
        double y;
        if (direct)
            y = (std::tanh (a * x + b) - tb) / k0;
        else
        {
            const double dx = x - x1[c];
            if (std::abs (dx) > 1.0e-9)
                y = (primitive (x) - primitive (x1[c])) / dx;
            else
                y = (std::tanh (a * (0.5 * (x + x1[c])) + b) - tb) / k0;
            x1[c] = x;
        }
        // Blocage du continu (5 Hz) : l'asymétrie crée un décalage dépendant du niveau.
        const double out = y - dcX[c] + dcR * dcY[c];
        dcX[c] = y; dcY[c] = out;
        return out;
    }

private:
    double primitive (double x) const noexcept { return (compdetail::logCosh (a * x + b) / a - tb * x) / k0; }

    bool direct = false;
    double dcR = 0.0, k0 = 1.0, tb = 0.0;
    double x1[2] {}, dcX[2] {}, dcY[2] {};
};

/**
    Étage de saturation douce à gain petit signal unitaire et à « raideur » réglable : y = (tanh(a·x + b) − tanh b) / (a·(1 − tanh² b)).
    `a` petit = presque linéaire (plafond ≈ 1/a), `a` grand = harmoniques plus présents ; `b` donne un peu d'harmonique 2 (étage discret).
    Mêmes règles que TubeStage : évaluation directe en DXD, ADAA du 1er ordre sous 176,4 kHz, blocage du continu à 5 Hz.
    `a` peut varier échantillon par échantillon (la primitive utilise la valeur courante des deux côtés : écart négligeable
    pour une variation lente).
*/
struct SatStage
{
    void prepare (double sampleRate, double drive, double bias) noexcept
    {
        direct = sampleRate >= dsp::kOversamplingOffRate;
        dcR = std::exp (-2.0 * dsp::kPi * 5.0 / sampleRate);
        b = bias;
        tb = std::tanh (b);
        setDrive (drive);
    }
    void setDrive (double newA) noexcept { a = std::max (newA, 1.0e-3); k0 = a * (1.0 - tb * tb); }
    void reset() noexcept { for (auto& v : x1) v = 0.0; for (auto& v : dcX) v = 0.0; for (auto& v : dcY) v = 0.0; }

    double process (double x, int ch) noexcept
    {
        const auto c = (std::size_t) ch;
        double y;
        if (direct)
            y = (std::tanh (a * x + b) - tb) / k0;
        else
        {
            const double dx = x - x1[c];
            y = std::abs (dx) > 1.0e-9 ? (primitive (x) - primitive (x1[c])) / dx : (std::tanh (a * (0.5 * (x + x1[c])) + b) - tb) / k0;
            x1[c] = x;
        }
        const double out = y - dcX[c] + dcR * dcY[c];
        dcX[c] = y; dcY[c] = out;
        return out;
    }

private:
    double primitive (double x) const noexcept { return (compdetail::logCosh (a * x + b) / a - tb * x) / k0; }

    bool direct = false;
    double a = 0.1, b = 0.0, tb = 0.0, k0 = 0.1, dcR = 0.0;
    double x1[2] {}, dcX[2] {}, dcY[2] {};
};

/** Plafond doux de L-Comp : identité jusqu'à -1 dBFS puis saturation tanh vers 0 dBFS (C¹), ADAA 1er ordre. */
struct SoftCeiling
{
    static constexpr double k = 0.8912509381337456;   // -1 dBFS

    void reset() noexcept { x1[0] = x1[1] = 0.0; }

    double process (double x, int ch) noexcept
    {
        const auto c = (std::size_t) ch;
        const double dx = x - x1[c];
        const double y = std::abs (dx) > 1.0e-9 ? (primitive (x) - primitive (x1[c])) / dx : shape (0.5 * (x + x1[c]));
        x1[c] = x;
        return y;
    }

private:
    static double shape (double x) noexcept
    {
        const double u = std::abs (x);
        const double y = u <= k ? u : k + (1.0 - k) * std::tanh ((u - k) / (1.0 - k));
        return std::copysign (y, x);
    }
    static double primitive (double x) noexcept
    {
        const double u = std::abs (x);
        if (u <= k) return 0.5 * u * u;
        return 0.5 * k * k + k * (u - k) + (1.0 - k) * (1.0 - k) * compdetail::logCosh ((u - k) / (1.0 - k));
    }
    double x1[2] {};
};
} // namespace compdetail
} // namespace aura
