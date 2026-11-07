// AuraRack — NextDSP · AuraVerb
// Réverbération algorithmique de studio : plate de Dattorro (« Effect Design Part 1 », JAES 1997),
// version compacte en double précision.
//
// - Toutes les longueurs du schéma original (définies à 29 761 Hz) sont mises à l'échelle de la fréquence
//   courante et du paramètre Size : même son de 44,1 à 384 kHz.
// - Les deux allpass modulés du réservoir et toutes les lectures du réservoir sont fractionnaires
//   (Lagrange 3e ordre) : Size se règle en continu, sans clic.
// - Decay est un vrai RT60 : le gain de chaque segment du réservoir vaut 10^(-3 · durée / RT60).
// - Damping est une fréquence de coupure (passe-bas 1 pôle dans la boucle), indépendante de fs.
// Sortie 100 % wet : le dosage se fait avec le Mix du slot.

#pragma once

#include "../AuraModuleDSP.h"

namespace aura
{
class AuraVerb final : public AuraModuleDSP
{
public:
    enum Param { preDelay, size, decay, damping, width, lowCut, numParams };

    static constexpr double kReferenceRate = 29761.0;
    static constexpr double kMaxSize = 1.5;
    static constexpr double kMaxPreDelayMs = 250.0;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr ParamSpec specs[] = {
            { "Pre-Delay", "ms", 0.0,    kMaxPreDelayMs, 20.0,   S::linear },
            { "Size",      "%",  50.0,   150.0,          100.0,  S::linear },
            { "Decay",     "s",  0.2,    20.0,           2.5,    S::logarithmic },
            { "Damping",   "Hz", 1000.0, 20000.0,        7000.0, S::logarithmic },
            { "Width",     "%",  0.0,    150.0,          100.0,  S::linear },
            { "Low Cut",   "Hz", 20.0,   1000.0,         100.0,  S::logarithmic },
        };
        static const ModuleInfo i { ModuleType::verb, "AuraVerb", "VERB", specs, 30.0 };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case preDelay: preDelaySmooth.setTarget (std::max (1.0, v * 0.001 * sampleRate)); break;
            case size:     sizeSmooth.setTarget (std::clamp (v / 100.0, 0.5, kMaxSize)); break;
            case decay:    rt60 = std::max (0.05, v); break;
            case damping:  dampCoef = std::exp (-dsp::kTwoPi * std::min (v, 0.45 * sampleRate) / sampleRate); break;
            case width:    widthSmooth.setTarget (v / 100.0); break;
            case lowCut:   inputHp.setup (dsp::TptSvf::Type::highPass, v, 0.7071067811865476, 0.0, sampleRate); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        preDelayLine.reset();
        for (auto& d : inputDiffusers) d.line.reset();
        for (auto& t : tank) { t.ap1.reset(); t.d1.reset(); t.ap2.reset(); t.d2.reset(); t.lp = 0.0; t.out = 0.0; }
        inputHp.reset();
        preDelaySmooth.reset (preDelaySmooth.getTarget());
        sizeSmooth.reset (sizeSmooth.getTarget());
        widthSmooth.reset (widthSmooth.getTarget());
        lfoPhase = 0.0;
        updateGeometry();
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            if (sizeSmooth.isSmoothing()) sizeSmooth.next();
            if ((++geometryCounter & 31) == 0) updateGeometry();   // décroissance suivie même pendant un changement de Decay

            // Entrée mono -> pré-delay -> passe-haut -> 4 diffuseurs.
            double x = 0.5 * (L[i] + R[i]);
            const double pd = preDelaySmooth.next();
            const double delayed = preDelayLine.readFractional (std::max (1.0, pd - 1.0));
            preDelayLine.push (x);
            x = inputHp.processSample (delayed, 0);
            for (std::size_t k = 0; k < inputDiffusers.size(); ++k)
                x = inputDiffusers[k].process (x, kInputDiffusion[k], scaledInput[k]);

            // Réservoir en huit croisé.
            lfoPhase += lfoInc;
            if (lfoPhase >= dsp::kTwoPi) lfoPhase -= dsp::kTwoPi;
            const double mod[2] = { std::sin (lfoPhase) * excursion, std::cos (lfoPhase) * excursion };
            const double feedL = tank[1].out, feedR = tank[0].out;
            for (int side = 0; side < 2; ++side)
            {
                auto& t = tank[(std::size_t) side];
                const auto& g = geometry[(std::size_t) side];
                double v = x + (side == 0 ? feedL : feedR);
                v = allpassFractional (t.ap1, v, -0.70, g.ap1 + mod[side]);
                v = readThenPush (t.d1, v, g.d1);
                t.lp = v + dampCoef * (t.lp - v);
                v = t.lp * g.gainA;
                v = allpassFractional (t.ap2, v, decayDiffusion2, g.ap2);
                v = readThenPush (t.d2, v, g.d2);
                t.out = v * g.gainB;
            }

            // Prises de sortie (Dattorro, tableau 2), lues après l'écriture de l'échantillon courant.
            const auto& tl = tank[0];
            const auto& tr = tank[1];
            const double s = scale;
            double yL = tr.d1.readFractional (266 * s)  + tr.d1.readFractional (2974 * s) - tr.ap2.readFractional (1913 * s)
                      + tr.d2.readFractional (1996 * s) - tl.d1.readFractional (1990 * s) - tl.ap2.readFractional (187 * s)
                      - tl.d2.readFractional (1066 * s);
            double yR = tl.d1.readFractional (353 * s)  + tl.d1.readFractional (3627 * s) - tl.ap2.readFractional (1228 * s)
                      + tl.d2.readFractional (2673 * s) - tr.d1.readFractional (2111 * s) - tr.ap2.readFractional (335 * s)
                      - tr.d2.readFractional (121 * s);
            yL *= 0.6;
            yR *= 0.6;

            const double w = widthSmooth.next();
            const double mid = 0.5 * (yL + yR), side = 0.5 * (yL - yR) * w;
            L[i] = mid + side;
            R[i] = mid - side;
        }
    }

private:
    struct Allpass
    {
        dsp::DelayLine line;
        double process (double x, double g, std::size_t d) noexcept
        {
            const double z = line.read (d - 1);
            const double v = x + g * z;
            line.push (v);
            return z - g * v;
        }
    };

    struct TankSide
    {
        dsp::DelayLine ap1, d1, ap2, d2;
        double lp = 0.0, out = 0.0;
    };

    struct Geometry { double ap1, d1, ap2, d2, gainA, gainB; };

    static double allpassFractional (dsp::DelayLine& line, double x, double g, double d) noexcept
    {
        const double z = line.readFractional (d - 1.0);
        const double v = x + g * z;
        line.push (v);
        return z - g * v;
    }

    static double readThenPush (dsp::DelayLine& line, double x, double d) noexcept
    {
        const double y = line.readFractional (d - 1.0);
        line.push (x);
        return y;
    }

    // Longueurs de Dattorro à 29 761 Hz.
    static constexpr double kInputLengths[4]   = { 142.0, 107.0, 379.0, 277.0 };
    static constexpr double kInputDiffusion[4] = { 0.75, 0.75, 0.625, 0.625 };
    static constexpr double kTankLengths[2][4] = { { 672.0, 4453.0, 1800.0, 3720.0 },
                                                   { 908.0, 4217.0, 2656.0, 3163.0 } };
    static constexpr double kExcursion = 16.0;
    static constexpr double kLfoHz = 1.0;

    void updateGeometry() noexcept
    {
        scale = (sampleRate / kReferenceRate) * sizeSmooth.getCurrent();
        double avgGain = 0.0;
        for (int side = 0; side < 2; ++side)
        {
            auto& g = geometry[(std::size_t) side];
            g.ap1 = kTankLengths[side][0] * scale;
            g.d1  = kTankLengths[side][1] * scale;
            g.ap2 = kTankLengths[side][2] * scale;
            g.d2  = kTankLengths[side][3] * scale;
            g.gainA = std::pow (10.0, -3.0 * (g.ap1 + g.d1) / (rt60 * sampleRate));
            g.gainB = std::pow (10.0, -3.0 * (g.ap2 + g.d2) / (rt60 * sampleRate));
            avgGain += 0.25 * (g.gainA + g.gainB);
        }
        // Diffusion de décroissance 2 = decay + 0,15 bornée à [0,25 ; 0,5] (Dattorro).
        decayDiffusion2 = std::clamp (avgGain + 0.15, 0.25, 0.5);
    }

    void prepareInternal() override
    {
        const double maxScale = (sampleRate / kReferenceRate) * kMaxSize;
        const double baseScale = sampleRate / kReferenceRate;
        excursion = kExcursion * baseScale;
        for (std::size_t k = 0; k < 4; ++k)
        {
            scaledInput[k] = (std::size_t) std::max (2.0, std::round (kInputLengths[k] * baseScale));
            inputDiffusers[k].line.prepare (scaledInput[k] + 4);
        }
        for (int side = 0; side < 2; ++side)
        {
            auto& t = tank[(std::size_t) side];
            t.ap1.prepare ((std::size_t) std::ceil (kTankLengths[side][0] * maxScale + excursion) + 8);
            t.d1.prepare  ((std::size_t) std::ceil (kTankLengths[side][1] * maxScale) + 8);
            t.ap2.prepare ((std::size_t) std::ceil (kTankLengths[side][2] * maxScale) + 8);
            t.d2.prepare  ((std::size_t) std::ceil (kTankLengths[side][3] * maxScale) + 8);
        }
        preDelayLine.prepare ((std::size_t) std::ceil (kMaxPreDelayMs * 0.001 * sampleRate) + 8);
        preDelaySmooth.prepare (sampleRate, 50.0);
        sizeSmooth.prepare (sampleRate, 250.0);
        widthSmooth.prepare (sampleRate, 20.0);
        lfoInc = dsp::kTwoPi * kLfoHz / sampleRate;
    }

    dsp::DelayLine preDelayLine;
    std::array<Allpass, 4> inputDiffusers;
    std::array<std::size_t, 4> scaledInput {};
    std::array<TankSide, 2> tank;
    std::array<Geometry, 2> geometry {};
    dsp::TptSvf inputHp;
    dsp::OnePoleSmoother preDelaySmooth, sizeSmooth, widthSmooth;
    double rt60 = 2.5, dampCoef = 0.0, decayDiffusion2 = 0.5, scale = 1.0, excursion = 16.0;
    double lfoPhase = 0.0, lfoInc = 0.0;
    unsigned geometryCounter = 0;
};

} // namespace aura
