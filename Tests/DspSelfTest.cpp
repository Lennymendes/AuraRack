// AuraRack — NextDSP
// Banc d'essai du DSP et du moteur de rack, sans JUCE. Code de sortie 0 = tout passe.
//   aurarack_dsp_test [filtre]

#include "../Source/Engine/RackEngine.h"

#include <atomic>
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <random>
#include <string>
#include <thread>

//==============================================================================
// Compteur d'allocations (actif seulement pendant les mesures).
static std::atomic<bool> gCountAllocs { false };
static std::atomic<long> gAllocs { 0 };

void* operator new (std::size_t n)
{
    if (gCountAllocs.load (std::memory_order_relaxed)) gAllocs.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }   // NOLINT : paire de l'operator new ci-dessus
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace aura;
using Buf = std::vector<double>;

static int gChecks = 0, gFailures = 0;

static void check (bool ok, const std::string& what, double value = 0.0, const char* unit = "")
{
    ++gChecks;
    if (! ok) ++gFailures;
    std::printf ("  [%s] %-72s %12.4f %s\n", ok ? " OK " : "FAIL", what.c_str(), value, unit);
}

//==============================================================================
// Outils de mesure.
static double toneAmplitude (const Buf& x, std::size_t start, std::size_t len, double freq, double fs)
{
    // Corrélation avec sin / cos sur un nombre entier de périodes (projection exacte).
    double s = 0.0, c = 0.0;
    for (std::size_t i = start; i < start + len; ++i)
    {
        const double ph = dsp::kTwoPi * freq * (double) i / fs;
        s += x[i] * std::sin (ph);
        c += x[i] * std::cos (ph);
    }
    return 2.0 * std::sqrt (s * s + c * c) / (double) len;
}

static void fft (std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -dsp::kTwoPi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Puissance de toutes les raies non harmoniques / puissance totale (dB). f0 tombe sur la raie k0. */
static double aliasRatioDb (const Buf& x, std::size_t start, std::size_t n, std::size_t k0)
{
    std::vector<std::complex<double>> a (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (dsp::kTwoPi * (double) i / (double) n);   // Hann
        a[i] = x[start + i] * w;
    }
    fft (a);
    double total = 0.0, alias = 0.0;
    for (std::size_t k = 1; k < n / 2; ++k)
    {
        const double p = std::norm (a[k]);
        total += p;
        const std::size_t r = k % k0;
        const bool harmonic = (r <= 2 || r >= k0 - 2);   // lobe principal de Hann ± 2 raies
        if (! harmonic) alias += p;
    }
    return 10.0 * std::log10 (alias / total + 1e-300);
}

static double peakAbs (const Buf& x, std::size_t a, std::size_t b)
{
    double p = 0.0;
    for (std::size_t i = a; i < b; ++i) p = std::max (p, std::abs (x[i]));
    return p;
}

/** True peak de référence : interpolation sinc fenêtrée ×16, indépendante du limiteur. */
static double truePeak16 (const Buf& x, std::size_t a, std::size_t b)
{
    constexpr int over = 16, half = 32;
    double p = 0.0;
    for (std::size_t i = a + half; i < b - half; ++i)
        for (int ph = 0; ph < over; ++ph)
        {
            const double t = (double) ph / over;
            double y = 0.0;
            for (int k = -half + 1; k <= half; ++k)
            {
                const double m = (double) k - t;
                const double sinc = std::abs (m) < 1e-12 ? 1.0 : std::sin (dsp::kPi * m) / (dsp::kPi * m);
                const double w = 0.5 + 0.5 * std::cos (dsp::kPi * m / half);
                y += x[(std::size_t) ((long) i + k)] * sinc * w;
            }
            p = std::max (p, std::abs (y));
        }
    return p;
}

/** Fait passer L/R dans un module, par blocs de `block`. */
static void runModule (AuraModuleDSP& m, Buf& L, Buf& R, int block = 256, const Buf* scL = nullptr, const Buf* scR = nullptr)
{
    for (std::size_t off = 0; off < L.size(); off += (std::size_t) block)
    {
        const int n = (int) std::min<std::size_t> ((std::size_t) block, L.size() - off);
        SidechainBlock key;
        if (scL != nullptr) key = { scL->data() + off, scR->data() + off, true };
        m.process (L.data() + off, R.data() + off, n, key);
    }
}

static void setParam (AuraModuleDSP& m, int idx, double plain) { m.setParameter (idx, plain); }

/** Bruit gaussien portable (Box-Muller sur mt19937, normalisé par le C++) : mêmes valeurs avec GCC, Clang et MSVC,
    contrairement à std::normal_distribution dont l'algorithme dépend de la bibliothèque standard. */
struct PortableGauss
{
    explicit PortableGauss (unsigned seed, double sigmaIn) : rng (seed), sigma (sigmaIn) {}
    double operator()()
    {
        const double u1 = ((double) rng() + 1.0) / 4294967297.0;
        const double u2 = (double) rng() / 4294967296.0;
        return sigma * std::sqrt (-2.0 * std::log (u1)) * std::cos (dsp::kTwoPi * u2);
    }
    std::mt19937 rng;
    double sigma;
};

static Buf sine (std::size_t n, double f, double fs, double amp, double phase = 0.0)
{
    Buf x (n);
    for (std::size_t i = 0; i < n; ++i) x[i] = amp * std::sin (dsp::kTwoPi * f * (double) i / fs + phase);
    return x;
}

//==============================================================================
static void testSvf()
{
    std::puts ("\n== 1. SVF TPT (stabilité et exactitude jusqu'à 384 kHz)");
    for (double fs : { 44100.0, 352800.0, 384000.0 })
    {
        dsp::TptSvf f;
        f.setup (dsp::TptSvf::Type::bell, 1000.0, 1.0, 12.0, fs);
        auto x = sine ((std::size_t) fs, 1000.0, fs, 0.25);
        Buf y (x.size());
        for (std::size_t i = 0; i < x.size(); ++i) y[i] = f.processSample (x[i], 0);
        const auto n = (std::size_t) (fs / 2);
        const double g = dsp::gainToDb (toneAmplitude (y, n, n, 1000.0, fs) / 0.25);
        check (std::abs (g - 12.0) < 0.001, "Peak 1 kHz +12 dB @ " + std::to_string ((int) fs) + " Hz : gain mesuré", g, "dB");

        dsp::TptSvf ls;
        ls.setup (dsp::TptSvf::Type::lowShelf, 20.0, 0.7071, 6.0, fs);
        auto lo = sine ((std::size_t) fs * 2, 2.0, fs, 0.25);
        Buf yl (lo.size());
        for (std::size_t i = 0; i < lo.size(); ++i) yl[i] = ls.processSample (lo[i], 0);
        const double gl = dsp::gainToDb (toneAmplitude (yl, (std::size_t) fs, (std::size_t) fs, 2.0, fs) / 0.25);
        check (std::abs (gl - 6.0) < 0.05, "Low shelf 20 Hz +6 dB, mesure à 2 Hz @ " + std::to_string ((int) fs), gl, "dB");

        dsp::TptSvf hp;
        hp.setup (dsp::TptSvf::Type::highPass, 20.0, 0.7071, 0.0, fs);
        double e = 0.0, last = 0.0;
        for (int i = 0; i < (int) fs * 3; ++i) { last = hp.processSample (i == 0 ? 1.0 : 0.0, 0); e += last * last; }
        check (std::isfinite (e) && std::abs (last) < 1e-9, "HP 20 Hz : réponse impulsionnelle finie et éteinte après 3 s", std::abs (last));
    }
}

static void testEq()
{
    std::puts ("\n== 2. AuraEQ");
    const double fs = 384000.0;
    AuraEQ eq;
    eq.prepare (fs, 256);
    auto L = sine (100000, 997.0, fs, 0.5), R = L;
    const auto refL = L;
    runModule (eq, L, R);
    double err = 0.0;
    for (std::size_t i = 0; i < L.size(); ++i) err = std::max (err, std::abs (L[i] - refL[i]));
    check (err == 0.0, "Toutes bandes à 0 dB : sortie bit-identique à l'entrée", err);

    eq.setParameter (AuraEQ::pkFreq, 3000.0);
    eq.setParameter (AuraEQ::pkGain, -9.0);
    eq.setParameter (AuraEQ::pkQ, 2.0);
    eq.reset();
    auto x = sine ((std::size_t) fs, 3000.0, fs, 0.5), xr = x;
    runModule (eq, x, xr);
    const double g = dsp::gainToDb (toneAmplitude (x, (std::size_t) fs / 2, (std::size_t) fs / 2, 3000.0, fs) / 0.5);
    check (std::abs (g + 9.0) < 0.001, "Peak 3 kHz -9 dB Q 2 @ 384 kHz", g, "dB");
}

static void testComp()
{
    std::puts ("\n== 3. AuraComp");
    const double fs = 96000.0;
    AuraComp c;
    c.prepare (fs, 256);
    setParam (c, AuraComp::threshold, -18.0);
    setParam (c, AuraComp::ratio, 4.0);
    setParam (c, AuraComp::knee, 0.0);
    setParam (c, AuraComp::attack, 1.0);
    Buf L ((std::size_t) fs, 0.5), R = L;
    runModule (c, L, R);
    const double expected = (dsp::gainToDb (0.5) + 18.0) * 0.75;
    const double measured = dsp::gainToDb (0.5 / L.back());
    check (std::abs (measured - expected) < 0.01, "Réduction statique (DC -6 dB, seuil -18, 4:1) = " + std::to_string (expected), measured, "dB");

    setParam (c, AuraComp::knee, 12.0);
    const double inKnee = c.gainReductionFor (-18.0);
    check (std::abs (inKnee - 0.75 * 36.0 / 24.0) < 1e-9, "Genou 12 dB : réduction au seuil = (1-1/R)·(W/2)²/(2W)", inKnee, "dB");
}

static void testDuck()
{
    std::puts ("\n== 4. AuraDuck (kick -> 808, Dynamic EQ)");
    const double fs = 352800.0;
    const auto n = (std::size_t) (fs * 2.0);
    // Programme : 808 à 45 Hz + charleston à 6 kHz. Clé : kick 60 Hz de 120 ms toutes les 500 ms.
    auto L = sine (n, 45.0, fs, 0.5);
    auto hat = sine (n, 6000.0, fs, 0.1);
    for (std::size_t i = 0; i < n; ++i) L[i] += hat[i];
    auto R = L;
    Buf kick (n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = std::fmod ((double) i / fs, 0.5);
        if (t < 0.12) kick[i] = 0.8 * std::sin (dsp::kTwoPi * 60.0 * t) * std::exp (-t * 10.0);
    }
    AuraDuck d;
    d.prepare (fs, 256);
    setParam (d, AuraDuck::threshold, -30.0);
    setParam (d, AuraDuck::depth, 12.0);
    setParam (d, AuraDuck::mode, 1.0);
    setParam (d, AuraDuck::subFreq, 120.0);
    setParam (d, AuraDuck::attack, 0.5);
    setParam (d, AuraDuck::release, 60.0);
    std::vector<double> grTrace;
    for (std::size_t off = 0; off < n; off += 256)
    {
        const int m = (int) std::min<std::size_t> (256, n - off);
        d.process (L.data() + off, R.data() + off, m, { kick.data() + off, kick.data() + off, true });
        grTrace.push_back (d.getGainReductionDb());
    }
    // Fenêtre au cœur du kick (40–100 ms après l'attaque du 3e kick) et hors kick (350–450 ms).
    const auto a0 = (std::size_t) (fs * 1.04), a1 = (std::size_t) (fs * 1.10);
    const auto b0 = (std::size_t) (fs * 1.35), b1 = (std::size_t) (fs * 1.45);
    auto bandAmp = [&] (double f, std::size_t s, std::size_t e)
    {
        const auto per = (std::size_t) std::round (fs / f);
        const auto len = ((e - s) / per) * per;
        return toneAmplitude (L, s, len, f, fs);
    };
    const double subDuck = dsp::gainToDb (bandAmp (45.0, a0, a1) / 0.5);
    const double subFree = dsp::gainToDb (bandAmp (45.0, b0, b1) / 0.5);
    const double hatDuck = dsp::gainToDb (bandAmp (6000.0, a0, a1) / 0.1);
    check (subDuck < -9.0, "Sub 45 Hz pendant le kick (Depth 12 dB)", subDuck, "dB");
    check (std::abs (subFree) < 0.2, "Sub 45 Hz entre les kicks (relâché)", subFree, "dB");
    check (std::abs (hatDuck) < 0.1, "Charleston 6 kHz pendant le kick (intouché)", hatDuck, "dB");

    // Ondulation de l'enveloppe pendant le kick : le maintien de crête supprime le suivi des alternances.
    double gMin = 1e9, gMax = -1e9;
    for (std::size_t k = a0 / 256; k < a1 / 256; ++k) { gMin = std::min (gMin, grTrace[k]); gMax = std::max (gMax, grTrace[k]); }
    check (gMax - gMin < 0.5, "Ondulation de la réduction pendant le kick (anti-distorsion BF)", gMax - gMin, "dB");
}

static void testClip()
{
    std::puts ("\n== 5. AuraClip (ADAA, sur-échantillonnage adaptatif)");
    for (double fs : { 44100.0, 96000.0, 352800.0 })
    {
        AuraClip c;
        c.prepare (fs, 256);
        const int factor = dsp::AdaptiveOversampler::factorForRate (fs);
        check (factor == (fs >= 176400.0 ? 1 : (fs >= 88200.0 ? 2 : 4)),
               "Facteur de sur-échantillonnage @ " + std::to_string ((int) fs), factor, "x");

        setParam (c, AuraClip::drive, 18.0);
        setParam (c, AuraClip::ceiling, -1.0);
        setParam (c, AuraClip::knee, 0.0);
        c.reset();
        const std::size_t N = 16384;
        const std::size_t k0 = (std::size_t) std::round (N * 7000.0 / fs) | 1u;   // raie impaire
        const double f0 = (double) k0 * fs / (double) N;
        auto L = sine (N * 3, f0, fs, 0.9), R = L;
        auto naive = L;
        runModule (c, L, R);
        const double ceil = dsp::dbToGain (-1.0);
        for (auto& v : naive) v = std::clamp (v * dsp::dbToGain (18.0) / ceil, -1.0, 1.0) * ceil;
        const double pk = peakAbs (L, N, 3 * N);
        // En DXD (ADAA seul) le plafond est exact ; sur-échantillonné, le filtre de redescente déborde (Gibbs).
        if (factor == 1)
            check (pk <= ceil * (1.0 + 1e-9), "Crête de sortie = plafond exact (-1 dBFS, drive 18 dB)", dsp::gainToDb (pk), "dBFS");
        else
            check (pk <= ceil * dsp::dbToGain (1.5), "Crête de sortie <= plafond + 1,5 dB (débord du filtre)", dsp::gainToDb (pk), "dBFS");
        const double aliasClip = aliasRatioDb (L, N, N, k0);
        const double aliasNaive = aliasRatioDb (naive, N, N, k0);
        check (aliasClip < aliasNaive - (fs < 176400.0 ? 25.0 : 6.0),
               "Repliement : AuraClip " + std::to_string (aliasClip).substr (0, 6) + " dB vs hard clip naïf", aliasNaive, "dB");

        // Latence annoncée : le signal non écrêté ressort aligné (corrélation maximale à la latence).
        AuraClip lin;
        lin.prepare (fs, 256);
        setParam (lin, AuraClip::ceiling, 0.0);
        setParam (lin, AuraClip::knee, 0.0);
        lin.reset();
        Buf imp (4096, 0.0), ir;
        PortableGauss nd (7, 0.05);
        for (auto& v : imp) v = nd();
        auto a = imp, b = imp;
        runModule (lin, a, b);
        const int lat = lin.getLatencySamples();
        int best = -1; double bestC = -1e9;
        for (int d = 0; d < 200; ++d)
        {
            double cc = 0.0;
            for (std::size_t i = 300; i < 3500; ++i) cc += a[i] * imp[i - (std::size_t) d];
            if (cc > bestC) { bestC = cc; best = d; }
        }
        // L'ADAA ajoute un demi-échantillon de la fréquence de traitement (1/(2·facteur) d'échantillon de base).
        const double expectedDelay = lat + 0.5 / factor;
        check (std::abs (best - expectedDelay) <= 0.5, "Latence annoncée (+ ½ éch. ADAA) = alignement mesuré @ " + std::to_string ((int) fs), best, "éch.");
    }
}

static void testLimit()
{
    std::puts ("\n== 6. AuraLimit (True Peak, lookahead 1,5 ms)");
    for (double fs : { 44100.0, 384000.0 })
    {
        AuraLimit lim;
        lim.prepare (fs, 256);
        setParam (lim, AuraLimit::inputGain, 12.0);
        setParam (lim, AuraLimit::ceiling, -1.0);
        setParam (lim, AuraLimit::release, 50.0);
        lim.reset();
        PortableGauss nd (3, 0.25);
        const auto n = (std::size_t) (fs * 0.5);
        Buf L (n), R (n);
        // Bruit rose approximatif + transitoires : le cas difficile pour les crêtes inter-échantillons.
        double b0 = 0.0, b1 = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double w = nd();
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            L[i] = b0 + b1 + 0.1 * w;
            if (i % (std::size_t) (fs * 0.05) == 0) L[i] += 0.9;
        }
        // Programme réaliste : limité à 20 kHz (44,1 kHz) ou 40 kHz (DXD, contenu ultrasonore compris).
        dsp::TptSvf bl1, bl2;
        const double bw = fs < 100000.0 ? 20000.0 : 40000.0;
        bl1.setup (dsp::TptSvf::Type::lowPass, bw, 0.5412, 0.0, fs);
        bl2.setup (dsp::TptSvf::Type::lowPass, bw, 1.3066, 0.0, fs);
        for (std::size_t i = 0; i < n; ++i) { L[i] = bl2.processSample (bl1.processSample (L[i], 0), 0); R[i] = -L[i]; }
        runModule (lim, L, R);
        const auto lat = (std::size_t) lim.getLatencySamples();
        const double tp = dsp::gainToDb (truePeak16 (L, lat + 100, n));
        check (tp <= -1.0 + 0.05, "True peak de sortie (plafond -1 dBTP, +12 dB) @ " + std::to_string ((int) fs), tp, "dBTP");
        const double expected = std::round (1.5e-3 * fs);
        check ((double) lat >= expected && (double) lat <= expected + 20, "Latence = 1,5 ms + filtre de détection", (double) lat, "éch.");

        // En dessous du plafond : transparent et aligné.
        AuraLimit clean;
        clean.prepare (fs, 256);
        Buf a (4096, 0.0), b (4096, 0.0);
        a[100] = 0.5; b[100] = 0.5;
        runModule (clean, a, b);
        const auto l2 = (std::size_t) clean.getLatencySamples();
        check (std::abs (a[100 + l2] - 0.5) < 1e-12, "Sous le plafond : impulsion intacte à l'échantillon 100 + latence", a[100 + l2]);
    }
}

static void testDelay()
{
    std::puts ("\n== 7. AuraDelay (ping-pong, Lagrange)");
    const double fs = 352800.0;
    AuraDelay d;
    d.prepare (fs, 256);
    setParam (d, AuraDelay::time, 10.0);
    setParam (d, AuraDelay::feedback, 50.0);
    setParam (d, AuraDelay::pingPong, 100.0);
    setParam (d, AuraDelay::lowCut, 20.0);
    setParam (d, AuraDelay::highCut, 20000.0);
    d.reset();
    // Salve de 2 kHz (fenêtre de Hann, 1 ms) : bien dans la bande passante des filtres de boucle.
    const auto burstLen = (std::size_t) (0.001 * fs);
    Buf burst (burstLen);
    double be = 0.0;
    for (std::size_t j = 0; j < burstLen; ++j)
    {
        const double w = 0.5 - 0.5 * std::cos (dsp::kTwoPi * (double) j / (double) burstLen);
        burst[j] = w * std::sin (dsp::kTwoPi * 2000.0 * (double) j / fs);
        be += burst[j] * burst[j];
    }
    const auto n = (std::size_t) (fs * 0.05);
    Buf L (n, 0.0), R (n, 0.0);
    std::copy (burst.begin(), burst.end(), L.begin());
    std::copy (burst.begin(), burst.end(), R.begin());
    runModule (d, L, R);

    const auto T = (std::size_t) std::lround (0.010 * fs);
    // Position (corrélation maximale autour de k·T) et amplitude relative de chaque écho.
    auto echo = [&] (const Buf& x, std::size_t centre)
    {
        double bestC = -1e9; long bestD = 0;
        for (long dd = -20; dd <= 20; ++dd)
        {
            double c = 0.0;
            for (std::size_t j = 0; j < burstLen; ++j) c += x[(std::size_t) ((long) centre + dd) + j] * burst[j];
            if (c > bestC) { bestC = c; bestD = dd; }
        }
        return std::pair<long, double> { bestD, bestC / be };
    };
    const auto [d1, a1] = echo (L, T);
    const auto [d2, a2] = echo (R, 2 * T);
    const auto [d3, a3] = echo (L, 3 * T);
    check (std::abs (d1) <= 1 && std::abs (a1 - 1.0) < 0.01, "1er écho à gauche, 10 ms, amplitude 1", (double) d1, "éch.");
    check (std::abs (d2) <= 1 && std::abs (a2 - 0.5) < 0.01, "2e écho à droite, 20 ms, amplitude 0,5", (double) d2, "éch.");
    check (std::abs (d3) <= 1 && std::abs (a3 - 0.25) < 0.01, "3e écho à gauche, 30 ms, amplitude 0,25", (double) d3, "éch.");
    check (peakAbs (R, T / 2, T + T / 2) < 1e-3 && peakAbs (L, T + T / 2, 2 * T + T / 2) < 1e-3, "Alternance stricte L / R (ping-pong 100 %)", 0.0);

    // Glissement du temps : pas de discontinuité sur un sinus retardé.
    AuraDelay g;
    g.prepare (fs, 256);
    setParam (g, AuraDelay::time, 20.0);
    setParam (g, AuraDelay::feedback, 0.0);
    setParam (g, AuraDelay::pingPong, 0.0);
    g.reset();
    auto s = sine ((std::size_t) fs, 200.0, fs, 0.5), sr = s;
    runModule (g, s, sr, 256);
    auto s2 = sine ((std::size_t) fs, 200.0, fs, 0.5), sr2 = s2;
    setParam (g, AuraDelay::time, 35.0);
    runModule (g, s2, sr2, 256);
    double jump = 0.0;
    for (std::size_t i = 1; i < s2.size(); ++i) jump = std::max (jump, std::abs (s2[i] - s2[i - 1]));
    const double bound = 2.0 * dsp::kTwoPi * 200.0 / fs * 0.5 * 1.2;   // pente max avec glissement (×2) + marge
    check (jump < bound, "Changement de temps 20 -> 35 ms : pente max (pas de clic)", jump);
}

static void testVerb()
{
    std::puts ("\n== 8. AuraVerb (Dattorro, RT60)");
    for (double fs : { 48000.0, 384000.0 })
    {
        AuraVerb v;
        v.prepare (fs, 256);
        setParam (v, AuraVerb::decay, 2.0);
        setParam (v, AuraVerb::damping, 20000.0);
        setParam (v, AuraVerb::preDelay, 0.0);
        setParam (v, AuraVerb::lowCut, 20.0);
        v.reset();
        const auto n = (std::size_t) (fs * 3.0);
        Buf L (n, 0.0), R (n, 0.0);
        L[0] = R[0] = 1.0;
        runModule (v, L, R);
        // Mesure normalisée (ISO 3382) : bande d'octave 1 kHz, intégrale de Schroeder, pente -5 / -25 dB (T20).
        dsp::TptSvf b1, b2;
        b1.setup (dsp::TptSvf::Type::bandPass, 1000.0, 1.41, 0.0, fs);
        b2.setup (dsp::TptSvf::Type::bandPass, 1000.0, 1.41, 0.0, fs);
        for (std::size_t i = 0; i < n; ++i)
        {
            L[i] = b2.processSample (b1.processSample (L[i], 0), 0);
            R[i] = b2.processSample (b1.processSample (R[i], 1), 1);
        }
        Buf edc (n);
        double acc = 0.0;
        for (std::size_t i = n; i-- > 0;) { acc += L[i] * L[i] + R[i] * R[i]; edc[i] = acc; }
        const double e0 = edc[0];
        std::size_t i5 = 0, i25 = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double db = 10.0 * std::log10 (edc[i] / e0);
            if (i5 == 0 && db <= -5.0) i5 = i;
            if (i25 == 0 && db <= -25.0) { i25 = i; break; }
        }
        const double rt60 = 3.0 * (double) (i25 - i5) / fs;
        check (std::abs (rt60 - 2.0) < 0.3, "RT60 mesuré (Decay = 2,0 s) @ " + std::to_string ((int) fs), rt60, "s");
        bool finite = true;
        for (double x : L) finite = finite && std::isfinite (x);
        check (finite && peakAbs (L, 0, n) < 2.0, "Queue finie et bornée (bande 1 kHz)", peakAbs (L, 0, n));
    }
}

//==============================================================================
struct TestParams
{
    struct Slot
    {
        std::atomic<float> bypass { 0.0f }, mix { 100.0f }, inTrim { 0.0f }, outTrim { 0.0f };
        std::array<std::atomic<float>, kMaxModuleParams> p {};
    };
    std::array<Slot, kNumSlots> slots;
    std::atomic<float> inGain { 0.0f }, outGain { 0.0f }, masterMix { 100.0f }, scSource { 0.0f };

    void bind (RackEngine& e)
    {
        for (int s = 0; s < kNumSlots; ++s)
        {
            auto& sl = slots[(std::size_t) s];
            SlotParameterSources src { &sl.bypass, &sl.mix, &sl.inTrim, &sl.outTrim, {} };
            for (int i = 0; i < kMaxModuleParams; ++i) src.params[(std::size_t) i] = &sl.p[(std::size_t) i];
            e.bindSlot (s, src);
        }
        e.bindGlobal ({ &inGain, &outGain, &masterMix, &scSource });
    }

    void setType (RackEngine& e, int slot, ModuleType t)
    {
        const auto& info = moduleInfo (t);
        for (std::size_t i = 0; i < info.params.size(); ++i)
            slots[(std::size_t) slot].p[i] = (float) info.params[i].defaultNormalised();
        slots[(std::size_t) slot].mix = (float) info.defaultMixPercent;
        e.setModuleType (slot, t);
    }

    void setPlain (int slot, ModuleType t, int idx, double plain)
    {
        slots[(std::size_t) slot].p[(std::size_t) idx] = (float) moduleInfo (t).params[(std::size_t) idx].toNormalised (plain);
    }
};

static double maxStep (const Buf& x, std::size_t a, std::size_t b)
{
    double m = 0.0;
    for (std::size_t i = std::max<std::size_t> (a, 1); i < b; ++i) m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

static void runEngine (RackEngine& e, Buf& L, Buf& R, std::size_t from, std::size_t to, int block,
                       const std::function<void (std::size_t)>& atBlock = nullptr)
{
    for (std::size_t off = from; off < to; off += (std::size_t) block)
    {
        if (atBlock) atBlock (off);
        const int n = (int) std::min<std::size_t> ((std::size_t) block, to - off);
        e.process (L.data() + off, R.data() + off, n, nullptr, nullptr, false);
    }
}

static void testEngine()
{
    std::puts ("\n== 9. Moteur du rack (lock-free, 0 allocation, latence, fondus)");
    const double fs = 352800.0;

    {
        RackEngine e;
        TestParams p;
        p.bind (e);
        e.prepare (fs, 512);
        auto L = sine (100000, 1234.5, fs, 0.7), R = sine (100000, 777.0, fs, 0.3);
        const auto rl = L, rr = R;
        runEngine (e, L, R, 0, L.size(), 512);
        bool same = true;
        for (std::size_t i = 0; i < L.size(); ++i) same = same && L[i] == rl[i] && R[i] == rr[i];
        check (same, "Rack vide : sortie bit-identique à l'entrée (64 bits)", 0.0);
    }

    {
        // Compensation : Limit (latence) + Clip (pas de latence à 352,8 kHz) avec Mix 50 %, Master Mix 50 %.
        RackEngine e;
        TestParams p;
        p.bind (e);
        e.prepare (fs, 512);
        p.setType (e, 0, ModuleType::limit);
        p.setType (e, 3, ModuleType::eq);
        p.slots[0].mix = 50.0f;
        p.masterMix = 50.0f;
        e.prepare (fs, 512);
        const int lat = e.getRequestedLatency();
        Buf L (8192, 0.0), R (8192, 0.0);
        L[1000] = R[1000] = 0.25;
        runEngine (e, L, R, 0, L.size(), 333);
        double rest = 0.0;
        for (std::size_t i = 0; i < L.size(); ++i) if (i != (std::size_t) (1000 + lat)) rest = std::max (rest, std::abs (L[i]));
        check (std::abs (L[(std::size_t) (1000 + lat)] - 0.25) < 1e-12 && rest < 1e-12,
               "Mix 50 % + Master 50 % : une seule impulsion, amplitude exacte, à la latence " + std::to_string (lat),
               L[(std::size_t) (1000 + lat)]);
        AuraLimit reference;
        reference.prepare (fs, 256);
        check (lat == reference.getLatencySamples(), "Latence du rack = somme des latences des modules", lat, "éch.");
    }

    {
        // Bypass, réordonnancement et échange de module à chaud : aucun saut d'échantillon anormal, 0 allocation.
        RackEngine e;
        TestParams p;
        p.bind (e);
        e.prepare (fs, 512);
        p.setType (e, 0, ModuleType::eq);
        p.setPlain (0, ModuleType::eq, AuraEQ::lsGain, 9.0);
        p.setType (e, 1, ModuleType::comp);
        p.setType (e, 2, ModuleType::duck);
        p.setType (e, 3, ModuleType::clip);
        p.setPlain (3, ModuleType::clip, AuraClip::drive, 12.0);
        p.setType (e, 4, ModuleType::limit);
        p.setType (e, 5, ModuleType::delay);
        p.setType (e, 6, ModuleType::verb);
        p.scSource = 1.0f;
        e.collectGarbage();

        const auto n = (std::size_t) (fs * 1.5);
        auto L = sine (n, 440.0, fs, 0.5), R = sine (n, 440.0, fs, 0.5, 0.3);
        // Signal de référence : pente maximale « naturelle » de la sortie en régime établi.
        Buf warm = L, warmR = R;
        runEngine (e, warm, warmR, 0, (std::size_t) (fs * 0.3), 512);
        const double natural = maxStep (warm, (std::size_t) (fs * 0.2), (std::size_t) (fs * 0.3));

        const std::function<void (std::size_t)> automation = [&] (std::size_t off)
        {
            const double t = (double) off / fs;
            p.slots[3].bypass = (std::fmod (t, 0.2) < 0.1) ? 1.0f : 0.0f;              // bypass toutes les 100 ms
            if (std::abs (t - 0.5) < 1.0 / fs * 512) e.setOrder ({ 6, 5, 4, 3, 2, 1, 0, 7, 8, 9 });
            if (std::abs (t - 0.9) < 1.0 / fs * 512) e.setOrder ({ 3, 0, 1, 2, 4, 5, 6, 7, 8, 9 });
        };
        gAllocs = 0;
        gCountAllocs = true;
        runEngine (e, L, R, 0, n, 512, automation);
        gCountAllocs = false;
        const long allocs = gAllocs.load();
        check (allocs == 0, "Allocations sur le thread audio (7 modules, bypass, 2 réordonnancements)", (double) allocs);

        const double worst = maxStep (L, (std::size_t) (fs * 0.05), n);
        check (worst < 4.0 * natural, "Pente max pendant bypass / réordonnancements (× pente naturelle)", worst / natural, "x");

        // Échange de module à chaud (Clip -> Limit) pendant la lecture.
        auto L2 = sine ((std::size_t) (fs * 0.2), 440.0, fs, 0.5), R2 = L2;
        p.slots[3].bypass = 0.0f;
        runEngine (e, L2, R2, 0, L2.size(), 512, [&] (std::size_t off)
        {
            if (off == 512 * 40) p.setType (e, 3, ModuleType::limit);
        });
        const double swapStep = maxStep (L2, 512 * 30, L2.size());
        check (swapStep < 4.0 * natural, "Échange de module Clip -> Limit à chaud : pente max (× naturelle)", swapStep / natural, "x");
        e.collectGarbage();
        check (e.getModuleType (3) == ModuleType::limit, "Type de slot publié", (double) (int) e.getModuleType (3));
    }

    {
        // Ordre : empaquetage et validation.
        const SlotOrder o { 9, 3, 1, 0, 2, 4, 8, 7, 6, 5 };
        check (RackRouting::unpack (RackRouting::pack (o)) == o, "Ordre empaqueté sur 64 bits (aller-retour)", 0.0);
        check (! RackRouting::isValid ({ 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 }), "Ordre invalide (doublon) refusé", 0.0);
    }
}


static void testStress()
{
    std::puts ("\n== 10. Stress multi-thread (thread audio + message thread, à lancer aussi sous ThreadSanitizer)");
    const double fs = 384000.0;
    RackEngine e;
    TestParams p;
    p.bind (e);
    e.prepare (fs, 512);

    std::atomic<bool> running { true };
    std::atomic<long> blocks { 0 };
    std::atomic<bool> finite { true };
    std::thread audio ([&]
    {
        Buf L (512), R (512), key (512, 0.0);
        std::size_t t = 0;
        while (running.load())
        {
            for (int i = 0; i < 512; ++i, ++t)
            {
                L[(std::size_t) i] = 0.5 * std::sin (0.013 * (double) t);
                R[(std::size_t) i] = 0.5 * std::sin (0.017 * (double) t);
                key[(std::size_t) i] = (t % 20000) < 3000 ? 0.8 * std::sin (0.001 * (double) t) : 0.0;
            }
            e.process (L.data(), R.data(), 512, key.data(), key.data(), true);
            for (int i = 0; i < 512; ++i)
                if (! std::isfinite (L[(std::size_t) i]) || std::abs (L[(std::size_t) i]) > 100.0) finite = false;
            blocks.fetch_add (1);
        }
    });

    // Message thread : rafales de changements de module, d'ordre et de paramètres.
    std::mt19937 rng (11);
    int swaps = 0, reorders = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (1500);
    while (std::chrono::steady_clock::now() < until)
    {
        const int slot = (int) (rng() % kNumSlots);
        p.setType (e, slot, (ModuleType) (rng() % kNumModuleTypes));
        ++swaps;
        SlotOrder o = RackRouting::identity();
        std::shuffle (o.begin(), o.end(), rng);
        e.setOrder (o);
        ++reorders;
        for (int k = 0; k < kMaxModuleParams; ++k)
            p.slots[(std::size_t) slot].p[(std::size_t) k] = (float) (rng() % 1000) / 999.0f;
        p.slots[(std::size_t) (rng() % kNumSlots)].bypass = (float) (rng() % 2);
        e.collectGarbage();
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    running = false;
    audio.join();
    e.collectGarbage();

    check (finite.load(), "Sortie finie et bornée sous " + std::to_string (swaps) + " échanges / " + std::to_string (reorders) + " réordonnancements",
           (double) blocks.load(), "blocs");
    check (blocks.load() > 100, "Le thread audio n'a jamais été bloqué (blocs traités)", (double) blocks.load());
}

//==============================================================================
int main (int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : "";
    const std::pair<const char*, void (*)()> tests[] = {
        { "svf", testSvf }, { "eq", testEq }, { "comp", testComp }, { "duck", testDuck }, { "clip", testClip },
        { "limit", testLimit }, { "delay", testDelay }, { "verb", testVerb }, { "engine", testEngine },
        { "stress", testStress },
    };
    std::puts ("AuraRack — banc d'essai DSP (64 bits)");
    for (auto& [name, fn] : tests)
        if (filter.empty() || filter == name) fn();

    std::printf ("\n%d / %d vérifications réussies\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
