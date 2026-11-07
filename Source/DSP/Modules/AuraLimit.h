// AuraRack — NextDSP · AuraLimit
// Limiteur brickwall True Peak, lookahead 1,5 ms, relâchement adaptatif.
//
// 1. Détection True Peak : interpolation polyphase 4× à 32 coefficients par phase (2× à partir de 176,4 kHz,
//    où l'écart inter-échantillon d'un contenu audio est déjà < 0,04 dB) + crête échantillon.
// 2. Gain requis g = min(1, plafond / crête), minimum glissant sur la fenêtre d'anticipation.
// 3. Relâchement : instantané vers le bas, exponentiel vers le haut. Constante adaptative : courte
//    après un transitoire isolé (les transitoires ne pompent pas), longue quand la réduction dure
//    (pas de distorsion sur un signal dense et fort).
// 4. Filtre moyenneur (boîte) de la longueur de l'anticipation : l'attaque est une rampe lisse qui
//    atteint exactement le gain requis à l'arrivée de la crête (démonstration dans le README).
// 5. Écrêtage de sécurité au plafond, en dernier recours.

#pragma once

#include "../AuraModuleDSP.h"

namespace aura
{
class AuraLimit final : public AuraModuleDSP
{
public:
    enum Param { inputGain, ceiling, release, adaptive, numParams };

    static constexpr double kLookaheadMs = 1.5;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr ParamSpec specs[] = {
            { "Gain",     "dB",   0.0,   24.0,   0.0,  S::linear },
            { "Ceiling",  "dBTP", -12.0, 0.0,    -1.0, S::linear },
            { "Release",  "ms",   1.0,   1000.0, 60.0, S::logarithmic },
            { "Adaptive", "%",    0.0,   100.0,  70.0, S::linear },
        };
        static const ModuleInfo i { ModuleType::limit, "AuraLimit", "LIMIT", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case inputGain: inSmooth.setTarget (dsp::dbToGain (v)); break;
            case ceiling:   ceilingGain = dsp::dbToGain (v); break;
            case release:   releaseMs = v; break;
            case adaptive:  adaptiveAmount = std::clamp (v / 100.0, 0.0, 1.0); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        for (auto& d : audioDelay) d.reset();
        for (auto& h : tpHistory) std::fill (h.begin(), h.end(), 0.0);
        tpPos = 0;
        holdMin.reset();
        std::fill (boxRing.begin(), boxRing.end(), 1.0);
        boxSum = (double) boxRing.size();
        boxPos = 0;
        boxRefresh = 0;
        envelope = 1.0;
        sustainedDb = 0.0;
        outGain = 1.0;
        inSmooth.reset (inSmooth.getTarget());
    }

    int getLatencySamples() const noexcept override { return latency; }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        double* ch[2] = { L, R };
        const double releaseBase = releaseMs;
        const double boxNorm = 1.0 / (double) boxRing.size();

        for (int i = 0; i < n; ++i)
        {
            const double gIn = inSmooth.next();
            double peak = 0.0;

            for (int c = 0; c < 2; ++c)
            {
                const double x = ch[c][i] * gIn;
                audioDelay[(std::size_t) c].push (x);
                peak = std::max (peak, std::abs (x));
                peak = std::max (peak, interpolatedPeak (x, c));
            }
            tpPos = (tpPos + 1) % kTpTaps;

            // Gain requis et minimum glissant (fenêtre = latence + 1).
            const double required = peak > ceilingGain ? ceilingGain / peak : 1.0;
            const double m = holdMin.push (required);

            // Relâchement adaptatif.
            const double grDb = -dsp::gainToDb (envelope);
            sustainedDb = grDb + sustainCoef * (sustainedDb - grDb);
            const double s = std::clamp (sustainedDb / 6.0, 0.0, 1.0);
            const double relMs = releaseBase * ((1.0 - adaptiveAmount) + adaptiveAmount * (0.3 + 1.7 * s));
            if (m < envelope)
                envelope = m;
            else
                envelope = m + dsp::timeCoef (relMs, sampleRate) * (envelope - m);

            // Moyenne glissante exacte sur boxRing.size() échantillons.
            boxSum += envelope - boxRing[boxPos];
            boxRing[boxPos] = envelope;
            boxPos = (boxPos + 1) % boxRing.size();
            if (++boxRefresh >= 65536)
            {
                boxRefresh = 0;
                boxSum = 0.0;
                for (double v : boxRing) boxSum += v;
            }
            outGain = std::min (1.0, boxSum * boxNorm);

            for (int c = 0; c < 2; ++c)
            {
                const double y = audioDelay[(std::size_t) c].read ((std::size_t) latency) * outGain;
                ch[c][i] = std::clamp (y, -ceilingGain, ceilingGain);
            }
        }
    }

    double getGainReductionDb() const noexcept override { return -dsp::gainToDb (outGain); }

private:
    static constexpr int kTpTaps = 32;   // coefficients par phase (128 au total en 4×)

    double interpolatedPeak (double x, int c) noexcept
    {
        auto& hist = tpHistory[(std::size_t) c];
        hist[(std::size_t) tpPos] = x;
        double p = 0.0;
        for (int phase = 1; phase < tpFactor; ++phase)
        {
            double y = 0.0;
            for (int k = 0; k < kTpTaps; ++k)
            {
                const int idx = (tpPos - k + kTpTaps) % kTpTaps;
                y += tpCoefs[(std::size_t) (phase + tpFactor * k)] * hist[(std::size_t) idx];
            }
            p = std::max (p, std::abs (y));
        }
        return p;
    }

    void prepareInternal() override
    {
        tpFactor = sampleRate >= dsp::kOversamplingOffRate ? 2 : 4;
        // Coupure à la fréquence de Nyquist d'origine : le signal y est déjà limité, l'interpolation reste plate
        // jusqu'à ~0,42·fs (18,5 kHz à 44,1 kHz ; tout le spectre audio en DXD).
        tpCoefs = dsp::designKaiserLowpass (tpFactor * kTpTaps, 0.5 / tpFactor, 9.0);
        for (auto& c : tpCoefs) c *= tpFactor;
        for (auto& h : tpHistory) h.assign (kTpTaps, 0.0);

        // Le filtre d'interpolation retarde la détection d'environ (N-1)/2 échantillons hauts :
        // l'audio est retardé d'autant (+1 de marge) pour que l'anticipation reste de 1,5 ms.
        const int lookahead = std::max (1, (int) std::lround (kLookaheadMs * 0.001 * sampleRate));
        const int tpDelay = (int) std::ceil (0.5 * (tpFactor * kTpTaps - 1) / tpFactor) + 1;
        latency = lookahead + tpDelay;

        for (auto& d : audioDelay) d.prepare ((std::size_t) latency + 2);
        holdMin.prepare (latency + 1);
        boxRing.assign ((std::size_t) lookahead + 1, 1.0);
        sustainCoef = dsp::timeCoef (300.0, sampleRate);
        inSmooth.prepare (sampleRate, 20.0);
    }

    std::array<dsp::DelayLine, 2> audioDelay;
    std::array<std::vector<double>, 2> tpHistory;
    std::vector<double> tpCoefs, boxRing;
    dsp::SlidingMinimum holdMin;
    dsp::OnePoleSmoother inSmooth;
    int tpFactor = 4, tpPos = 0, latency = 0, boxRefresh = 0;
    std::size_t boxPos = 0;
    double boxSum = 0.0, envelope = 1.0, sustainedDb = 0.0, sustainCoef = 0.0, outGain = 1.0;
    double ceilingGain = 0.891, releaseMs = 60.0, adaptiveAmount = 0.7;
};

} // namespace aura
