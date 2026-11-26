// AuraRack — NextDSP · mesure de sonie ITU-R BS.1770-4 / EBU R128 (EBU Tech 3341 / 3342)
//
//   - Pondération K : étage 1 (plateau aigu +4 dB à 1,68 kHz) puis étage 2 (passe-haut RLB 38 Hz), en TPT / double :
//     la réponse est la même de 44,1 à 384 kHz (les coefficients de la norme ne valent qu'à 48 kHz).
//   - Momentary (400 ms), Short-term (3 s) : fenêtres glissantes, pas de 100 ms.
//   - Integrated : blocs de 400 ms (recouvrement 75 %), porte absolue −70 LUFS puis porte relative −10 LU.
//   - Loudness Range : sonie court terme, porte absolue −70 LUFS puis relative −20 LU, percentiles 10 % – 95 %.
//   - Peak : crête échantillon (dBFS) — ce n'est PAS un true-peak ; l'interface l'affiche comme « Peak ».
//   - Histogrammes à pas de 0,1 LU : mémoire fixe, aucune allocation, aucun verrou. Sans JUCE.
//   - Mono : un canal source seul compte pour une fois (G = 1), pas deux.
//   - Publication : atomiques `float` écrits par le thread audio, lus par l'interface.

#pragma once

#include "AuraDspCore.h"

#include <array>
#include <atomic>

namespace aura
{
class LoudnessMeter
{
public:
    struct Readout
    {
        std::atomic<float> momentary { -120.0f }, shortTerm { -120.0f }, integrated { -120.0f }, range { 0.0f }, peakDb { -120.0f };
    };

    static constexpr double kFloor = -120.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        hopLen = std::max (1, (int) std::lround (0.1 * sampleRate));
        // Coefficients généralisés de la norme (forme analytique de BS.1770 / libebur128) : exacts à toute fréquence.
        {
            const double Vh = std::pow (10.0, 3.999843853973347 / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double K = std::tan (dsp::kPi * 1681.974450955533 / sampleRate), Q = 0.7071752369554196;
            const double a0 = 1.0 + K / Q + K * K;
            shelf.set ((Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                       2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
        }
        {
            const double K = std::tan (dsp::kPi * 38.13547087602444 / sampleRate), Q = 0.5003270373238773;
            const double a0 = 1.0 + K / Q + K * K;
            rlb.set (1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
        }
        reset();
    }

    /** Remise à zéro complète (appelée sur le thread audio). */
    void reset() noexcept
    {
        shelf.reset();
        rlb.reset();
        hopSum = 0.0;
        hopCount = 0;
        ring.fill (0.0);
        ringPos = 0;
        ringFill = 0;
        for (auto* h : { &gated, &range })
        {
            h->count.fill (0);
            h->energy.fill (0.0);
        }
        peak = 0.0;
        publish (kFloor, kFloor, kFloor, 0.0, kFloor);
    }

    void process (const double* L, const double* R, int n, bool mono) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double l = L[i], r = mono ? 0.0 : R[i];
            peak = std::max (peak, std::max (std::abs (l), mono ? 0.0 : std::abs (r)));
            const double kl = rlb.processSample (shelf.processSample (l, 0), 0);
            double e = kl * kl;
            if (! mono)
            {
                const double kr = rlb.processSample (shelf.processSample (r, 1), 1);
                e += kr * kr;
            }
            hopSum += e;
            if (++hopCount >= hopLen) endHop();
        }
    }

    const Readout& getReadout() const noexcept { return readout; }

    //==== utilitaires (tests) ======================================================
    static double toLufs (double meanSquare) noexcept { return meanSquare > 1.0e-12 ? -0.691 + 10.0 * std::log10 (meanSquare) : kFloor; }

private:
    static constexpr int kBins = 900;                   // -70 … +20 LUFS par pas de 0,1
    static constexpr int kRingLen = 30;                 // 3 s de pas de 100 ms

    struct Histogram
    {
        std::array<std::uint32_t, kBins> count {};
        std::array<double, kBins> energy {};
        void add (double lufs, double meanSquare) noexcept
        {
            if (lufs < -70.0) return;
            const int b = std::clamp ((int) ((lufs + 70.0) * 10.0), 0, kBins - 1);
            ++count[(std::size_t) b];
            energy[(std::size_t) b] += meanSquare;
        }
    };

    static double binLufs (int b) noexcept { return -70.0 + 0.1 * (double) b; }

    void endHop() noexcept
    {
        const double e = hopSum / (double) hopLen;
        hopSum = 0.0;
        hopCount = 0;
        ring[(std::size_t) ringPos] = e;
        ringPos = (ringPos + 1) % kRingLen;
        ringFill = std::min (ringFill + 1, kRingLen);

        auto meanOfLast = [this] (int k)
        {
            double s = 0.0;
            for (int j = 1; j <= k; ++j) s += ring[(std::size_t) ((ringPos - j + 2 * kRingLen) % kRingLen)];
            return s / (double) k;
        };

        const double momentaryMs = meanOfLast (std::min (ringFill, 4));
        const double shortMs = meanOfLast (std::min (ringFill, kRingLen));

        if (ringFill >= 4)  gated.add (toLufs (momentaryMs), momentaryMs);   // bloc de 400 ms
        if (ringFill >= kRingLen) range.add (toLufs (shortMs), shortMs);     // fenêtre de 3 s

        publish (toLufs (momentaryMs), toLufs (shortMs), integratedLufs(), loudnessRange(),
                 peak > 1.0e-9 ? dsp::gainToDb (peak) : kFloor);
    }

    double integratedLufs() const noexcept
    {
        double sum = 0.0;
        std::uint64_t cnt = 0;
        for (int b = 0; b < kBins; ++b) { sum += gated.energy[(std::size_t) b]; cnt += gated.count[(std::size_t) b]; }
        if (cnt == 0) return kFloor;
        const double relative = toLufs (sum / (double) cnt) - 10.0;
        double s2 = 0.0;
        std::uint64_t c2 = 0;
        for (int b = 0; b < kBins; ++b)
            if (binLufs (b) >= relative) { s2 += gated.energy[(std::size_t) b]; c2 += gated.count[(std::size_t) b]; }
        return c2 > 0 ? toLufs (s2 / (double) c2) : kFloor;
    }

    double loudnessRange() const noexcept
    {
        double sum = 0.0;
        std::uint64_t cnt = 0;
        for (int b = 0; b < kBins; ++b) { sum += range.energy[(std::size_t) b]; cnt += range.count[(std::size_t) b]; }
        if (cnt == 0) return 0.0;
        const double relative = toLufs (sum / (double) cnt) - 20.0;
        std::uint64_t total = 0;
        for (int b = 0; b < kBins; ++b)
            if (binLufs (b) >= relative) total += range.count[(std::size_t) b];
        if (total == 0) return 0.0;
        auto percentile = [&] (double p)
        {
            const std::uint64_t target = (std::uint64_t) std::ceil (p * (double) total);
            std::uint64_t acc = 0;
            for (int b = 0; b < kBins; ++b)
            {
                if (binLufs (b) < relative) continue;
                acc += range.count[(std::size_t) b];
                if (acc >= std::max<std::uint64_t> (target, 1)) return binLufs (b);
            }
            return relative;
        };
        return std::max (0.0, percentile (0.95) - percentile (0.10));
    }

    void publish (double m, double s, double i, double lra, double pk) noexcept
    {
        readout.momentary.store ((float) m, std::memory_order_relaxed);
        readout.shortTerm.store ((float) s, std::memory_order_relaxed);
        readout.integrated.store ((float) i, std::memory_order_relaxed);
        readout.range.store ((float) lra, std::memory_order_relaxed);
        readout.peakDb.store ((float) pk, std::memory_order_relaxed);
    }

    /** Biquad en forme transposée II, double précision, deux canaux. */
    struct Biquad
    {
        void set (double nb0, double nb1, double nb2, double na1, double na2) noexcept { b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2; }
        void reset() noexcept { z1.fill (0.0); z2.fill (0.0); }
        double processSample (double x, int ch) noexcept
        {
            const double y = b0 * x + z1[(std::size_t) ch];
            z1[(std::size_t) ch] = b1 * x - a1 * y + z2[(std::size_t) ch];
            z2[(std::size_t) ch] = b2 * x - a2 * y;
            return y;
        }
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        std::array<double, 2> z1 {}, z2 {};
    };

    double fs = 48000.0;
    int hopLen = 4800, hopCount = 0, ringPos = 0, ringFill = 0;
    double hopSum = 0.0, peak = 0.0;
    std::array<double, kRingLen> ring {};
    Histogram gated, range;
    Biquad shelf, rlb;
    Readout readout;
};

} // namespace aura
