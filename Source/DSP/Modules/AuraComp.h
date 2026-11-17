// AuraRack — NextDSP · AuraComp
// Compresseur à trois algorithmes, conçu pour le DXD (352,8 / 384 kHz) et correct à toute fréquence.
//
//   Aura-Opto : leveling amplifier à cellule optique (esprit LA-2A). Gain + Peak Reduction.
//               Attaque et relâchement dépendants du programme.
//   Aura-Tube : compresseur optique à lampes (esprit CL 1B). Threshold, Ratio, Attack, Release, Makeup.
//               Étage de sortie « lampe » : saturation asymétrique douce (ADAA) + blocage du continu.
//   L-Comp    : « Looser Compressor », un seul potentiomètre (Crush) qui règle seuil, ratio et makeup.
//               Attaque ~50 µs, relâchement à deux temps, plafond doux de sécurité.
//
// Calcul d'enveloppe (toute la dynamique de gain est en double, en dB) :
//   - constantes de temps exprimées en secondes ; coefficient 1-exp(-1/(τ·fs)) calculé par expm1
//     (aucune annulation numérique, même pour 7,5 s à 384 kHz) ;
//   - temps dépendants du programme réévalués à chaque échantillon : la réponse ne dépend pas de fs ;
//   - courbes de transfert analytiques et lisses (softplus, genou quadratique), sans table ni interpolation ;
//   - étage « panneau électroluminescent » (moyenne de puissance, τ ~ 1 ms) avant la conversion en dB :
//     pas de modulation du gain à 2f par le redressement du signal ;
//   - états ramenés exactement à zéro en dessous de 1e-15 dB (pas de dénormalisés).
// Latence : 0. Clé : entrée du module ou bus Sidechain.

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

/** Étage « lampe » : tanh asymétrique à gain petit signal unitaire, ADAA du 1er ordre. */
struct TubeStage
{
    static constexpr double a = 0.3, b = 0.12;

    void prepare (double sampleRate) noexcept
    {
        dcR = std::exp (-2.0 * dsp::kPi * 5.0 / sampleRate);
        k0 = a * (1.0 - std::tanh (b) * std::tanh (b));
        tb = std::tanh (b);
    }
    void reset() noexcept { for (auto& v : x1) v = 0.0; for (auto& v : dcX) v = 0.0; for (auto& v : dcY) v = 0.0; }

    double process (double x, int ch) noexcept
    {
        const auto c = (std::size_t) ch;
        const double dx = x - x1[c];
        double y;
        if (std::abs (dx) > 1.0e-9)
            y = (primitive (x) - primitive (x1[c])) / dx;
        else
            y = (std::tanh (a * (0.5 * (x + x1[c])) + b) - tb) / k0;
        x1[c] = x;
        // Blocage du continu (5 Hz) : l'asymétrie crée un décalage dépendant du niveau.
        const double out = y - dcX[c] + dcR * dcY[c];
        dcX[c] = y; dcY[c] = out;
        return out;
    }

private:
    double primitive (double x) const noexcept { return (compdetail::logCosh (a * x + b) / a - tb * x) / k0; }

    double dcR = 0.0, k0 = 1.0, tb = 0.0;
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

//==============================================================================
class AuraComp final : public AuraModuleDSP
{
public:
    enum Param { algorithm, keySource, peakReduction, gain, threshold, ratio, attack, release, makeup, crush, numParams };
    enum Algorithm { opto = 0, tube = 1, lComp = 2 };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* algos[] = { "Aura-Opto", "Aura-Tube", "L-Comp" };
        static constexpr const char* keys[]  = { "Interne", "Sidechain" };
        static constexpr ParamSpec specs[] = {
            { "Algorithm",      "",   0.0,   2.0,    0.0,   S::choice, algos, 3 },
            { "Key",            "",   0.0,   1.0,    0.0,   S::choice, keys, 2 },
            { "Peak Reduction", "%",  0.0,   100.0,  40.0,  S::linear },
            { "Gain",           "dB", -12.0, 30.0,   0.0,   S::linear },
            { "Threshold",      "dB", -60.0, 0.0,    -18.0, S::linear },
            { "Ratio",          ":1", 1.0,   10.0,   4.0,   S::logarithmic },
            { "Attack",         "ms", 0.5,   300.0,  10.0,  S::logarithmic },
            { "Release",        "ms", 5.0,   7500.0, 200.0, S::logarithmic },
            { "Makeup",         "dB", -12.0, 24.0,   0.0,   S::linear },
            { "Crush",          "%",  0.0,   100.0,  50.0,  S::linear },
        };
        // Paramètres visibles par algorithme (bit i = paramètre i).
        static constexpr std::uint32_t masks[] = {
            (1u << algorithm) | (1u << keySource) | (1u << peakReduction) | (1u << gain),
            (1u << algorithm) | (1u << keySource) | (1u << threshold) | (1u << ratio) | (1u << attack) | (1u << release) | (1u << makeup),
            (1u << algorithm) | (1u << keySource) | (1u << crush),
        };
        static const ModuleInfo i { ModuleType::comp, "AuraComp", "COMP", specs, 100.0, algorithm, masks };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case algorithm:
            {
                const int a = std::clamp ((int) std::lround (v), 0, 2);
                if (a != algo) { algo = a; configureCell(); snapSmoothers(); resetState(); }
                break;
            }
            case keySource:     useSidechain = v >= 0.5; break;
            case peakReduction: driveSm.setTarget (0.6 * v); break;                // 0 … 60 dB envoyés au détecteur
            case gain:          gainSm.setTarget (v); break;
            case threshold:     thresholdSm.setTarget (v); break;
            case ratio:         ratioSm.setTarget (std::max (1.0, v)); break;
            case attack:        attackMs = v; configureCell(); updateElCoef(); break;
            case release:       releaseMs = v; configureCell(); break;
            case makeup:        makeupSm.setTarget (v); break;
            case crush:         crushSm.setTarget (std::clamp (v, 0.0, 100.0) * 0.01); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        snapSmoothers();
        resetState();
    }

    void process (double* L, double* R, int n, const SidechainBlock& key) noexcept override
    {
        const bool external = useSidechain && key.available;
        const double* kL = external ? key.left  : L;
        const double* kR = external ? key.right : R;

        switch (algo)
        {
            case opto:  processOpto (L, R, kL, kR, n);  break;
            case tube:  processTube (L, R, kL, kR, n);  break;
            default:    processLComp (L, R, kL, kR, n); break;
        }
    }

    double getGainReductionDb() const noexcept override { return lastReduction; }

    /** Courbe statique du mode Tube (genou 10 dB) pour un niveau d'entrée en dB — utilisée par les tests. */
    double gainReductionFor (double xDb) const noexcept
    {
        return compdetail::kneeReduction (xDb, thresholdSm.getTarget(), ratioSm.getTarget(), kTubeKneeDb);
    }

private:
    static constexpr double kTubeKneeDb = 10.0;
    static constexpr double kLCompKneeDb = 4.0;
    // Aura-Opto : courbe softplus de largeur 10 dB, ratio effectif 3:1, origine +18 dBFS (Peak Reduction = 0 : presque rien).
    static constexpr double kOptoWidthDb = 10.0, kOptoSlope = 1.0 - 1.0 / 3.0, kOptoOriginDb = 18.0;

    void prepareInternal() override
    {
        for (auto* s : { &driveSm, &gainSm, &thresholdSm, &ratioSm, &makeupSm, &crushSm })
            s->prepare (sampleRate, 20.0);
        cell.prepare (sampleRate);
        tubeStage.prepare (sampleRate);
        keyHp.setup (dsp::TptSvf::Type::highPass, 60.0, 0.70710678118654752, 0.0, sampleRate);
        peakDecay = std::exp (-1.0 / (0.0005 * sampleRate));
        configureCell();
        updateElCoef();
    }

    void configureCell() noexcept
    {
        switch (algo)
        {
            case opto:
                cell.fixedBlend = -1.0; cell.attackDepthDb = 6.0; cell.memKneeDb = 6.0;
                cell.setTimes (0.010, 0.060, 2.5);
                elCoef = compdetail::alpha (0.002, sampleRate);
                break;
            case tube:
                cell.fixedBlend = -1.0; cell.attackDepthDb = 24.0; cell.memKneeDb = 6.0;
                cell.setTimes (attackMs * 0.001, releaseMs * 0.00025, releaseMs * 0.001);
                updateElCoef();
                break;
            default:
                cell.fixedBlend = 0.5; cell.attackDepthDb = 1.0e9; cell.memKneeDb = 6.0;
                cell.setTimes (0.00005, 0.015, 0.200);
                break;
        }
    }

    void updateElCoef() noexcept
    {
        if (algo == tube)
            elCoef = compdetail::alpha (std::clamp (0.25 * attackMs * 0.001, 5.0e-4, 2.0e-3), sampleRate);
    }

    void snapSmoothers() noexcept
    {
        for (auto* s : { &driveSm, &gainSm, &thresholdSm, &ratioSm, &makeupSm, &crushSm })
            s->reset (s->getTarget());
    }

    void resetState() noexcept
    {
        cell.reset();
        tubeStage.reset();
        ceiling.reset();
        keyHp.reset();
        elPower = 0.0;
        peakHold = 0.0;
        lastReduction = 0.0;
    }

    /** Niveau (dB) vu par la cellule : puissance lissée par le « panneau EL », référencée crête (+3 dB pour un sinus). */
    double electroLuminescentDb (double level) noexcept
    {
        elPower += elCoef * (level * level - elPower);
        if (elPower < 1.0e-30) elPower = 0.0;
        return dsp::gainToDb (std::sqrt (2.0 * elPower));
    }

    void processOpto (double* L, double* R, const double* kL, const double* kR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double xDb = electroLuminescentDb (std::max (std::abs (kL[i]), std::abs (kR[i])));
            const double z = (xDb + driveSm.next() - kOptoOriginDb) / kOptoWidthDb;
            const double reduction = cell.next (kOptoSlope * kOptoWidthDb * compdetail::softplus (z));
            lastReduction = reduction;
            const double g = dsp::dbToGain (gainSm.next() - reduction);
            L[i] *= g;
            R[i] *= g;
        }
    }

    void processTube (double* L, double* R, const double* kL, const double* kR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double xDb = electroLuminescentDb (std::max (std::abs (kL[i]), std::abs (kR[i])));
            const double target = compdetail::kneeReduction (xDb, thresholdSm.next(), ratioSm.next(), kTubeKneeDb);
            const double reduction = cell.next (target);
            lastReduction = reduction;
            const double g = dsp::dbToGain (makeupSm.next() - reduction);
            L[i] = tubeStage.process (L[i] * g, 0);
            R[i] = tubeStage.process (R[i] * g, 1);
        }
    }

    void processLComp (double* L, double* R, const double* kL, const double* kR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            // Clé filtrée à 60 Hz, détecteur à maintien de crête (τ 0,5 ms) : pas de modulation à 2f sur les graves.
            const double hl = keyHp.processSample (kL[i], 0);
            const double hr = keyHp.processSample (kR[i], 1);
            peakHold = std::max (std::max (std::abs (hl), std::abs (hr)), peakHold * peakDecay);
            if (peakHold < 1.0e-30) peakHold = 0.0;

            // Un seul bouton : seuil -6 … -46 dB, ratio 2 … 20:1, makeup automatique (85 % de la compensation du plafond).
            const double c = crushSm.next();
            const double thr = -6.0 - 40.0 * c;
            const double rat = 2.0 * std::pow (10.0, c);
            const double autoMakeup = -0.85 * thr * (1.0 - 1.0 / rat);

            const double target = compdetail::kneeReduction (dsp::gainToDb (peakHold), thr, rat, kLCompKneeDb);
            const double reduction = cell.next (target);
            lastReduction = reduction;
            const double g = dsp::dbToGain (autoMakeup - reduction);
            L[i] = ceiling.process (L[i] * g, 0);
            R[i] = ceiling.process (R[i] * g, 1);
        }
    }

    int algo = opto;
    bool useSidechain = false;
    double attackMs = 10.0, releaseMs = 200.0;
    double elCoef = 0.0, elPower = 0.0, peakDecay = 0.0, peakHold = 0.0, lastReduction = 0.0;

    dsp::OnePoleSmoother driveSm, gainSm, thresholdSm, ratioSm, makeupSm, crushSm;
    compdetail::OptoCell cell;
    compdetail::TubeStage tubeStage;
    compdetail::SoftCeiling ceiling;
    dsp::TptSvf keyHp;
};

} // namespace aura
