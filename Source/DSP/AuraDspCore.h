// AuraRack — NextDSP
// Noyau DSP 64 bits, sans dépendance à JUCE : lisseurs, rampes, filtre SVF TPT,
// lignes de retard fractionnaires (Lagrange 3e ordre), sur-échantillonneur polyphase.
// Tout est alloué dans prepare() ; les fonctions de traitement n'allouent jamais.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aura::dsp
{
inline constexpr double kPi    = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

/** Au-delà de cette fréquence d'échantillonnage, l'oversampling des étages non linéaires est coupé (ADAA seul). */
inline constexpr double kOversamplingOffRate = 176400.0;

/** Égalité exacte voulue (valeur figée, cible atteinte) sans avertissement -Wfloat-equal. */
constexpr bool exactlyEqual (double a, double b) noexcept { return ! (a < b) && ! (b < a); }

inline double dbToGain (double db) noexcept     { return std::pow (10.0, db * 0.05); }
inline double gainToDb (double g) noexcept      { return 20.0 * std::log10 (std::max (g, 1.0e-30)); }

/** Coefficient d'une moyenne exponentielle : ~63 % du chemin en `ms` millisecondes. */
inline double timeCoef (double ms, double sampleRate) noexcept
{
    if (ms <= 0.0) return 0.0;
    return std::exp (-1.0 / (ms * 0.001 * sampleRate));
}

inline std::size_t nextPow2 (std::size_t n) noexcept
{
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

//==============================================================================
/** Lissage exponentiel d'un paramètre (anti zipper-noise). */
class OnePoleSmoother
{
public:
    void prepare (double sampleRate, double timeMs) noexcept { coef = timeCoef (timeMs, sampleRate); }
    void reset (double v) noexcept                           { current = target = v; }
    void setTarget (double v) noexcept                       { target = v; }
    double getTarget() const noexcept                        { return target; }
    double getCurrent() const noexcept                       { return current; }

    double next() noexcept
    {
        current = target + coef * (current - target);
        if (std::abs (current - target) < 1.0e-12) current = target;
        return current;
    }

    bool isSmoothing() const noexcept { return ! exactlyEqual (current, target); }

private:
    double current = 0.0, target = 0.0, coef = 0.0;
};

//==============================================================================
/** Rampe linéaire de durée fixe (bypass 5 ms, fondus de permutation). */
class LinearRamp
{
public:
    void prepare (double sampleRate, double timeMs) noexcept
    {
        lengthSamples = std::max (1, (int) std::lround (timeMs * 0.001 * sampleRate));
    }

    void reset (double v) noexcept { current = target = v; remaining = 0; step = 0.0; }

    void setTarget (double v) noexcept
    {
        if (exactlyEqual (v, target)) return;
        target    = v;
        remaining = lengthSamples;
        step      = (target - current) / (double) lengthSamples;
    }

    double next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0) current = target;
        }
        return current;
    }

    bool isRamping() const noexcept     { return remaining > 0; }
    int getRemaining() const noexcept   { return remaining; }
    double getCurrent() const noexcept  { return current; }
    double getTarget() const noexcept   { return target; }
    int getLengthSamples() const noexcept { return lengthSamples; }

private:
    double current = 0.0, target = 0.0, step = 0.0;
    int remaining = 0, lengthSamples = 1;
};

//==============================================================================
/**
    Filtre à variables d'état, topologie TPT (intégrateurs trapézoïdaux, Zavalishin / Simper).
    Les coefficients passent par g = tan(pi·f/fs) : la réponse est exacte (transformation bilinéaire
    pré-déformée) et le filtre reste stable pour toute fréquence < fs/2, y compris 20 Hz à 384 kHz
    où un biquad direct en forme I/II perd sa précision. Calcul intégral en double.
*/
class TptSvf
{
public:
    enum class Type { lowPass, highPass, bandPass, notch, allPass, bell, lowShelf, highShelf };

    static constexpr int kMaxChannels = 2;

    void setup (Type type, double freqHz, double q, double gainDb, double sampleRate) noexcept
    {
        const double nyquistGuard = 0.49 * sampleRate;
        const double f  = std::clamp (freqHz, 1.0, nyquistGuard);
        const double qq = std::max (q, 0.025);
        const double A  = std::pow (10.0, gainDb / 40.0);
        double g = std::tan (kPi * f / sampleRate);
        double k = 1.0 / qq;

        switch (type)
        {
            case Type::lowPass:   m0 = 0.0; m1 = 0.0;  m2 = 1.0;  break;
            case Type::highPass:  m0 = 1.0; m1 = -k;   m2 = -1.0; break;
            case Type::bandPass:  m0 = 0.0; m1 = k;    m2 = 0.0;  break;   // gain unité au centre
            case Type::notch:     m0 = 1.0; m1 = -k;   m2 = 0.0;  break;
            case Type::allPass:   m0 = 1.0; m1 = -2.0 * k; m2 = 0.0; break;
            case Type::bell:
                k  = 1.0 / (qq * A);
                m0 = 1.0; m1 = k * (A * A - 1.0); m2 = 0.0;
                break;
            case Type::lowShelf:
                g /= std::sqrt (A);
                m0 = 1.0; m1 = k * (A - 1.0); m2 = A * A - 1.0;
                break;
            case Type::highShelf:
                g *= std::sqrt (A);
                m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A;
                break;
        }

        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    /** Low shelf à modulation rapide (gain par échantillon) : tanWarp = tan(pi·f/fs) précalculé, A = 10^(dB/40). */
    void setLowShelfFast (double tanWarp, double k, double A) noexcept
    {
        const double g = tanWarp / std::sqrt (A);
        m0 = 1.0; m1 = k * (A - 1.0); m2 = A * A - 1.0;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept
    {
        for (auto& s : ic1) s = 0.0;
        for (auto& s : ic2) s = 0.0;
    }

    double processSample (double v0, int ch) noexcept
    {
        double& s1 = ic1[(std::size_t) ch];
        double& s2 = ic2[(std::size_t) ch];
        const double v3 = v0 - s2;
        const double v1 = a1 * s1 + a2 * v3;
        const double v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0 * v1 - s1;
        s2 = 2.0 * v2 - s2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }

private:
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;
    std::array<double, kMaxChannels> ic1 {}, ic2 {};
};

//==============================================================================
/**
    Ligne de retard circulaire (taille puissance de 2), lecture entière ou fractionnaire (Lagrange 3e ordre).
    reset() est O(1) : un compteur d'échantillons valides fait lire 0 au-delà de ce qui a été écrit depuis.
    Une ligne de 2 s à 384 kHz peut ainsi être remise à zéro sur le thread audio sans effacer 6 Mo.
*/
class DelayLine
{
public:
    void prepare (std::size_t maxDelaySamples)
    {
        const std::size_t size = nextPow2 (maxDelaySamples + 8);
        buffer.assign (size, 0.0);
        mask = size - 1;
        writePos = 0;
        filled = 0;
    }

    void reset() noexcept { writePos = 0; filled = 0; }

    void push (double x) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[writePos] = x;
        if (filled < buffer.size()) ++filled;
    }

    /** Échantillon poussé il y a `d` échantillons (d = 0 : le dernier poussé). */
    double read (std::size_t d) const noexcept { return d < filled ? buffer[(writePos - d) & mask] : 0.0; }

    /** Lecture fractionnaire, d >= 1 : interpolation de Lagrange d'ordre 3 (4 points). */
    double readFractional (double d) const noexcept
    {
        d = std::max (d, 1.0);
        const auto   i = (std::size_t) d;
        const double t = d - (double) i;
        const double ym1 = read (i - 1);   // un échantillon plus récent
        const double y0  = read (i);
        const double y1  = read (i + 1);
        const double y2  = read (i + 2);
        const double c_m1 = -t * (t - 1.0) * (t - 2.0) / 6.0;
        const double c0   = (t + 1.0) * (t - 1.0) * (t - 2.0) / 2.0;
        const double c1   = -(t + 1.0) * t * (t - 2.0) / 2.0;
        const double c2   = (t + 1.0) * t * (t - 1.0) / 6.0;
        return c_m1 * ym1 + c0 * y0 + c1 * y1 + c2 * y2;
    }

    std::size_t capacity() const noexcept { return buffer.size() - 8; }

private:
    std::vector<double> buffer;
    std::size_t mask = 0, writePos = 0, filled = 0;
};

//==============================================================================
/** Filtre RIF passe-bas à fenêtre de Kaiser (conception hors temps réel). */
inline std::vector<double> designKaiserLowpass (int numTaps, double cutoffNormalised /* cycles/échantillon */, double beta)
{
    auto besselI0 = [] (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1.0e-18) break;
        }
        return sum;
    };

    std::vector<double> h ((std::size_t) numTaps);
    const double centre = 0.5 * (numTaps - 1);
    const double i0Beta = besselI0 (beta);
    double sum = 0.0;

    for (int n = 0; n < numTaps; ++n)
    {
        const double m = n - centre;
        const double sinc = exactlyEqual (m, 0.0) ? 2.0 * cutoffNormalised
                                       : std::sin (kTwoPi * cutoffNormalised * m) / (kPi * m);
        const double r = m / centre;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0Beta;
        h[(std::size_t) n] = sinc * w;
        sum += h[(std::size_t) n];
    }

    for (auto& c : h) c /= sum;   // gain DC exactement 1
    return h;
}

//==============================================================================
/**
    Étage ×2 polyphase (montée + descente), phase linéaire.
    Longueur impaire N avec (N-1) divisible par 4 : la latence aller-retour vaut (N-1)/2 échantillons
    à la fréquence haute, soit un nombre entier d'échantillons à la fréquence de base.
*/
class Oversampler2xStage
{
public:
    void prepare (int numTaps, double cutoff, double beta, int maxInputBlock)
    {
        h = designKaiserLowpass (numTaps, cutoff, beta);
        const auto half = (std::size_t) (numTaps + 1) / 2;
        even.assign (half, 0.0);
        odd.assign (half, 0.0);
        for (std::size_t i = 0; i < h.size(); ++i)
            (i % 2 == 0 ? even[i / 2] : odd[i / 2]) = 2.0 * h[i];   // ×2 : compense l'insertion de zéros

        upHist.assign (2 * half * TptSvf::kMaxChannels, 0.0);
        downHist.assign (2 * h.size() * TptSvf::kMaxChannels, 0.0);
        upPos = { 0, 0 };
        downPos = { 0, 0 };
        histUp = half;
        histDown = h.size();
        upBuffer.assign ((std::size_t) maxInputBlock * 2 * TptSvf::kMaxChannels, 0.0);
        maxBlock = maxInputBlock;
    }

    void reset() noexcept
    {
        std::fill (upHist.begin(), upHist.end(), 0.0);
        std::fill (downHist.begin(), downHist.end(), 0.0);
        upPos = { 0, 0 };
        downPos = { 0, 0 };
    }

    /** Latence aller-retour en échantillons de la fréquence d'entrée de l'étage. */
    double roundTripLatencyInput() const noexcept { return 0.5 * (double) (h.size() - 1); }

    /** n échantillons d'entrée -> 2n échantillons dans out. */
    void upsample (const double* in, double* out, int n, int ch) noexcept
    {
        double* hist = upHist.data() + (std::size_t) ch * 2 * histUp;
        auto& pos = upPos[(std::size_t) ch];
        for (int i = 0; i < n; ++i)
        {
            pos = (pos == 0 ? histUp : pos) - 1;
            hist[pos] = hist[pos + histUp] = in[i];   // double écriture : lecture contiguë sans modulo
            double y0 = 0.0, y1 = 0.0;
            const double* x = hist + pos;
            for (std::size_t k = 0; k < histUp; ++k)
            {
                y0 += even[k] * x[k];
                y1 += odd[k] * x[k];
            }
            out[2 * i]     = y0;
            out[2 * i + 1] = y1;
        }
    }

    /** 2n échantillons d'entrée (fréquence haute) -> n échantillons dans out. */
    void downsample (const double* in, double* out, int n, int ch) noexcept
    {
        double* hist = downHist.data() + (std::size_t) ch * 2 * histDown;
        auto& pos = downPos[(std::size_t) ch];
        // L'échantillon gardé est celui de phase paire (indice 2i) : la latence reste un nombre entier
        // d'échantillons de base ; l'échantillon impair n'entre que dans l'historique.
        for (int i = 0; i < n; ++i)
        {
            pos = (pos == 0 ? histDown : pos) - 1;
            hist[pos] = hist[pos + histDown] = in[2 * i];
            const double* x = hist + pos;
            double y = 0.0;
            for (std::size_t k = 0; k < histDown; ++k)
                y += h[k] * x[k];
            out[i] = y;

            pos = (pos == 0 ? histDown : pos) - 1;
            hist[pos] = hist[pos + histDown] = in[2 * i + 1];
        }
    }

    double* scratch (int ch) noexcept { return upBuffer.data() + (std::size_t) ch * 2 * (std::size_t) maxBlock; }

private:
    std::vector<double> h, even, odd, upHist, downHist, upBuffer;
    std::size_t histUp = 0, histDown = 0;
    std::array<std::size_t, TptSvf::kMaxChannels> upPos {}, downPos {};
    int maxBlock = 0;
};

//==============================================================================
/**
    Sur-échantillonneur 1× / 2× / 4× pour les étages non linéaires.
    Facteur choisi d'après la fréquence : 4× sous 88,2 kHz, 2× sous 176,4 kHz, 1× au-delà (DXD : ADAA seul).
    Étage 1 : 127 coefficients (bande passante plate jusqu'à ~0,45·fs), étage 2 : 65 coefficients.
*/
class AdaptiveOversampler
{
public:
    static int factorForRate (double sampleRate) noexcept
    {
        if (sampleRate >= kOversamplingOffRate) return 1;
        if (sampleRate >= 88200.0)              return 2;
        return 4;
    }

    void prepare (double sampleRate, int maxBlock)
    {
        factor = factorForRate (sampleRate);
        if (factor >= 2) stage1.prepare (127, 0.25 * 0.95, 10.0, maxBlock);
        if (factor >= 4) stage2.prepare (65, 0.25 * 0.70, 10.0, maxBlock * 2);
        latency = 0;
        if (factor >= 2) latency += (int) stage1.roundTripLatencyInput();
        if (factor >= 4) latency += (int) (stage2.roundTripLatencyInput() / 2.0);
    }

    void reset() noexcept { stage1.reset(); stage2.reset(); }

    int getFactor() const noexcept  { return factor; }
    int getLatency() const noexcept { return latency; }

    /** Monte n échantillons, renvoie le tampon haute fréquence (n·factor échantillons). */
    double* up (const double* in, int n, int ch) noexcept
    {
        if (factor == 1) return nullptr;
        double* s1 = stage1.scratch (ch);
        stage1.upsample (in, s1, n, ch);
        if (factor == 2) return s1;
        double* s2 = stage2.scratch (ch);
        stage2.upsample (s1, s2, 2 * n, ch);
        return s2;
    }

    /** Redescend le tampon renvoyé par up() vers out (n échantillons). */
    void down (double* high, double* out, int n, int ch) noexcept
    {
        if (factor == 1) return;
        if (factor == 4)
        {
            double* s1 = stage1.scratch (ch);
            stage2.downsample (high, s1, 2 * n, ch);
            stage1.downsample (s1, out, n, ch);
        }
        else
        {
            stage1.downsample (high, out, n, ch);
        }
    }

private:
    Oversampler2xStage stage1, stage2;
    int factor = 1, latency = 0;
};

//==============================================================================
/** Minimum glissant sur une fenêtre fixe (file monotone, O(1) amorti, sans allocation). */
class SlidingMinimum
{
public:
    void prepare (int windowLength)
    {
        window = std::max (1, windowLength);
        values.assign ((std::size_t) window + 1, 0.0);
        stamps.assign ((std::size_t) window + 1, 0);
        reset();
    }

    void reset() noexcept { head = tail = count = 0; time = 0; }

    double push (double v) noexcept
    {
        const std::size_t cap = values.size();
        while (count > 0)
        {
            const std::size_t last = (tail + cap - 1) % cap;
            if (values[last] < v) break;
            tail = last;
            --count;
        }
        values[tail] = v;
        stamps[tail] = time;
        tail = (tail + 1) % cap;
        ++count;

        while (stamps[head] + (std::int64_t) window <= time)
        {
            head = (head + 1) % cap;
            --count;
        }
        ++time;
        return values[head];
    }

private:
    std::vector<double> values;
    std::vector<std::int64_t> stamps;
    std::size_t head = 0, tail = 0, count = 0;
    std::int64_t time = 0;
    int window = 1;
};

} // namespace aura::dsp
