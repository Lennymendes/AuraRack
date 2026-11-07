// AuraRack — NextDSP · AuraClip
// Clipper Hard / Soft DXD avec antialiasing par antidérivée du 1er ordre (ADAA, Parker et al., DAFx 2016).
//
// Courbe normalisée (plafond = 1), genou w dans [0, 0,5] (Knee 0 % = hard clip, 100 % = genou de 6 dB) :
//   |x| <= 1 - w        : f(x) = x
//   1 - w < |x| < 1 + w : f(x) = x - (|x| - (1 - w))² / (4w)     (C1 : pente 1 -> 0)
//   |x| >= 1 + w        : f(x) = ±1
// ADAA : y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]), F antidérivée exacte de f,
// repli f((x[n] + x[n-1]) / 2) quand l'écart est trop petit pour être bien conditionné.
//
// Fréquence < 176,4 kHz : ADAA + sur-échantillonnage 2× / 4× ;
// à partir de 176,4 kHz (DXD) : ADAA seul. L'ADAA du 1er ordre retarde d'un demi-échantillon (de la fréquence
// de traitement) : 1,4 µs en DXD, déphasage de 0,18 rad à 20 kHz entre Dry et Wet, invisible dans le Mix.

#pragma once

#include "../AuraModuleDSP.h"

namespace aura
{
class AuraClip final : public AuraModuleDSP
{
public:
    enum Param { drive, ceiling, knee, output, autoGain, numParams };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* onOff[] = { "Off", "On" };
        static constexpr ParamSpec specs[] = {
            { "Drive",     "dB", 0.0,   24.0, 0.0,  S::linear },
            { "Ceiling",   "dB", -12.0, 0.0,  -0.3, S::linear },
            { "Knee",      "%",  0.0,   100.0, 30.0, S::linear },
            { "Output",    "dB", -24.0, 12.0, 0.0,  S::linear },
            { "Auto Gain", "",   0.0,   1.0,  0.0,  S::choice, onOff, 2 },
        };
        static const ModuleInfo i { ModuleType::clip, "AuraClip", "CLIP", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case drive:    driveDb = v; break;
            case ceiling:  ceilingDb = v; break;
            case knee:     kneeSmooth.setTarget (0.5 * std::clamp (v, 0.0, 100.0) / 100.0); break;
            case output:   outputDb = v; break;
            case autoGain: autoGainOn = v >= 0.5; break;
            default: break;
        }
        // Entrée normalisée par le plafond, sortie remise à l'échelle : le plafond reste exact.
        inScale.setTarget (dsp::dbToGain (driveDb - ceilingDb));
        outScale.setTarget (dsp::dbToGain (ceilingDb + outputDb - (autoGainOn ? driveDb : 0.0)));
    }

    void reset() noexcept override
    {
        oversampler.reset();
        kneeSmooth.reset (kneeSmooth.getTarget());
        inScale.reset (inScale.getTarget());
        outScale.reset (outScale.getTarget());
        prevX = { 0.0, 0.0 };
        peakIn = 0.0;
    }

    int getLatencySamples() const noexcept override { return oversampler.getLatency(); }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        const int factor = oversampler.getFactor();
        double* ch[2] = { L, R };

        // Les gains sont lissés au rythme de base, une fois par échantillon de base, puis appliqués à
        // chaque sous-échantillon : on les fige dans des tampons partagés par les deux canaux.
        for (int i = 0; i < n; ++i)
        {
            inGain[(std::size_t) i]   = inScale.next();
            outGain[(std::size_t) i]  = outScale.next();
            kneeValue[(std::size_t) i] = kneeSmooth.next();
        }

        double maxIn = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            double* x = ch[c];
            if (factor == 1)
            {
                for (int i = 0; i < n; ++i)
                {
                    const double u = x[i] * inGain[(std::size_t) i];
                    maxIn = std::max (maxIn, std::abs (u));
                    x[i] = adaa (u, kneeValue[(std::size_t) i], c) * outGain[(std::size_t) i];
                }
            }
            else
            {
                double* hi = oversampler.up (x, n, c);
                for (int i = 0; i < n; ++i)
                {
                    const double gIn = inGain[(std::size_t) i];
                    const double w = kneeValue[(std::size_t) i];
                    for (int k = 0; k < factor; ++k)
                    {
                        double& s = hi[i * factor + k];
                        const double u = s * gIn;
                        maxIn = std::max (maxIn, std::abs (u));
                        s = adaa (u, w, c);
                    }
                }
                oversampler.down (hi, x, n, c);
                // Pas d'écrêtage après la redescente : il rendrait le repliement que le sur-échantillonnage a retiré.
                // Le filtre peut faire déborder les angles d'un écrêtage dur (Gibbs, < 1 dB) ; AuraLimit, placé
                // après, garantit le plafond True Peak.
                for (int i = 0; i < n; ++i)
                    x[i] *= outGain[(std::size_t) i];
            }
        }
        peakIn = maxIn;
    }

    double getGainReductionDb() const noexcept override { return std::max (0.0, dsp::gainToDb (peakIn)); }

    //==============================================================================
    /** Courbe statique normalisée. */
    static double shape (double x, double w) noexcept
    {
        const double a = std::abs (x);
        const double s = x < 0.0 ? -1.0 : 1.0;
        const double lo = 1.0 - w, hi = 1.0 + w;
        if (a <= lo) return x;
        if (a >= hi) return s;
        const double d = a - lo;
        return s * (a - d * d / (4.0 * w));
    }

    /** Antidérivée exacte de shape (fonction paire, F(0) = 0). */
    static double antiderivative (double x, double w) noexcept
    {
        const double a = std::abs (x);
        const double lo = 1.0 - w, hi = 1.0 + w;
        if (a <= lo) return 0.5 * a * a;
        if (a >= hi) return 0.5 * hi * hi - (2.0 / 3.0) * w * w + (a - hi);
        const double d = a - lo;
        return 0.5 * a * a - d * d * d / (12.0 * w);
    }

private:
    double adaa (double x, double w, int c) noexcept
    {
        double& x1 = prevX[(std::size_t) c];
        const double dx = x - x1;
        double y;
        if (std::abs (dx) > 1.0e-7)
            y = (antiderivative (x, w) - antiderivative (x1, w)) / dx;
        else
            y = shape (0.5 * (x + x1), w);
        x1 = x;
        return y;
    }

    void prepareInternal() override
    {
        oversampler.prepare (sampleRate, maxBlock);
        kneeSmooth.prepare (sampleRate, 20.0);
        inScale.prepare (sampleRate, 20.0);
        outScale.prepare (sampleRate, 20.0);
        inGain.assign ((std::size_t) maxBlock, 1.0);
        outGain.assign ((std::size_t) maxBlock, 1.0);
        kneeValue.assign ((std::size_t) maxBlock, 0.0);
    }

    dsp::AdaptiveOversampler oversampler;
    dsp::OnePoleSmoother kneeSmooth, inScale, outScale;
    std::vector<double> inGain, outGain, kneeValue;
    std::array<double, 2> prevX {};
    double driveDb = 0.0, ceilingDb = -0.3, outputDb = 0.0, peakIn = 0.0;
    bool autoGainOn = false;
};

} // namespace aura
