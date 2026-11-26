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

/** Fait tourner un compresseur échantillon par échantillon et renvoie la réduction (dB) relevée aux instants demandés. */
template <typename Comp>
static std::vector<double> reductionAt (Comp& c, double fs, const std::vector<double>& times, double tailSec,
                                        const std::function<double (double)>& program)
{
    std::vector<double> out;
    const auto total = (std::size_t) ((times.back() + tailSec) * fs) + 2;
    std::size_t next = 0;
    for (std::size_t i = 0; i < total; ++i)
    {
        const double t = (double) i / fs;
        double l = program (t), r = l;
        c.process (&l, &r, 1, {});
        if (next < times.size() && t >= times[next]) { out.push_back (c.getGainReductionDb()); ++next; }
    }
    return out;
}

static void testComp()
{
    std::puts ("\n== 3. AuraComp (Aura-Opto, Aura-Tube, L-Comp)");

    // --- Masques de paramètres par algorithme.
    {
        const auto& i = AuraComp::info();
        check (i.visibleMask (0) == ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3)), "Masque Aura-Opto : Algorithm, Key, Peak Reduction, Gain");
        check ((i.visibleMask (1) & (1u << AuraComp::threshold)) != 0 && (i.visibleMask (1) & (1u << AuraComp::crush)) == 0, "Masque Aura-Tube : Threshold/Ratio/Attack/Release/Makeup, pas Crush");
        check (i.visibleMask (2) == ((1u << 0) | (1u << 1) | (1u << 9)), "Masque L-Comp : un seul bouton (Crush)");
    }

    // --- Aura-Tube : réduction statique et gain de sortie.
    {
        const double fs = 96000.0;
        AuraComp c;
        c.prepare (fs, 256);
        setParam (c, AuraComp::algorithm, 1);
        setParam (c, AuraComp::threshold, -18.0);
        setParam (c, AuraComp::ratio, 4.0);
        setParam (c, AuraComp::attack, 10.0);
        setParam (c, AuraComp::release, 50.0);
        c.reset();
        auto L = sine ((std::size_t) (fs * 3.0), 1000.0, fs, 0.5), R = L;
        runModule (c, L, R);
        const double expected = c.gainReductionFor (dsp::gainToDb (0.5));
        double meanGr = 0.0;                                   // moyenne sur 10 périodes (l'ondulation résiduelle s'annule)
        for (int i = 0; i < 960; ++i)
        {
            double l = 0.5 * std::sin (dsp::kTwoPi * 1000.0 * (double) (L.size() + (std::size_t) i) / fs), r = l;
            c.process (&l, &r, 1, {});
            meanGr += c.getGainReductionDb() / 960.0;
        }
        check (std::abs (meanGr - expected) < 0.08, "Tube : réduction statique (sinus -6 dBFS, seuil -18, 4:1, genou 10 dB)", meanGr - expected, "dB d'écart");
        const std::size_t n = L.size(), len = (std::size_t) fs / 2;
        const double outDb = dsp::gainToDb (toneAmplitude (L, n - len, len, 1000.0, fs) / 0.5);
        check (std::abs (outDb + expected) < 0.35, "Tube : niveau de sortie = -réduction (écart = couleur de l'étage lampe)", outDb + expected, "dB");
        double mean = 0.0;
        for (std::size_t i = n - len; i < n; ++i) mean += L[i];
        check (std::abs (mean / (double) len) < 1e-4, "Tube : pas de continu en sortie (blocage 5 Hz)", mean / (double) len);
    }

    // --- Aura-Opto : Peak Reduction règle la profondeur, monotone, quasi nul à 0.
    {
        const double fs = 96000.0;
        double gr[3] {};
        const double pr[3] = { 0.0, 50.0, 80.0 };
        for (int k = 0; k < 3; ++k)
        {
            AuraComp c;
            c.prepare (fs, 256);
            setParam (c, AuraComp::algorithm, 0);
            setParam (c, AuraComp::peakReduction, pr[k]);
            c.reset();
            auto L = sine ((std::size_t) (fs * 12.0), 1000.0, fs, 0.316), R = L;   // -10 dBFS
            runModule (c, L, R);
            gr[k] = c.getGainReductionDb();
        }
        check (gr[0] < 1.0, "Opto : Peak Reduction 0 ≈ pas de compression (sinus -10 dBFS)", gr[0], "dB");
        check (gr[1] > gr[0] + 2.0 && gr[2] > gr[1] + 2.0, "Opto : réduction croissante avec Peak Reduction (0 / 50 / 80)", gr[2], "dB");
        check (gr[1] > 3.0 && gr[1] < 9.0, "Opto : Peak Reduction 50 -> quelques dB (leveling, courbe douce)", gr[1], "dB");
    }

    // --- Aura-Opto : relâchement dépendant du programme (court après un bref passage, long après un passage soutenu).
    {
        const double fs = 48000.0;
        auto halfTime = [&] (double burstSec)
        {
            AuraComp c;
            c.prepare (fs, 256);
            setParam (c, AuraComp::algorithm, 0);
            setParam (c, AuraComp::peakReduction, 70.0);
            c.reset();
            const auto nBurst = (std::size_t) (burstSec * fs);
            double peak = 0.0;
            for (std::size_t i = 0; i < nBurst; ++i)
            {
                double l = 0.5 * std::sin (dsp::kTwoPi * 1000.0 * (double) i / fs), r = l;
                c.process (&l, &r, 1, {});
                peak = c.getGainReductionDb();
            }
            for (std::size_t i = 0; i < (std::size_t) (30.0 * fs); ++i)
            {
                double l = 0.0, r = 0.0;
                c.process (&l, &r, 1, {});
                if (c.getGainReductionDb() < 0.5 * peak) return (double) i / fs;
            }
            return 30.0;
        };
        const double shortT = halfTime (0.15), longT = halfTime (12.0);
        check (shortT < 0.4, "Opto : relâchement à 50 % après un passage de 150 ms", shortT * 1000.0, "ms");
        check (longT > 3.0 * shortT, "Opto : relâchement bien plus lent après 12 s de compression (programme)", longT * 1000.0, "ms");
    }

    // --- Invariance de la dynamique en fréquence d'échantillonnage (réponse à un échelon, Tube et Opto).
    {
        const std::vector<double> times { 0.001, 0.003, 0.01, 0.03, 0.1, 0.3, 1.0 };
        for (int algo : { 0, 1 })
        {
            std::vector<std::vector<double>> res;
            const double rates[] = { 44100.0, 96000.0, 352800.0, 384000.0 };
            for (double fs : rates)
            {
                AuraComp c;
                c.prepare (fs, 256);
                setParam (c, AuraComp::algorithm, algo);
                setParam (c, AuraComp::peakReduction, 60.0);
                setParam (c, AuraComp::threshold, -24.0);
                setParam (c, AuraComp::ratio, 6.0);
                setParam (c, AuraComp::attack, 5.0);
                setParam (c, AuraComp::release, 300.0);
                c.reset();
                // Échelon d'amplitude sur un sinus de 1 kHz à t = 0,2 s (phase continue).
                std::vector<double> t2; for (double t : times) t2.push_back (0.2 + t);
                res.push_back (reductionAt (c, fs, t2, 0.01, [] (double t) { return (t < 0.2 ? 0.02 : 0.5) * std::sin (dsp::kTwoPi * 1000.0 * t); }));
            }
            double worst = 0.0;
            for (std::size_t r = 0; r < 3; ++r)
                for (std::size_t k = 0; k < times.size(); ++k)
                {
                    const double d = std::abs (res[r][k] - res[3][k]);
                    worst = std::max (worst, d);
                }
            check (worst < 0.2, std::string ("Invariance en fréquence d'échantillonnage (44,1 / 96 / 352,8 vs 384 kHz) : ") + (algo == 0 ? "Opto" : "Tube"), worst, "dB max");
        }
    }

    // --- Aucun repliement / modulation parasite à 384 kHz et à 44,1 kHz (sinus de 18 kHz comprimé).
    for (double fs : { 384000.0, 44100.0 })
    {
        for (int algo : { 0, 1, 2 })
        {
            AuraComp c;
            c.prepare (fs, 256);
            setParam (c, AuraComp::algorithm, algo);
            setParam (c, AuraComp::peakReduction, 70.0);
            setParam (c, AuraComp::threshold, -24.0);
            setParam (c, AuraComp::ratio, 8.0);
            setParam (c, AuraComp::crush, 80.0);
            c.reset();
            const std::size_t n = (std::size_t) 65536, warm = (std::size_t) (fs * 1.5);
            const std::size_t k0 = (std::size_t) std::lround ((fs > 100000.0 ? 18000.0 : 5000.0) * (double) n / fs);   // raie exacte
            const double f0 = (double) k0 * fs / (double) n;
            auto L = sine (warm + n, f0, fs, 0.5), R = L;
            runModule (c, L, R);
            const double r = aliasRatioDb (L, warm, n, k0);
            const char* names[] = { "Opto", "Tube", "L-Comp" };
            // Au-dessus de 176,4 kHz, le contenu non harmonique doit être sous -110 dB ; à 44,1 kHz la saturation
            // (Tube, L-Comp) peut produire des harmoniques repliées, mais restant sous -50 dB.
            const double limit = fs > 100000.0 ? -90.0 : (algo == 0 ? -90.0 : -50.0);
            check (r < limit, std::string ("Contenu non harmonique, ") + names[algo] + " " + std::to_string ((int) (fs / 1000.0)) + " kHz, sinus " + std::to_string ((int) f0) + " Hz", r, "dB");
        }
    }

    // --- L-Comp : un bouton, écrase vite, plafond 0 dBFS.
    {
        const double fs = 352800.0;
        AuraComp c;
        c.prepare (fs, 256);
        setParam (c, AuraComp::algorithm, 2);
        setParam (c, AuraComp::crush, 100.0);
        c.reset();
        auto level = [&] (double amp)
        {
            c.reset();
            auto L = sine ((std::size_t) (fs * 1.0), 1000.0, fs, amp), R = L;
            runModule (c, L, R);
            return std::make_pair (dsp::gainToDb (toneAmplitude (L, (std::size_t) (fs * 0.5), (std::size_t) (fs * 0.5), 1000.0, fs)), peakAbs (L, 0, L.size()));
        };
        const auto lo = level (0.0316), hi = level (0.5);      // -30 dBFS et -6 dBFS : 24 dB d'écart à l'entrée
        check (hi.first - lo.first < 12.0, "L-Comp 100 % : 24 dB d'écart en entrée -> moins de 12 dB en sortie", hi.first - lo.first, "dB");
        const auto hot = level (1.0);
        check (hot.second <= 1.0 + 1e-12, "L-Comp : crête de sortie <= 0 dBFS pour une entrée à 0 dBFS", dsp::gainToDb (hot.second), "dBFS");

        // Vitesse : échelon de +20 dB, 90 % de la réduction finale atteints en moins de 0,5 ms.
        c.reset();
        auto gr = reductionAt (c, fs, { 0.1002, 0.1005, 0.102, 0.3 }, 0.01, [] (double t) { return (t < 0.1 ? 0.03 : 0.3) * std::sin (dsp::kTwoPi * 1000.0 * t); });
        check (gr[1] > 0.9 * gr[3], "L-Comp : 90 % de la réduction atteints en 0,5 ms (échelon +20 dB)", gr[1] / gr[3] * 100.0, "%");
    }

    // --- Silence : les états retombent exactement à zéro (pas de dénormalisés).
    {
        const double fs = 48000.0;
        AuraComp c;
        c.prepare (fs, 256);
        setParam (c, AuraComp::algorithm, 0);
        setParam (c, AuraComp::peakReduction, 80.0);
        c.reset();
        auto L = sine ((std::size_t) (fs * 2.0), 1000.0, fs, 0.5), R = L;
        runModule (c, L, R);
        Buf z ((std::size_t) fs, 0.0), z2 = z;
        for (int s = 0; s < 150; ++s) runModule (c, z, z2);
        check (c.getGainReductionDb() == 0.0, "Opto : réduction exactement nulle après 150 s de silence", c.getGainReductionDb(), "dB");
    }
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
        std::atomic<float> bypass { 0.0f }, mix { 100.0f }, inTrim { 0.0f }, outTrim { 0.0f }, agc { 0.0f };
        std::array<std::atomic<float>, kMaxModuleParams> p {};
    };
    std::array<Slot, kNumSlots> slots;
    std::atomic<float> inGain { 0.0f }, outGain { 0.0f }, masterMix { 100.0f }, scSource { 0.0f };
    std::atomic<float> rackAgc { 0.0f }, width { 100.0f }, bassMono { 0.0f }, mono { 0.0f };

    void bind (RackEngine& e)
    {
        for (int s = 0; s < kNumSlots; ++s)
        {
            auto& sl = slots[(std::size_t) s];
            SlotParameterSources src { &sl.bypass, &sl.mix, &sl.inTrim, &sl.outTrim, &sl.agc, {} };
            for (int i = 0; i < kMaxModuleParams; ++i) src.params[(std::size_t) i] = &sl.p[(std::size_t) i];
            e.bindSlot (s, src);
        }
        e.bindGlobal ({ &inGain, &outGain, &masterMix, &scSource, &rackAgc, &width, &bassMono, &mono });
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
        // Transparence : EQ à 0 dB + Limit sous le plafond, Mix 100 % -> l'entrée retardée, au bit près.
        RackEngine e;
        TestParams p;
        p.bind (e);
        p.setType (e, 2, ModuleType::eq);
        p.setType (e, 5, ModuleType::limit);
        e.prepare (fs, 512);
        const int lat = e.getRequestedLatency();
        PortableGauss g (5, 0.1);
        Buf L (40000), R (40000);
        for (std::size_t i = 0; i < L.size(); ++i) { L[i] = g(); R[i] = g(); }
        const auto inL = L, inR = R;
        runEngine (e, L, R, 0, L.size(), 300);
        bool exact = true;
        for (std::size_t i = (std::size_t) lat; i < L.size(); ++i)
            exact = exact && L[i] == inL[i - (std::size_t) lat] && R[i] == inR[i - (std::size_t) lat];
        check (exact, "EQ 0 dB + Limit sous le plafond : sortie = entrée retardée, bit pour bit", lat, "éch.");
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
        // Valeur normalisée arrivée en float depuis l'hôte : 0 dB reste exactement 0 dB.
        const auto& spec = AuraEQ::info().params[AuraEQ::outGain];
        const double drifted = spec.toPlain ((double) (0.5f - 6.0e-8f));
        check (drifted == 0.0, "Paramètre « 0 dB » reçu à 1e-7 près : exactement 0 dB", drifted, "dB");
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
                if (! std::isfinite (L[(std::size_t) i]) || std::abs (L[(std::size_t) i]) > 1.0e290)   // 10 modules × jusqu'à +54 dB de gain chacun : 1e27 est légitime
                {
                    finite = false;
                    std::printf ("      échantillon hors borne : %g\n", L[(std::size_t) i]);
                }
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

static void testMoveSlot()
{
    std::puts("\n== 11. RackEngine::moveSlot (compare-and-swap)");
    check (RackRouting::moved (RackRouting::identity(), 0, 3) == SlotOrder { 1, 2, 3, 0, 4, 5, 6, 7, 8, 9 }, "moved(0 -> 3) : les slots intermédiaires glissent");
    check (RackRouting::moved (RackRouting::identity(), 7, 2) == SlotOrder { 0, 1, 7, 2, 3, 4, 5, 6, 8, 9 }, "moved(7 -> 2)");
    check (RackRouting::moved (RackRouting::identity(), 4, 4) == RackRouting::identity(), "moved(4 -> 4) : sans effet");

    RackEngine e;
    check (! e.moveSlot (5, 5) && ! e.moveSlot (-1, 3) && ! e.moveSlot (0, 10), "Indices invalides ou identiques : refusés");

    // 4 fils déplacent en rafale ; un cinquième lit l'ordre en un mot : toujours une permutation valide.
    std::atomic<bool> stop { false }, valid { true };
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back ([&, t]
        {
            unsigned x = 101u + (unsigned) t;
            while (! stop.load())
            {
                x = x * 1664525u + 1013904223u;
                e.moveSlot ((int) (x >> 8) % kNumSlots, (int) (x >> 16) % kNumSlots);
            }
        });
    std::thread reader ([&] { while (! stop.load()) if (! RackRouting::isValid (RackRouting::unpack (e.getOrderPacked()))) valid = false; });
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    stop = true;
    for (auto& th : threads) th.join();
    reader.join();
    check (valid.load() && RackRouting::isValid (e.getOrder()), "4 fils en rafale : l'ordre reste une permutation valide");
}

static void testW9b()
{
    std::puts ("\n== 12. W9BMK1 (compresseur opto à lampes, esprit CL 1B)");
    using W = AuraW9B;

    check (W::info().params.size() == 8, "8 paramètres : Input, Threshold, Ratio, Attack, Release, Output, Auto Focus, Focus");
    check (W::info().visibleMask (0) == 0xFFu, "Tous les paramètres sont visibles (aucun bouton mort)");

    // --- Réduction statique : Input pousse la clé ; theorie = courbe à genou 10 dB.
    {
        const double fs = 96000.0;
        W c;
        c.prepare (fs, 256);
        setParam (c, W::input, 6.0);
        setParam (c, W::threshold, -18.0);
        setParam (c, W::ratio, 4.0);
        setParam (c, W::attack, 10.0);
        setParam (c, W::release, 50.0);
        setParam (c, W::output, 0.0);
        c.reset();
        auto L = sine ((std::size_t) (fs * 3.0), 1000.0, fs, 0.25), R = L;     // -12 dBFS + 6 dB d'Input = -6 dB à la clé
        runModule (c, L, R);
        const double expected = c.gainReductionFor (dsp::gainToDb (0.5));
        double meanGr = 0.0;
        for (int i = 0; i < 960; ++i)
        {
            double l = 0.25 * std::sin (dsp::kTwoPi * 1000.0 * (double) (L.size() + (std::size_t) i) / fs), r = l;
            c.process (&l, &r, 1, {});
            meanGr += c.getGainReductionDb() / 960.0;
        }
        check (std::abs (meanGr - expected) < 0.08, "Réduction statique = théorie (Input +6 dB, seuil -18, 4:1)", meanGr - expected, "dB d'écart");
    }

    // --- Transparence au repos : seuil 0 dB, ratio 1:1, gains à 0 -> seule la couleur de l'étage lampe, sans continu.
    {
        const double fs = 384000.0;
        double gain1k = 0.0, gain20k = 0.0, mean = 0.0;
        for (double f : { 1000.0, 20000.0 })
        {
            W c;
            c.prepare (fs, 256);
            setParam (c, W::threshold, 0.0);
            setParam (c, W::ratio, 1.0);
            c.reset();
            const std::size_t n = 65536, warm = (std::size_t) fs;
            const std::size_t k = (std::size_t) std::lround (f * (double) n / fs);
            const double fe = (double) k * fs / (double) n;
            auto L = sine (warm + n, fe, fs, 0.1), R = L;
            runModule (c, L, R);
            const double g = dsp::gainToDb (toneAmplitude (L, warm, n, fe, fs) / 0.1);
            (f < 5000.0 ? gain1k : gain20k) = g;
            if (f < 5000.0) { for (std::size_t i = warm; i < warm + n; ++i) mean += L[i] / (double) n; }
        }
        check (std::abs (gain20k - gain1k) < 0.01, "DXD : l'étage lampe n'affaiblit pas les aigus (20 kHz vs 1 kHz)", gain20k - gain1k, "dB");
        check (std::abs (mean) < 1e-5, "Aucun continu en sortie", mean);
    }

    // --- Invariance en fréquence d'échantillonnage (échelon d'amplitude).
    {
        const std::vector<double> times { 0.003, 0.01, 0.03, 0.1, 0.3, 1.0 };
        std::vector<std::vector<double>> res;
        const double rates[] = { 44100.0, 96000.0, 352800.0, 384000.0 };
        for (double fs : rates)
        {
            W c;
            c.prepare (fs, 256);
            setParam (c, W::input, 6.0);
            setParam (c, W::threshold, -24.0);
            setParam (c, W::ratio, 6.0);
            setParam (c, W::attack, 5.0);
            setParam (c, W::release, 300.0);
            c.reset();
            std::vector<double> t2; for (double t : times) t2.push_back (0.2 + t);
            res.push_back (reductionAt (c, fs, t2, 0.01, [] (double t) { return (t < 0.2 ? 0.02 : 0.5) * std::sin (dsp::kTwoPi * 1000.0 * t); }));
        }
        double worst = 0.0;
        for (std::size_t r = 0; r < 3; ++r)
            for (std::size_t k = 0; k < times.size(); ++k)
                worst = std::max (worst, std::abs (res[r][k] - res[3][k]));
        check (worst < 0.2, "Invariance en fréquence d'échantillonnage (44,1 / 96 / 352,8 vs 384 kHz)", worst, "dB max");
    }

    // --- Contenu non harmonique (repliement / modulation) à 384 kHz et 44,1 kHz, sinus comprimé.
    for (double fs : { 384000.0, 44100.0 })
    {
        W c;
        c.prepare (fs, 256);
        setParam (c, W::input, 6.0);
        setParam (c, W::threshold, -24.0);
        setParam (c, W::ratio, 8.0);
        c.reset();
        const std::size_t n = 65536, warm = (std::size_t) (fs * 1.5);
        const std::size_t k0 = (std::size_t) std::lround ((fs > 100000.0 ? 18000.0 : 5000.0) * (double) n / fs);
        const double f0 = (double) k0 * fs / (double) n;
        auto L = sine (warm + n, f0, fs, 0.5), R = L;
        runModule (c, L, R);
        const double r = aliasRatioDb (L, warm, n, k0);
        check (r < (fs > 100000.0 ? -90.0 : -50.0), std::string ("Contenu non harmonique à ") + std::to_string ((int) (fs / 1000.0)) + " kHz", r, "dB");
    }

    // --- Voyant : s'allume quand la sortie approche 0 dBFS, reste éteint sinon ; se remet à zéro à la lecture.
    {
        const double fs = 96000.0;
        W c;
        c.prepare (fs, 256);
        setParam (c, W::threshold, 0.0);
        setParam (c, W::ratio, 1.0);
        c.reset();
        auto L = sine (4800, 1000.0, fs, 0.1), R = L;
        runModule (c, L, R);
        check (c.takeStatusPeak() < 0.2 && c.takeStatusPeak() == 0.0, "Voyant : crête faible, puis remise à zéro à la lecture");
        setParam (c, W::output, 24.0);
        c.reset();
        auto L2 = sine (4800, 1000.0, fs, 0.5), R2 = L2;
        runModule (c, L2, R2);
        check (c.takeStatusPeak() > 1.0, "Voyant : sortie au-delà de 0 dBFS (Output +24 dB)");
    }
}


//==============================================================================
// Signal « parole » déterministe : syllabes de 180 ms (enveloppe en cosinus surélevé, niveaux variant sur 10 dB),
// petits creux de 60 ms, phrases de 10 syllabes séparées de 0,9 s de quasi-silence. Porteuse à 3 harmoniques.
struct SpeechSource
{
    SpeechSource()
    {
        std::uint32_t seed = 12345u;
        for (auto& a : amps)
        {
            seed = seed * 1664525u + 1013904223u;
            a = std::pow (10.0, -10.0 * ((double) (seed >> 8) / 16777216.0) / 20.0);
        }
    }
    static bool inSyllable (double t) { const double p = std::fmod (t, 3.3); return p < 2.4 && std::fmod (p, 0.24) < 0.18; }
    double operator() (double t) const
    {
        const double period = 3.3, p = std::fmod (t, period);
        const double carrier = (std::sin (dsp::kTwoPi * 180.0 * t) + 0.5 * std::sin (dsp::kTwoPi * 360.0 * t)
                                + 0.3 * std::sin (dsp::kTwoPi * 540.0 * t)) / 1.6;
        if (p >= 2.4) return 1.0e-4 * carrier;                              // pause entre phrases (-80 dB)
        const auto k = (std::size_t) (std::floor (t / period) * 10.0 + std::floor (p / 0.24));
        const double q = std::fmod (p, 0.24);
        const double env = q < 0.18 ? 0.5 - 0.5 * std::cos (dsp::kTwoPi * q / 0.18) : 0.03;
        return amps[k % amps.size()] * env * carrier;
    }
    std::array<double, 64> amps {};
};

struct FocusRun { std::vector<double> time, threshold, reduction, active; };   // active = 1 pendant une syllabe

/** Fait tourner le W9BMK1 sur la parole. `levelDb(t)` : niveau (dBFS) de la parole ; `extraDb(t)` : gain supplémentaire par échantillon. */
static FocusRun runFocus (AuraModuleDSP& c, double fs, double seconds, const std::function<double (double)>& levelDb,
                          const std::function<double (double)>& extraDb = {}, double blockSeconds = 0.002)
{
    static const SpeechSource speech;
    FocusRun r;
    const int block = std::max (1, (int) std::lround (blockSeconds * fs));
    const auto total = (std::size_t) (seconds * fs);
    Buf L ((std::size_t) block), R ((std::size_t) block);
    for (std::size_t off = 0; off < total; off += (std::size_t) block)
    {
        const int n = (int) std::min<std::size_t> ((std::size_t) block, total - off);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (off + (std::size_t) i) / fs;
            double g = std::pow (10.0, levelDb (t) / 20.0);
            if (extraDb) g *= std::pow (10.0, extraDb (t) / 20.0);
            L[(std::size_t) i] = R[(std::size_t) i] = g * speech (t);
        }
        c.process (L.data(), R.data(), n, {});
        r.time.push_back ((double) (off + (std::size_t) n) / fs);
        r.threshold.push_back (c.getMeterAux());
        r.reduction.push_back (c.getGainReductionDb());
        r.active.push_back (SpeechSource::inSyllable (r.time.back()) ? 1.0 : 0.0);
    }
    return r;
}

static double meanBetween (const std::vector<double>& t, const std::vector<double>& v, double t0, double t1)
{
    double s = 0.0; int n = 0;
    for (std::size_t i = 0; i < t.size(); ++i) if (t[i] >= t0 && t[i] < t1) { s += v[i]; ++n; }
    return n > 0 ? s / n : 0.0;
}
/** Moyenne pendant les syllabes seulement (ce que le suiveur appelle « réduction moyenne » : pauses et creux exclus). */
static double meanActive (const FocusRun& r, double t0, double t1)
{
    double s = 0.0; int n = 0;
    for (std::size_t i = 0; i < r.time.size(); ++i) if (r.time[i] >= t0 && r.time[i] < t1 && r.active[i] > 0.5) { s += r.reduction[i]; ++n; }
    return n > 0 ? s / n : 0.0;
}
static double spanBetween (const std::vector<double>& t, const std::vector<double>& v, double t0, double t1)
{
    double lo = 1e9, hi = -1e9;
    for (std::size_t i = 0; i < t.size(); ++i) if (t[i] >= t0 && t[i] < t1) { lo = std::min (lo, v[i]); hi = std::max (hi, v[i]); }
    return hi - lo;
}
/** Plus grande excursion (crête à crête) du seuil dans une fenêtre glissante de `win` secondes. */
static double worstWindowSpan (const FocusRun& r, double t0, double t1, double win)
{
    double worst = 0.0;
    for (double a = t0; a + win <= t1; a += win * 0.25) worst = std::max (worst, spanBetween (r.time, r.threshold, a, a + win));
    return worst;
}
static double maxSlew (const FocusRun& r)
{
    double w = 0.0;
    for (std::size_t i = 1; i < r.time.size(); ++i) w = std::max (w, std::abs (r.threshold[i] - r.threshold[i - 1]) / (r.time[i] - r.time[i - 1]));
    return w;
}

static void prepareFocus (AuraW9B& c, double fs, double focusDb, bool on = true, double ratio = 4.0)
{
    c.prepare (fs, 256);
    setParam (c, AuraW9B::ratio, ratio);
    setParam (c, AuraW9B::attack, 10.0);
    setParam (c, AuraW9B::release, 200.0);
    setParam (c, AuraW9B::threshold, -18.0);
    setParam (c, AuraW9B::focus, focusDb);
    setParam (c, AuraW9B::autoFocus, on ? 1.0 : 0.0);
    c.reset();
}

static void testFocus()
{
    std::puts ("\n== 13. W9BMK1 · AUTO FOCUS (suivi lent du Threshold vers une réduction visée)");
    using W = AuraW9B;
    const double fs = 48000.0;

    // --- Inversion analytique de la courbe statique : exacte (genou compris).
    {
        double worst = 0.0;
        for (double ratio : { 1.5, 2.0, 4.0, 10.0 })
            for (double g : { 0.5, 1.0, 2.5, 3.0, 6.0, 12.0 })
            {
                const double thr = compdetail::FocusTracker::thresholdFor (-20.0, g, ratio, 10.0);
                worst = std::max (worst, std::abs (compdetail::kneeReduction (-20.0, thr, ratio, 10.0) - g));
            }
        check (worst < 1e-9, "Inversion de la courbe (ratios 1,5 à 10, cibles 0,5 à 12 dB, genou 10 dB)", worst, "dB d'écart");
    }

    // --- La réduction moyenne atteint la cible, quel que soit le niveau d'entrée.
    for (double target : { 3.0, 6.0, 10.0 })
        for (double level : { -24.0, -12.0 })
        {
            W c;
            prepareFocus (c, fs, target);
            auto r = runFocus (c, fs, 45.0, [level] (double) { return level; });
            const double mean = meanActive (r, 35.0, 45.0);
            char what[128];
            std::snprintf (what, sizeof (what), "Cible %.0f dB, parole à %.0f dBFS : réduction moyenne sur les syllabes (35-45 s)", target, level);
            check (std::abs (mean - target) < 1.0, what, mean, "dB");
        }

    // --- Anti-pompage : seuil quasi immobile en régime établi, vitesse limitée.
    {
        W c;
        prepareFocus (c, fs, 6.0);
        auto r = runFocus (c, fs, 60.0, [] (double) { return -20.0; });
        check (worstWindowSpan (r, 30.0, 60.0, 0.5) < 0.3, "Régime établi : excursion du seuil sur 0,5 s (échelle syllabe) < 0,3 dB", worstWindowSpan (r, 30.0, 60.0, 0.5), "dB");
        check (worstWindowSpan (r, 30.0, 60.0, 3.3) < 1.5, "Régime établi : excursion du seuil sur une phrase (3,3 s) < 1,5 dB", worstWindowSpan (r, 30.0, 60.0, 3.3), "dB");
        check (maxSlew (r) <= 6.0 + 0.05, "Vitesse maximale du seuil ≤ 6 dB/s", maxSlew (r), "dB/s");
    }

    // --- Changement de niveau de +10 dB : suivi en quelques secondes, sans dépassement excessif.
    {
        W c;
        prepareFocus (c, fs, 6.0);
        auto r = runFocus (c, fs, 70.0, [] (double t) { return t < 35.0 ? -24.0 : -14.0; });
        const double before = meanBetween (r.time, r.threshold, 30.0, 35.0), after = meanBetween (r.time, r.threshold, 65.0, 70.0);
        check (std::abs ((after - before) - 10.0) < 1.5, "Parole +10 dB : le seuil monte d'environ 10 dB", after - before, "dB");
        check (std::abs (meanActive (r, 60.0, 70.0) - 6.0) < 1.0, "… et la réduction revient à la cible", meanActive (r, 60.0, 70.0), "dB");
        double worst = 0.0;   // dépassement : pire réduction moyennée sur 1 s juste après l'échelon
        for (double t0 = 35.0; t0 < 50.0; t0 += 0.5) worst = std::max (worst, meanBetween (r.time, r.reduction, t0, t0 + 1.0));
        check (worst < 6.0 + 7.0, "Échelon +10 dB : la réduction (moyenne sur 1 s) reste sous la cible + 7 dB", worst, "dB");
    }

    // --- Transitoire isolé et pauses : comparaison à la même parole SANS l'événement (le seuil dérive très lentement
    //     de lui-même ; seul l'écart dû à l'événement compte).
    {
        auto steady = [&] (const std::function<double (double)>& level, const std::function<double (double)>& extra)
        {
            W c;
            prepareFocus (c, fs, 6.0);
            return runFocus (c, fs, 45.0, level, extra);
        };
        const auto level0 = [] (double) { return -20.0; };
        const auto ref = steady (level0, {});
        auto delta = [&] (const FocusRun& r, double t0, double t1) { return meanBetween (r.time, r.threshold, t0, t1) - meanBetween (ref.time, ref.threshold, t0, t1); };

        const auto click = steady (level0, [] (double t) { return (t >= 30.0 && t < 30.005) ? 20.0 : 0.0; });
        check (std::abs (delta (click, 33.0, 36.0)) < 0.4, "Clic de 5 ms à +20 dB : le seuil bouge de moins de 0,4 dB", delta (click, 33.0, 36.0), "dB");

        const auto soft = steady ([] (double t) { return (t >= 30.0 && t < 32.0) ? -65.0 : -20.0; }, {});
        check (std::abs (delta (soft, 32.0, 33.0)) < 0.5, "Pause de 2 s à -45 dB sous la parole : le seuil ne réagit pas (1 s après la reprise)", delta (soft, 32.0, 33.0), "dB");

        const auto mute = steady ([] (double t) { return (t >= 30.0 && t < 35.0) ? -100.0 : -20.0; }, {});
        double frozen = 0.0;   // pendant le silence, le seuil est exactement celui d'avant
        for (std::size_t i = 0; i < mute.time.size(); ++i)
            if (mute.time[i] > 30.1 && mute.time[i] < 35.0) frozen = std::max (frozen, std::abs (mute.threshold[i] - meanBetween (mute.time, mute.threshold, 29.99, 30.01)));
        check (frozen < 0.05, "Silence de 5 s (sous la porte à -60 dB) : seuil gelé", frozen, "dB");
        check (std::abs (delta (mute, 35.0, 36.0)) < 0.5, "… et la reprise ne le déplace pas (1 s après)", delta (mute, 35.0, 36.0), "dB");
    }

    // --- Activation / désactivation sans saut, et le manuel est intact.
    {
        W c;
        prepareFocus (c, fs, 6.0, false);
        auto before = runFocus (c, fs, 2.0, [] (double) { return -20.0; });
        check (std::abs (before.threshold.back() - (-18.0)) < 1e-9, "Mode manuel : le seuil publié est le réglage Threshold", before.threshold.back(), "dB");
        setParam (c, W::autoFocus, 1.0);
        auto on = runFocus (c, fs, 0.02, [] (double) { return -20.0; });
        check (std::abs (on.threshold.front() - (-18.0)) < 0.2, "Activation : le seuil part du réglage manuel (pas de saut)", on.threshold.front() + 18.0, "dB");
        runFocus (c, fs, 20.0, [] (double) { return -20.0; });
        setParam (c, W::autoFocus, 0.0);
        auto off = runFocus (c, fs, 0.5, [] (double) { return -20.0; });
        check (std::abs (off.threshold.back() - (-18.0)) < 1e-9, "Désactivation : retour au Threshold manuel", off.threshold.back(), "dB");

        // En mode manuel, Focus n'a aucune influence : sortie bit-identique.
        W a, b;
        prepareFocus (a, fs, 2.0, false);
        prepareFocus (b, fs, 11.0, false);
        Buf la = sine (48000, 700.0, fs, 0.3), ra = la, lb = la, rb = la;
        runModule (a, la, ra);
        runModule (b, lb, rb);
        check (la == lb && ra == rb, "Mode manuel : sortie bit-identique quelle que soit la valeur de Focus");
    }

    // --- Même comportement à 44,1 et 384 kHz.
    {
        std::vector<double> thr, red;
        for (double rate : { 44100.0, 384000.0 })
        {
            W c;
            prepareFocus (c, rate, 6.0);
            auto r = runFocus (c, rate, 30.0, [] (double) { return -20.0; }, {}, 0.004);
            thr.push_back (meanBetween (r.time, r.threshold, 22.0, 30.0));
            red.push_back (meanBetween (r.time, r.reduction, 22.0, 30.0));
        }
        check (std::abs (thr[0] - thr[1]) < 0.5, "Seuil établi : 44,1 kHz vs 384 kHz", thr[0] - thr[1], "dB d'écart");
        check (std::abs (red[0] - red[1]) < 0.5, "Réduction moyenne : 44,1 kHz vs 384 kHz", red[0] - red[1], "dB d'écart");
    }
}

//==============================================================================
static void testL9a()
{
    std::puts ("\n== 14. L9A (amplificateur de nivellement opto, esprit LA-2A)");
    using L = AuraL9A;

    check (L::info().params.size() == 4, "4 paramètres : Peak Reduction, Gain, Mode, Meter");
    check (L::info().visibleMask (0) == 0xFu, "Tous les paramètres sont visibles (aucun bouton mort)");

    // --- Réduction statique = courbe théorique, et Limit plus franc que Comp.
    {
        const double fs = 96000.0;
        double grComp[2] {}, grLimit[2] {}, theory[2] {};
        int k = 0;
        for (double amp : { 0.063, 0.25 })   // -24 / -12 dBFS
        {
            for (int mode = 0; mode < 2; ++mode)
            {
                L c;
                c.prepare (fs, 256);
                setParam (c, L::peakReduction, 60.0);
                setParam (c, L::mode, (double) mode);
                c.reset();
                Buf x = sine ((std::size_t) (fs * 3.0), 1000.0, fs, amp), r = x;
                runModule (c, x, r);
                double m = 0.0;
                for (int i = 0; i < 960; ++i)
                {
                    double l = amp * std::sin (dsp::kTwoPi * 1000.0 * (double) (x.size() + (std::size_t) i) / fs), rr = l;
                    c.process (&l, &rr, 1, {});
                    m += c.getGainReductionDb() / 960.0;
                }
                (mode == 0 ? grComp : grLimit)[k] = m;
                if (mode == 0) theory[k] = c.gainReductionFor (dsp::gainToDb (amp));
            }
            ++k;
        }
        check (std::abs (grComp[1] - theory[1]) < 0.15, "Comp : réduction statique = théorie (sinus -12 dBFS, Peak Reduction 60 %)", grComp[1] - theory[1], "dB d'écart");
        check ((grLimit[1] - grLimit[0]) > 1.2 * (grComp[1] - grComp[0]),
               "Limit : la réduction croît plus vite avec le niveau que Comp (+12 dB d'entrée)", (grLimit[1] - grLimit[0]) - (grComp[1] - grComp[0]), "dB de plus");
    }

    // --- Dépendance au programme : relâchement court après un passage bref, long après une compression soutenue.
    {
        const double fs = 48000.0;
        auto halfTime = [&] (double burstSeconds)
        {
            L c;
            c.prepare (fs, 256);
            setParam (c, L::peakReduction, 70.0);
            c.reset();
            const std::size_t nBurst = (std::size_t) (burstSeconds * fs), nTail = (std::size_t) (8.0 * fs);
            Buf x (nBurst + nTail);
            for (std::size_t i = 0; i < x.size(); ++i)
                x[i] = (i < nBurst ? 0.5 : 0.002) * std::sin (dsp::kTwoPi * 1000.0 * (double) i / fs);
            Buf r = x;
            double peak = 0.0;
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                c.process (&x[i], &r[i], 1, {});
                if (i + 1 == nBurst) peak = c.getGainReductionDb();
            }
            // On repart pour mesurer : relâchement après la fin du passage.
            L d;
            d.prepare (fs, 256);
            setParam (d, L::peakReduction, 70.0);
            d.reset();
            Buf y (nBurst + nTail);
            for (std::size_t i = 0; i < y.size(); ++i)
                y[i] = (i < nBurst ? 0.5 : 0.002) * std::sin (dsp::kTwoPi * 1000.0 * (double) i / fs);
            Buf yr = y;
            double tHalf = -1.0;
            for (std::size_t i = 0; i < y.size(); ++i)
            {
                d.process (&y[i], &yr[i], 1, {});
                if (i >= nBurst && tHalf < 0.0 && d.getGainReductionDb() < 0.5 * peak) tHalf = (double) (i - nBurst) / fs;
            }
            return tHalf;
        };
        const double shortT = halfTime (0.15), longT = halfTime (12.0);
        check (shortT > 0.0 && shortT < 0.15, "Relâchement à 50 % après un passage de 150 ms", shortT, "s");
        check (longT > 0.4 && longT > 3.0 * shortT, "Relâchement à 50 % après 12 s de compression : bien plus long", longT, "s");
    }

    // --- Invariance en fréquence d'échantillonnage (échelon d'amplitude).
    {
        const std::vector<double> times { 0.003, 0.01, 0.03, 0.1, 0.3, 1.0 };
        std::vector<std::vector<double>> res;
        for (double fs : { 44100.0, 96000.0, 352800.0, 384000.0 })
        {
            L c;
            c.prepare (fs, 256);
            setParam (c, L::peakReduction, 60.0);
            c.reset();
            std::vector<double> t2; for (double t : times) t2.push_back (0.2 + t);
            res.push_back (reductionAt (c, fs, t2, 0.01, [] (double t) { return (t < 0.2 ? 0.02 : 0.5) * std::sin (dsp::kTwoPi * 1000.0 * t); }));
        }
        double worst = 0.0;
        for (std::size_t r = 0; r < 3; ++r)
            for (std::size_t k = 0; k < times.size(); ++k)
                worst = std::max (worst, std::abs (res[r][k] - res[3][k]));
        check (worst < 0.2, "Invariance en fréquence d'échantillonnage (44,1 / 96 / 352,8 vs 384 kHz)", worst, "dB max");
    }

    // --- DXD : réponse plate (20 kHz vs 1 kHz) et pas de contenu non harmonique.
    {
        const double fs = 384000.0;
        double g[2] {};
        int idx = 0;
        for (double f : { 1000.0, 20000.0 })
        {
            L c;
            c.prepare (fs, 256);
            setParam (c, L::peakReduction, 0.0);
            c.reset();
            const std::size_t n = 65536, warm = (std::size_t) fs;
            const std::size_t k = (std::size_t) std::lround (f * (double) n / fs);
            const double fe = (double) k * fs / (double) n;
            Buf x = sine (warm + n, fe, fs, 0.05), r = x;
            runModule (c, x, r);
            g[idx++] = dsp::gainToDb (toneAmplitude (x, warm, n, fe, fs) / 0.05);
        }
        check (std::abs (g[1] - g[0]) < 0.02, "DXD : 20 kHz vs 1 kHz sans compression", g[1] - g[0], "dB");

        L c;
        c.prepare (fs, 256);
        setParam (c, L::peakReduction, 80.0);
        c.reset();
        const std::size_t n = 65536, warm = (std::size_t) (fs * 1.5);
        const std::size_t k0 = (std::size_t) std::lround (18000.0 * (double) n / fs);
        const double f0 = (double) k0 * fs / (double) n;
        Buf x = sine (warm + n, f0, fs, 0.5), r = x;
        runModule (c, x, r);
        const double ratio = aliasRatioDb (x, warm, n, k0);
        check (ratio < -90.0, "Contenu non harmonique, sinus 18 kHz comprimé à 384 kHz", ratio, "dB");
    }

    // --- Mètre VU : niveau RMS en dBFS, 99 % en ≈ 300 ms.
    {
        const double fs = 48000.0;
        L c;
        c.prepare (fs, 256);
        setParam (c, L::peakReduction, 0.0);
        c.reset();
        const double amp = 0.1, expected = 20.0 * std::log10 (amp / std::sqrt (2.0));
        Buf x = sine ((std::size_t) (fs * 1.5), 1000.0, fs, amp), r = x;
        double at50 = 0.0, at300 = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            c.process (&x[i], &r[i], 1, {});
            if (i + 1 == (std::size_t) (0.05 * fs)) at50 = c.getMeterAux();
            if (i + 1 == (std::size_t) (0.30 * fs)) at300 = c.getMeterAux();
        }
        check (std::abs (c.getMeterAux() - expected) < 0.3, "VU : niveau de sortie RMS en dBFS (sinus 0,1 -> -23 dBFS)", c.getMeterAux() - expected, "dB d'écart");
        const double settled = c.getMeterAux();
        check (at300 > settled - 0.1 && at50 < settled - 3.0, "VU : montée à 99 % en ≈ 300 ms (et lente à 50 ms)", at300 - settled, "dB à 300 ms");
        Buf z (4800, 0.0), zr = z;
        runModule (c, z, zr);
        for (int i = 0; i < 9; ++i) { Buf zz (4800, 0.0), zzr = zz; runModule (c, zz, zzr); }
        check (c.getMeterAux() < -60.0, "VU : retombe dans le silence", c.getMeterAux(), "dBFS");
    }

    // --- Comp <-> Limit en cours de signal : la réduction de gain reste continue (pas de clic).
    {
        const double fs = 48000.0;
        L c;
        c.prepare (fs, 256);
        setParam (c, L::peakReduction, 60.0);
        c.reset();
        double prev = 0.0, worst = 0.0;
        for (std::size_t i = 0; i < (std::size_t) (fs * 4.0); ++i)
        {
            if (i == (std::size_t) (fs * 2.0)) setParam (c, L::mode, 1.0);
            double l = 0.4 * std::sin (dsp::kTwoPi * 1000.0 * (double) i / fs), r = l;
            c.process (&l, &r, 1, {});
            if (i > 1000) worst = std::max (worst, std::abs (c.getGainReductionDb() - prev));
            prev = c.getGainReductionDb();
        }
        check (worst < 0.01, "Comp -> Limit pendant le signal : réduction continue", worst, "dB/échantillon");
    }
}


//==============================================================================
static void testRackModules()
{
    std::puts ("\n== 15. W9BMK1 et L9A dans le rack (types persistés, indicateurs publiés par le moteur)");
    check ((int) ModuleType::w9b == 8 && (int) ModuleType::l9a == 9 && (int) ModuleType::e9ds == 10 && (int) ModuleType::v9c3 == 11 && (int) ModuleType::v9p2 == 12 && kNumModuleTypes == 17 && (int) ModuleType::f976 == 13 && (int) ModuleType::d9x == 14 && (int) ModuleType::sculpt == 15 && (int) ModuleType::tone == 16,
           "Identifiants de type figés (w9b = 8, l9a = 9, e9ds = 10, v9c3 = 11, v9p2 = 12) : les sessions existantes restent valables");

    const double fs = 96000.0;
    RackEngine e;
    TestParams p;
    p.bind (e);
    e.prepare (fs, 512);
    p.setType (e, 0, ModuleType::w9b);
    p.setType (e, 1, ModuleType::l9a);
    p.setPlain (0, ModuleType::w9b, AuraW9B::autoFocus, 1.0);
    p.setPlain (0, ModuleType::w9b, AuraW9B::focus, 6.0);
    p.setPlain (1, ModuleType::l9a, AuraL9A::peakReduction, 70.0);
    p.setPlain (1, ModuleType::l9a, AuraL9A::meter, 1.0);
    e.prepare (fs, 512);

    // 12 s d'un sinus -12 dBFS : le W9BMK1 en Auto déplace son seuil, le L9A comprime et publie son niveau VU.
    for (int chunk = 0; chunk < 12; ++chunk)
    {
        auto L = sine ((std::size_t) fs, 440.0, fs, 0.25), R = L;
        runEngine (e, L, R, 0, L.size(), 512);
    }
    const auto& m = e.getMeters();
    const double thr = m.slotAux[0].load(), vuDbfs = m.slotAux[1].load();
    check (thr < -18.5 && thr > -60.0, "Moteur : seuil effectif de l'Auto Focus publié (W9BMK1, slot 1)", thr, "dB");
    check (m.gainReductionDb[0].load() > 1.0, "Moteur : réduction de gain du W9BMK1 publiée", m.gainReductionDb[0].load(), "dB");
    check (m.gainReductionDb[1].load() > 1.0, "Moteur : réduction de gain du L9A publiée", m.gainReductionDb[1].load(), "dB");
    check (vuDbfs < -10.0 && vuDbfs > -40.0, "Moteur : niveau VU du L9A publié (dBFS RMS)", vuDbfs, "dBFS");
    check (m.slotStatus[0].load() > 0.01, "Moteur : crête de sortie du W9BMK1 publiée (voyant)", m.slotStatus[0].load());

    // Retour au mode manuel : le seuil publié est le réglage manuel (-18 dB par défaut).
    p.setPlain (0, ModuleType::w9b, AuraW9B::autoFocus, 0.0);
    auto L = sine (4096, 440.0, fs, 0.25), R = L;
    runEngine (e, L, R, 0, L.size(), 512);
    check (std::abs (m.slotAux[0].load() - (-18.0)) < 0.01, "Moteur : Auto Focus désactivé -> seuil manuel publié", m.slotAux[0].load(), "dB");
}

//==============================================================================

//==============================================================================
/** Amplitude de la composante à f dans x[start, start+n) (corrélation sur un nombre entier de périodes ou plus). */
static double amplitudeAt (const Buf& x, std::size_t start, std::size_t n, double f, double fs)
{
    double c = 0.0, s = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double ph = dsp::kTwoPi * f * (double) (start + i) / fs;
        c += x[start + i] * std::cos (ph);
        s += x[start + i] * std::sin (ph);
    }
    return 2.0 * std::sqrt (c * c + s * s) / (double) n;
}

static void testE9ds()
{
    std::puts ("\n== 16. E9DS (de-esser DXD)");
    using E = AuraE9DS;
    check (E::info().params.size() == 8 && E::info().visibleMask (0) == 0xFFu, "8 paramètres, tous visibles (aucun bouton mort)");

    auto settled = [] (double fs, double f, double amp, int modeParam, double* gr)
    {
        E c;
        c.prepare (fs, 256);
        setParam (c, E::mode, (double) modeParam);
        c.reset();
        Buf x = sine ((std::size_t) (fs * 1.0), f, fs, amp), r = x;
        runModule (c, x, r);
        if (gr != nullptr) *gr = c.getGainReductionDb();
        return x;
    };

    // --- Courbe statique : sinus à la fréquence centrale, GR mesurée = courbe théorique, jamais au-delà de Range.
    for (double fs : { 48000.0, 384000.0 })
    {
        E ref; ref.prepare (fs, 256);
        double gr = 0.0;
        settled (fs, 6500.0, 0.25, 0, &gr);   // -12 dBFS, seuil -30 dB
        const double theory = ref.staticReductionFor (-12.0);
        check (std::abs (gr - theory) < 0.25, fs > 100000.0 ? "Réduction statique = théorie (384 kHz)" : "Réduction statique = théorie (48 kHz)", gr - theory, "dB d'écart");
    }
    {
        double gr = 0.0;
        settled (96000.0, 6500.0, 1.0, 0, &gr);   // 0 dBFS : saturation à Range (8 dB)
        check (gr <= 8.0 + 1.0e-6 && gr > 7.0, "Range : la réduction ne dépasse jamais 8 dB (0 dBFS en bande)", gr, "dB");
    }
    {
        double a = 0.0, b = 0.0;
        settled (44100.0, 6500.0, 0.25, 0, &a);
        settled (384000.0, 6500.0, 0.25, 0, &b);
        check (std::abs (a - b) < 0.2, "Même réduction à 44,1 kHz et 384 kHz", std::abs (a - b), "dB");
    }

    // --- Sous le seuil ou hors bande : transparent. Split n'atteint pas le reste du spectre.
    {
        const double fs = 96000.0;
        E c; c.prepare (fs, 256);
        PortableGauss g (7, 0.003);   // bruit à ≈ -50 dBFS : sous le seuil
        Buf x (96000), r;
        for (auto& v : x) v = g();
        Buf y = x; r = x;
        runModule (c, y, r);
        double maxDiff = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i) maxDiff = std::max (maxDiff, std::abs (y[i] - x[i]));
        check (maxDiff == 0.0, "Sous le seuil : sortie identique à l'entrée (bit-transparent)", maxDiff);

        // 6,5 kHz (-12 dB) + 1 kHz (-12 dB) : Split réduit la sibilance, laisse le 1 kHz, Wide réduit tout.
        Buf mixIn (96000);
        for (std::size_t i = 0; i < mixIn.size(); ++i)
            mixIn[i] = 0.25 * std::sin (dsp::kTwoPi * 6500.0 * (double) i / fs) + 0.25 * std::sin (dsp::kTwoPi * 1000.0 * (double) i / fs);
        for (int modeParam = 0; modeParam < 2; ++modeParam)
        {
            E d; d.prepare (fs, 256);
            setParam (d, E::mode, (double) modeParam);
            d.reset();
            Buf yy = mixIn, rr = mixIn;
            runModule (d, yy, rr);
            const double sib = 20.0 * std::log10 (amplitudeAt (yy, 48000, 48000, 6500.0, fs) / 0.25);
            const double low = 20.0 * std::log10 (amplitudeAt (yy, 48000, 48000, 1000.0, fs) / 0.25);
            if (modeParam == 0)
            {
                check (sib < -3.0, "Split : la sibilance (6,5 kHz) est réduite", sib, "dB");
                check (std::abs (low) < 1.0, "Split : le médium (1 kHz) reste intact (pas de zozotement)", low, "dB");
            }
            else
                check (low < -2.0, "Wide : tout le signal est réduit (1 kHz inclus)", low, "dB");
        }
    }

    // --- Dynamique : attaque rapide, relâchement au réglage, pas de pompage sur un sinus continu.
    {
        const double fs = 96000.0;
        E c; c.prepare (fs, 256);
        setParam (c, E::release, 60.0);
        c.reset();
        const std::size_t nBurst = (std::size_t) (0.3 * fs), nTail = (std::size_t) (0.6 * fs);
        Buf x (nBurst + nTail);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = (i < nBurst ? 0.5 : 0.0005) * std::sin (dsp::kTwoPi * 6500.0 * (double) i / fs);
        Buf r = x;
        double peak = 0.0, at5ms = 0.0, tailAt = -1.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            c.process (&x[i], &r[i], 1, {});
            const double gr = c.getGainReductionDb();
            if (i < nBurst) peak = std::max (peak, gr);
            if (i == (std::size_t) (0.005 * fs)) at5ms = gr;
            if (i >= nBurst && tailAt < 0.0 && gr < 0.1 * peak) tailAt = (double) (i - nBurst) / fs;
        }
        check (at5ms > 0.85 * peak, "Attaque : 85 % de la réduction en 5 ms", at5ms / peak);
        check (tailAt > 0.04 && tailAt < 0.30, "Relâchement (60 ms) : retour sous 10 % de la réduction en 40 à 300 ms", tailAt, "s");
    }

    // --- Listen : sortie = bande de détection.
    {
        const double fs = 96000.0;
        E c; c.prepare (fs, 256);
        setParam (c, E::listen, 1.0);
        c.reset();
        Buf x (96000), r;
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = 0.25 * std::sin (dsp::kTwoPi * 6500.0 * (double) i / fs) + 0.25 * std::sin (dsp::kTwoPi * 300.0 * (double) i / fs);
        r = x;
        runModule (c, x, r);
        const double sib = amplitudeAt (x, 48000, 48000, 6500.0, fs) / 0.25;
        const double low = amplitudeAt (x, 48000, 48000, 300.0, fs) / 0.25;
        check (std::abs (sib - 1.0) < 0.02 && low < 0.1, "Listen : seule la bande sibilante passe (6,5 kHz ≈ 0 dB, 300 Hz < -20 dB)", low);
    }

    // --- Robustesse : silence, signal plein, sans NaN.
    {
        E c; c.prepare (384000.0, 256);
        Buf x (384000), r;
        PortableGauss g (3, 0.5);
        for (auto& v : x) v = g();
        r = x;
        runModule (c, x, r);
        bool finite = true;
        for (double v : x) finite = finite && std::isfinite (v);
        check (finite, "Bruit fort à 384 kHz : sortie finie");
    }
}

static void testLoudness()
{
    std::puts ("\n== 17. Mesure de sonie BS.1770-4 / EBU R128");
    auto feed = [] (LoudnessMeter& m, double fs, double f, double ampDb, double seconds, bool mono)
    {
        const std::size_t n = (std::size_t) (seconds * fs);
        Buf x = sine (n, f, fs, std::pow (10.0, ampDb / 20.0)), y = x;
        for (std::size_t off = 0; off < n; off += 512)
            m.process (x.data() + off, y.data() + off, (int) std::min<std::size_t> (512, n - off), mono);
    };

    for (double fs : { 44100.0, 48000.0, 96000.0, 384000.0 })
    {
        LoudnessMeter m; m.prepare (fs);
        feed (m, fs, 997.0, -23.0, 20.0, false);
        const auto& r = m.getReadout();
        char label[96];
        std::snprintf (label, sizeof label, "Sinus 997 Hz -23 dBFS stéréo = -23,0 LUFS (%.1f kHz) : intégrée", fs / 1000.0);
        check (std::abs (r.integrated.load() + 23.0) < 0.1, label, r.integrated.load(), "LUFS");
        check (std::abs (r.shortTerm.load() + 23.0) < 0.1 && std::abs (r.momentary.load() + 23.0) < 0.1, "  court terme et momentanée = -23,0 LUFS", r.shortTerm.load(), "LUFS");
    }
    {
        LoudnessMeter m; m.prepare (48000.0);
        feed (m, 48000.0, 997.0, -23.0, 10.0, true);
        check (std::abs (m.getReadout().integrated.load() + 26.01) < 0.1, "Mono : un canal compte une fois (-23 dBFS -> -26,0 LUFS)", m.getReadout().integrated.load(), "LUFS");
    }
    {   // Portes : silence et passage très faible ignorés, passage à -31 LUFS pris en compte.
        LoudnessMeter m; m.prepare (48000.0);
        feed (m, 48000.0, 997.0, -23.0, 10.0, false);
        feed (m, 48000.0, 997.0, -60.0, 10.0, false);   // < -33 : porte relative
        check (std::abs (m.getReadout().integrated.load() + 23.0) < 0.1, "Porte relative : un passage à -60 LUFS n'abaisse pas la mesure", m.getReadout().integrated.load(), "LUFS");

        LoudnessMeter k; k.prepare (48000.0);
        feed (k, 48000.0, 997.0, -23.0, 10.0, false);
        feed (k, 48000.0, 997.0, -31.0, 10.0, false);   // > -33 : compté
        const double expected = -0.691 + 10.0 * std::log10 (0.5 * (std::pow (10.0, (-23.0 + 0.691) / 10.0) + std::pow (10.0, (-31.0 + 0.691) / 10.0)));
        check (std::abs (k.getReadout().integrated.load() - expected) < 0.2, "Porte relative : un passage à -31 LUFS est compté", k.getReadout().integrated.load() - expected, "LU d'écart");

        LoudnessMeter z; z.prepare (48000.0);
        feed (z, 48000.0, 997.0, -100.0, 5.0, false);
        check (z.getReadout().integrated.load() <= -119.0, "Porte absolue : silence -> pas de valeur intégrée", z.getReadout().integrated.load());
    }
    {   // EBU Tech 3342 : 20 s à -20 LUFS puis 20 s à -30 LUFS -> LRA ≈ 10 LU.
        LoudnessMeter m; m.prepare (48000.0);
        feed (m, 48000.0, 997.0, -20.0, 20.0, false);
        feed (m, 48000.0, 997.0, -30.0, 20.0, false);
        check (std::abs (m.getReadout().range.load() - 10.0) < 1.0, "Loudness Range : -20 puis -30 LUFS = 10 LU (±1)", m.getReadout().range.load(), "LU");
    }
    {
        LoudnessMeter m; m.prepare (96000.0);
        feed (m, 96000.0, 1000.0, -6.0, 1.0, false);
        check (std::abs (m.getReadout().peakDb.load() + 6.02) < 0.05, "Crête échantillon = -6,02 dBFS", m.getReadout().peakDb.load(), "dBFS");
        m.reset();
        check (m.getReadout().peakDb.load() <= -119.0 && m.getReadout().integrated.load() <= -119.0, "reset() remet tout à zéro");
    }
    {   // Dans le moteur : désactivée par défaut (rien de publié), activée -> même valeur.
        const double fs = 96000.0;
        RackEngine e;
        TestParams p;
        p.bind (e);
        e.prepare (fs, 512);
        auto L = sine ((std::size_t) (fs * 6.0), 997.0, fs, std::pow (10.0, -23.0 / 20.0)), R = L;
        runEngine (e, L, R, 0, L.size() / 2, 512);
        check (e.getLoudness().integrated.load() <= -119.0, "Moteur : mesure désactivée -> rien n'est publié");
        e.setLoudnessEnabled (true);
        runEngine (e, L, R, L.size() / 2, L.size(), 512);
        check (std::abs (e.getLoudness().momentary.load() + 23.0) < 0.2, "Moteur : mesure activée -> momentanée = -23 LUFS", e.getLoudness().momentary.load(), "LUFS");
        e.requestLoudnessReset();
        auto L2 = sine (4096, 997.0, fs, 0.01), R2 = L2;
        runEngine (e, L2, R2, 0, L2.size(), 512);
        check (e.getLoudness().integrated.load() <= -119.0, "Moteur : requestLoudnessReset() efface l'historique");
    }
}


//==============================================================================
static double dbOf (double a) { return 20.0 * std::log10 (std::max (a, 1.0e-12)); }

/** Niveau (dB) de la composante f d'un tampon, sur les 1/2 dernières secondes. */
static double toneDb (const Buf& x, double f, double fs) { return dbOf (amplitudeAt (x, x.size() / 2, x.size() / 2, f, fs)); }

static void testV9c3()
{
    std::puts ("\n== 18. V9C3 (compresseur VCA, Auto Focus)");
    using C = AuraV9C3;
    check (C::info().params.size() == 10 && C::info().visibleMask (0) == 0x3FFu, "10 paramètres, tous visibles (aucun bouton mort)");

    auto prep = [] (C& c, double fs, double thrDbu, int ratio, int release = 3, int attack = 4)
    {
        c.prepare (fs, 256);
        setParam (c, C::threshold, thrDbu);
        setParam (c, C::ratio, ratio);
        setParam (c, C::attack, attack);
        setParam (c, C::release, release);
        c.reset();
    };
    auto settledGr = [&] (C& c, double fs, double f, double ampDb, double seconds = 1.5)
    {
        Buf x = sine ((std::size_t) (fs * seconds), f, fs, std::pow (10.0, ampDb / 20.0)), r = x;
        double m = 0.0; int cnt = 0;
        const std::size_t half = x.size() / 2;
        for (std::size_t off = 0; off < x.size(); off += 256)
        {
            const int n = (int) std::min<std::size_t> (256, x.size() - off);
            c.process (x.data() + off, r.data() + off, n, {});
            if (off >= half) { m += c.getGainReductionDb(); ++cnt; }
        }
        return m / cnt;
    };

    // --- Courbes statiques : théorie à ±0,3 dB (Peak et RMS), tous les ratios, étalonnage dBu.
    {
        const struct { int idx; const char* name; } ratios[] = { { C::r2, "2:1" }, { C::r4, "4:1" }, { C::r8, "8:1" }, { C::r10, "10:1" }, { C::limit, "Limit 40:1" }, { C::soft, "Soft" } };
        for (int det = 0; det < 2; ++det)
            for (auto& r : ratios)
            {
                C c; prep (c, 48000.0, 0.0, r.idx);   // seuil 0 dBu = -18 dBFS
                setParam (c, C::detect, det);
                const double gr = settledGr (c, 48000.0, 1000.0, -8.0);   // 10 dB au-dessus du seuil
                const double theory = C::staticReduction (-8.0, -18.0, r.idx);
                char what[128];
                std::snprintf (what, sizeof what, "Réduction statique %s (%s) : théorie à ±0,3 dB (seuil 0 dBu, +10 dB)", r.name, det ? "RMS" : "Peak");
                check (std::abs (gr - theory) < 0.3, what, gr - theory, "dB");
            }
        const double slope0 = (C::staticReduction (-17.0, -18.0, C::soft) - C::staticReduction (-18.0, -18.0, C::soft)) / 1.0;
        const double slope60 = (C::staticReduction (-18.0 + 61.0, -18.0, C::soft) - C::staticReduction (-18.0 + 59.0, -18.0, C::soft)) / 2.0;
        check (slope0 < 0.1 && 1.0 / (1.0 - slope60) > 5.0, "Soft : pente quasi nulle au seuil (~1:1), ratio local > 5:1 à +60 dB (→ 8:1)", 1.0 / (1.0 - slope60), ":1 local à +60 dB");
        double a = 0.0, b = 0.0;
        { C c; prep (c, 44100.0, 0.0, C::r4); a = settledGr (c, 44100.0, 1000.0, -8.0); }
        { C c; prep (c, 384000.0, 0.0, C::r4); b = settledGr (c, 384000.0, 1000.0, -8.0); }
        check (std::abs (a - b) < 0.1, "44,1 vs 384 kHz : même réduction", std::abs (a - b), "dB");
    }

    // --- Modes de détection.
    {
        const double fs = 48000.0;
        auto run = [&] (int mode, double lAmpDb, double rAmpDb, double rPhase = 0.0)
        {
            C c; prep (c, fs, 0.0, C::r8);
            setParam (c, C::mode, mode);
            Buf L = sine ((std::size_t) (fs * 1.5), 1000.0, fs, std::pow (10.0, lAmpDb / 20.0));
            Buf R = sine ((std::size_t) (fs * 1.5), 1000.0, fs, std::pow (10.0, rAmpDb / 20.0), rPhase);
            const Buf inL = L, inR = R;
            runModule (c, L, R);
            struct Out { double dl, dr; Buf L, R, inL, inR; };
            return Out { toneDb (L, 1000.0, fs) - toneDb (inL, 1000.0, fs), toneDb (R, 1000.0, fs) - toneDb (inR, 1000.0, fs), L, R, inL, inR };
        };
        const auto st = run (C::stereo, -8.0, -28.0);
        check (std::abs (st.dl - st.dr) < 0.05 && st.dl < -5.0, "Stereo : un seul gain pour L et R (le canal faible suit le fort, image intacte)", st.dl - st.dr, "dB");
        const auto du = run (C::dual, -8.0, -28.0);
        check (du.dl < -5.0 && std::abs (du.dr) < 0.3, "Dual : le canal fort est comprimé, le faible reste intact", du.dr, "dB");
        const auto mo = run (C::mono, -8.0, -8.0, 3.14159265358979);   // L = -R : somme nulle
        const auto s2 = run (C::stereo, -8.0, -8.0, 3.14159265358979);
        check (std::abs (mo.dl) < 0.3 && s2.dl < -5.0, "Mono SC : une clé sommée ne voit pas un signal en opposition de phase (Stereo, si)", mo.dl);
        // M/S : Mid fort (L = R), Side faible : le Mid est comprimé, le Side ne bouge pas.
        {
            C c; prep (c, fs, 0.0, C::r8);
            setParam (c, C::mode, C::midSide);
            Buf L (96000), R (96000), inS (96000);
            for (std::size_t i = 0; i < L.size(); ++i)
            {
                const double t = (double) i / fs, mid = 0.4 * std::sin (dsp::kTwoPi * 1000.0 * t), side = 0.01 * std::sin (dsp::kTwoPi * 3000.0 * t);
                L[i] = mid + side; R[i] = mid - side; inS[i] = side;
            }
            const Buf inL = L;
            runModule (c, L, R);
            Buf sOut (L.size());
            for (std::size_t i = 0; i < L.size(); ++i) sOut[i] = 0.5 * (L[i] - R[i]);
            const double midRed = toneDb (L, 1000.0, fs) - toneDb (inL, 1000.0, fs), sideDelta = toneDb (sOut, 3000.0, fs) - toneDb (inS, 3000.0, fs);
            check (midRed < -4.0 && std::abs (sideDelta) < 0.3, "M/S : le Mid est comprimé, le Side reste intact", sideDelta, "dB");
        }
    }

    // --- Attack / Release : crans exacts, Auto dépendant du programme, indépendance de fs.
    {
        auto releaseTime = [] (double fs, int releaseIdx)
        {
            C c; c.prepare (fs, 256);
            setParam (c, C::threshold, -10.0); setParam (c, C::ratio, C::limit); setParam (c, C::release, releaseIdx); setParam (c, C::attack, 0);
            c.reset();
            const std::size_t nBurst = (std::size_t) (0.5 * fs), nTail = (std::size_t) (3.0 * fs);
            double peak = 0.0, t37 = -1.0;
            for (std::size_t i = 0; i < nBurst + nTail; ++i)
            {
                double l = (i < nBurst ? 0.5 : 0.0001) * std::sin (dsp::kTwoPi * 1000.0 * (double) i / fs), r = l;
                c.process (&l, &r, 1, {});
                const double gr = c.getGainReductionDb();
                if (i < nBurst) peak = gr;
                else if (t37 < 0.0 && gr < peak * 0.368) t37 = (double) (i - nBurst) / fs;
            }
            return t37;
        };
        for (auto [idx, ms] : { std::pair { 2, 120.0 }, std::pair { 5, 380.0 }, std::pair { 8, 1000.0 } })
        {
            const double t = releaseTime (96000.0, idx) * 1000.0;
            char what[96];
            std::snprintf (what, sizeof what, "Release %.0f ms : constante de temps (37 %%) à ±20 %%", ms);
            check (std::abs (t - ms) < 0.2 * ms, what, t, "ms");
        }
        check (std::abs (releaseTime (44100.0, 5) - releaseTime (384000.0, 5)) < 0.01, "Release identique à 44,1 et 384 kHz", releaseTime (44100.0, 5) - releaseTime (384000.0, 5), "s");

        auto attackTime = [] (double fs, int attackIdx)
        {
            C c; c.prepare (fs, 256);
            setParam (c, C::threshold, -10.0); setParam (c, C::ratio, C::limit); setParam (c, C::attack, attackIdx); setParam (c, C::release, 9);
            c.reset();
            const auto n = (std::size_t) (0.5 * fs);
            double final = 0.0, t63 = -1.0;
            Buf x (n);
            for (std::size_t i = 0; i < n; ++i) x[i] = 0.5 * std::sin (dsp::kTwoPi * 1000.0 * (double) i / fs);
            std::vector<double> gr (n);
            for (std::size_t i = 0; i < n; ++i) { double l = x[i], r = l; c.process (&l, &r, 1, {}); gr[i] = c.getGainReductionDb(); }
            final = gr.back();
            for (std::size_t i = 0; i < n; ++i) if (gr[i] >= 0.632 * final) { t63 = (double) i / fs; break; }
            return t63;
        };
        const double a6 = attackTime (96000.0, 6) * 1000.0, a40 = attackTime (96000.0, 9) * 1000.0;
        check (a6 > 3.0 && a6 < 12.0 && a40 > 24.0 && a40 < 64.0, "Attack 6 ms et 40 ms : temps de montée à 63 % cohérents", a6, "ms (40 ms : mesuré ci-dessous)");
        check (std::abs (attackTime (44100.0, 6) - attackTime (384000.0, 6)) < 0.002, "Attack identique à 44,1 et 384 kHz", attackTime (44100.0, 6) - attackTime (384000.0, 6), "s");

        // Auto Fast / Auto Slow : relâchement plus court après un passage bref qu'après une compression soutenue.
        auto autoRelease = [] (int idx, double burstS)
        {
            const double fs = 48000.0;
            C c; c.prepare (fs, 256);
            setParam (c, C::threshold, -10.0); setParam (c, C::ratio, C::r8); setParam (c, C::release, idx);
            c.reset();
            const std::size_t nBurst = (std::size_t) (burstS * fs), nTail = (std::size_t) (10.0 * fs);
            double peak = 0.0, t = -1.0;
            for (std::size_t i = 0; i < nBurst + nTail; ++i)
            {
                double l = (i < nBurst ? 0.5 : 0.0001) * std::sin (dsp::kTwoPi * 1000.0 * (double) i / fs), r = l;
                c.process (&l, &r, 1, {});
                const double gr = c.getGainReductionDb();
                if (i < nBurst) peak = gr; else if (t < 0.0 && gr < 0.5 * peak) t = (double) (i - nBurst) / fs;
            }
            return t;
        };
        const double fastShort = autoRelease (10, 0.15), fastLong = autoRelease (10, 8.0), slowShort = autoRelease (11, 0.15);
        check (fastLong > 3.0 * fastShort, "Auto Fast : relâchement dépendant du programme (bref vs soutenu)", fastLong / fastShort, "× plus long");
        check (slowShort > fastShort, "Auto Slow plus lent qu'Auto Fast", slowShort / fastShort, "×");
    }

    // --- Filtre HP de clé : un 40 Hz fort ne déclenche pas la compression avec 140 Hz.
    {
        const double fs = 48000.0;
        C off; prep (off, fs, 0.0, C::r8);
        C hp; prep (hp, fs, 0.0, C::r8); setParam (hp, C::scHpf, 2);
        const double a = settledGr (off, fs, 40.0, -6.0), b = settledGr (hp, fs, 40.0, -6.0);
        check (a > 3.0 && b < 0.5, "SC HPF 140 Hz : un 40 Hz à -6 dBFS ne pompe plus (GR Off, HPF)", b, "dB (Off : > 3 dB)");
    }

    // --- Transparence au repos, distorsion VCA progressive et sans repliement.
    {
        const double fs = 96000.0;
        auto run1k = [&] (double thrDbu, double makeUp, double inDb, int ratio, double f = 1000.0, double rate = 96000.0)
        {
            C c; prep (c, rate, thrDbu, ratio);
            setParam (c, C::makeUp, makeUp);
            c.reset();
            Buf x = sine ((std::size_t) (rate * 1.5), f, rate, std::pow (10.0, inDb / 20.0)), r = x;
            const Buf in = x;
            double gr = 0.0; int cnt = 0;
            for (std::size_t off = 0; off < x.size(); off += 256)
            {
                const int n = (int) std::min<std::size_t> (256, x.size() - off);
                c.process (x.data() + off, r.data() + off, n, {});
                if (off >= x.size() / 2) { gr += c.getGainReductionDb(); ++cnt; }
            }
            struct O { Buf out, in; double gr; } o { x, in, gr / cnt };
            return o;
        };
        {   // Au repos : sortie = entrée, bit pour bit (aucun étage saturant, aucun filtre, aucun retard).
            const auto o = run1k (23.0, 0.0, -6.0, C::r4);
            check (o.out == o.in, "Au repos (pas de réduction, Make-Up 0) : sortie = entrée bit pour bit");
            const auto hf = run1k (23.0, 0.0, -6.0, C::r4, 20000.0);
            check (hf.out == hf.in, "Aigus à 20 kHz au repos : intacts (aucune perte dans l'air)");
        }
        auto h3 = [&] (const auto& o, double f, double rate) { return toneDb (o.out, 3.0 * f, rate) - toneDb (o.out, f, rate); };
        auto h2 = [&] (const auto& o, double f, double rate) { return toneDb (o.out, 2.0 * f, rate) - toneDb (o.out, f, rate); };
        const auto g3 = run1k (8.0, 0.0, -6.0, C::r4), g8 = run1k (3.0, 0.0, -6.0, C::r10), g20 = run1k (-5.0, 0.0, -6.0, C::limit);
        char what[200];
        std::snprintf (what, sizeof what, "Distorsion progressive : GR %.1f dB -> H3 %.0f dBc ; GR %.1f dB -> %.0f dBc ; GR %.1f dB -> %.0f dBc",
                       g3.gr, h3 (g3, 1000.0, fs), g8.gr, h3 (g8, 1000.0, fs), g20.gr, h3 (g20, 1000.0, fs));
        check (g3.gr > 1.5 && g3.gr < 4.5 && h3 (g3, 1000.0, fs) < -85.0 && h3 (g8, 1000.0, fs) > h3 (g3, 1000.0, fs) + 6.0 && h3 (g20, 1000.0, fs) > h3 (g8, 1000.0, fs) + 3.0
                   && h3 (g20, 1000.0, fs) < -45.0 && h2 (g3, 1000.0, fs) < -90.0, what, h3 (g3, 1000.0, fs), "dBc à 3 dB de GR");
        const auto mu = run1k (23.0, 24.0, -30.0, C::r4);
        check (mu.gr < 0.01 && std::abs (toneDb (mu.out, 1000.0, fs) - toneDb (mu.in, 1000.0, fs) - 24.0) < 1.0e-6 && h3 (mu, 1000.0, fs) < -120.0,
               "Make-Up 24 dB sans réduction : gain exact, aucune coloration ajoutée", h3 (mu, 1000.0, fs), "dBc");
        {   // Pas de repliement : 18 kHz fortement comprimé à 44,1 kHz (H3 = 54 kHz se replierait à 9,9 kHz).
            const auto a = run1k (-22.0, 0.0, 0.0, C::limit, 18000.0, 44100.0);
            const double alias = toneDb (a.out, 9900.0, 44100.0) - toneDb (a.out, 18000.0, 44100.0);
            const double alias2 = toneDb (a.out, 8100.0, 44100.0) - toneDb (a.out, 18000.0, 44100.0);   // 2·18 − 44,1 = -8,1 -> 8,1 kHz
            check (alias < -110.0 && alias2 < -110.0, "18 kHz comprimé à 44,1 kHz : repliements < -110 dBc", std::max (alias, alias2), "dBc");
        }
    }

    // --- Auto Focus : réduction moyenne = cible, indépendante du niveau, et aussi en Soft / RMS / M-S.
    {
        const double fs = 48000.0;
        auto prepAuto = [&] (C& c, double target, int ratio, int detect = 0, int mode = 0)
        {
            c.prepare (fs, 256);
            setParam (c, C::ratio, ratio); setParam (c, C::attack, 5); setParam (c, C::release, 5);
            setParam (c, C::detect, detect); setParam (c, C::mode, mode);
            setParam (c, C::focus, target); setParam (c, C::autoFocus, 1.0);
            c.reset();
        };
        for (double target : { 3.0, 6.0 })
            for (double level : { -24.0, -12.0 })
            {
                C c; prepAuto (c, target, C::r4);
                auto r = runFocus (c, fs, 45.0, [level] (double) { return level; });
                const double m = meanActive (r, 35.0, 45.0);
                char what[128];
                std::snprintf (what, sizeof what, "Auto Focus V9C3 : cible %.0f dB, parole à %.0f dBFS", target, level);
                check (std::abs (m - target) < 1.0, what, m, "dB");
            }
        for (auto [ratio, det, mode, name] : { std::tuple { (int) C::soft, 0, 0, "Soft" }, std::tuple { (int) C::r8, 1, 0, "RMS 8:1" }, std::tuple { (int) C::r4, 0, 3, "M/S" } })
        {
            C c; prepAuto (c, 6.0, ratio, det, mode);
            auto r = runFocus (c, fs, 45.0, [] (double) { return -18.0; });
            const double m = meanActive (r, 35.0, 45.0);
            char what[128];
            std::snprintf (what, sizeof what, "Auto Focus V9C3 (%s) : cible 6 dB à ±1,5 dB", name);
            check (std::abs (m - 6.0) < 1.5, what, m, "dB");
        }
        C c; prepAuto (c, 6.0, C::r4);
        auto r = runFocus (c, fs, 60.0, [] (double) { return -20.0; });
        check (worstWindowSpan (r, 30.0, 60.0, 0.5) < 0.4 && maxSlew (r) <= 6.05, "Anti-pompage : seuil quasi immobile (0,5 s < 0,4 dB), vitesse ≤ 6 dB/s", worstWindowSpan (r, 30.0, 60.0, 0.5), "dB");
        check (r.threshold.back() > -22.0 && r.threshold.back() < 23.0, "Seuil effectif publié en dBu, dans la plage", r.threshold.back(), "dBu");
    }
    {   // Robustesse : plein signal à 384 kHz, tous les modes, aucun NaN.
        bool finite = true;
        for (int mode = 0; mode < 4; ++mode)
        {
            C c; c.prepare (384000.0, 256); setParam (c, C::mode, mode); setParam (c, C::ratio, C::limit); setParam (c, C::makeUp, 24.0); c.reset();
            Buf L (200000), R;
            PortableGauss g (11 + (unsigned) mode, 0.5);
            for (auto& v : L) v = g();
            R = L;
            runModule (c, L, R);
            for (double v : L) finite = finite && std::isfinite (v);
        }
        check (finite, "Bruit fort, Limit 40:1, Make-Up 24 dB, 4 modes à 384 kHz : sortie finie");
    }
}

static void testV9p2()
{
    std::puts ("\n== 19. V9P2 (préampli double canal)");
    using P = AuraV9P2;
    check (P::info().params.size() == 8 && P::info().visibleMask (0) == 0xFFu, "8 paramètres, tous visibles, plus de 48V décoratif (aucun bouton mort)");

    const double fs = 96000.0;
    auto make = [&] (double fs2 = 96000.0) { auto c = std::make_unique<P>(); c->prepare (fs2, 256); return c; };
    auto run = [&] (P& c, double f, double ampDb, double fs2 = 96000.0, double seconds = 1.0)
    {
        Buf L = sine ((std::size_t) (fs2 * seconds), f, fs2, std::pow (10.0, ampDb / 20.0)), R = L; const Buf in = L;
        runModule (c, L, R);
        struct O { Buf L, R, in; } o { L, R, in };
        return o;
    };
    auto gainDb = [&] (const auto& o, double f, double fs2 = 96000.0) { return toneDb (o.L, f, fs2) - toneDb (o.in, f, fs2); };

    // --- Gain : 36 dB = unité ; cran de 6 dB ; Fine ; Pad.
    for (auto [idx, fine, pad] : { std::tuple { 4, 0.0, 0 }, std::tuple { 6, 0.0, 0 }, std::tuple { 0, 0.0, 0 }, std::tuple { 4, 6.0, 0 }, std::tuple { 6, 0.0, 1 } })
    {
        auto c = make();
        setParam (*c, P::gain, idx); setParam (*c, P::fine, fine); setParam (*c, P::pad, pad); c->reset();
        const auto o = run (*c, 1000.0, -60.0);
        const double expected = P::netGainDb (idx, fine, pad != 0);
        char what[128];
        std::snprintf (what, sizeof what, "Gain %d dB + Fine %.0f%s : %+.0f dB net (mesuré à -60 dBFS)", 12 + 6 * idx, fine, pad ? " + Pad" : "", expected);
        check (std::abs (gainDb (o, 1000.0) - expected) < 0.05, what, gainDb (o, 1000.0) - expected, "dB d'écart");
    }

    // --- HPF 12 dB/oct, indépendant de fs.
    for (double rate : { 44100.0, 384000.0 })
        for (auto [idx, hz] : { std::pair { 1, 80.0 }, std::pair { 2, 120.0 } })
        {
            auto c = make (rate);
            setParam (*c, P::hpf, idx); c->reset();
            const auto a = run (*c, hz * 0.5, -50.0, rate, 1.5);
            const auto b = run (*c, 2000.0, -50.0, rate, 1.0);
            const double lowDb = gainDb (a, hz * 0.5, rate), highDb = gainDb (b, 2000.0, rate);
            char what[128];
            std::snprintf (what, sizeof what, "HPF %.0f Hz à %.1f kHz : -12 dB une octave sous la coupure, 0 dB à 2 kHz", hz, rate / 1000.0);
            check (std::abs (lowDb + 12.3) < 0.6 && std::abs (highDb) < 0.1, what, lowDb, "dB");
        }
    {
        auto c = make();
        const auto o = run (*c, 40.0, -50.0, fs, 1.5);
        check (std::abs (gainDb (o, 40.0)) < 0.1, "HPF Off : 40 Hz inchangé à 0,1 dB (blocage du continu à 5 Hz : -0,07 dB)", gainDb (o, 40.0), "dB");
    }

    // --- Polarité (Off / L / R / L+R) et Dual / Linked.
    {
        for (int ph = 0; ph < 4; ++ph)
        {
            auto c = make();
            setParam (*c, P::phase, ph); c->reset();
            const auto o = run (*c, 1000.0, -60.0, fs, 0.5);
            // corrélation avec l'entrée : +1 normal, -1 inversé (après la rampe de 5 ms)
            auto sign = [&] (const Buf& y) { double s = 0.0; for (std::size_t i = 24000; i < y.size(); ++i) s += y[i] * o.in[i]; return s >= 0.0 ? 1 : -1; };
            const bool okL = sign (o.L) == ((ph == 1 || ph == 3) ? -1 : 1), okR = sign (o.R) == ((ph >= 2) ? -1 : 1);
            char what[96];
            std::snprintf (what, sizeof what, "Phase %s : polarité L et R conformes", std::array<const char*, 4> { "Off", "L", "R", "L+R" }[(std::size_t) ph]);
            check (okL && okR, what);
        }
        for (int linked = 0; linked < 2; ++linked)
        {
            auto c = make();
            setParam (*c, P::link, linked); setParam (*c, P::rOffset, 6.0); c->reset();
            const auto o = run (*c, 1000.0, -60.0, fs, 0.5);
            const double d = toneDb (o.R, 1000.0, fs) - toneDb (o.L, 1000.0, fs);
            check (linked ? std::abs (d) < 0.05 : std::abs (d - 6.0) < 0.05, linked ? "Linked : R Offset ignoré (L = R)" : "Dual : R Offset +6 dB appliqué au canal R", d, "dB");
        }
    }

    // --- Pas de clic : changement de cran de gain, de polarité, d'entrée Inst/DI, de HPF pendant un sinus.
    {
        const double rate = 48000.0;
        auto clickRatio = [&] (int param, double from, double to)
        {
            auto c = make (rate);
            setParam (*c, param, from); c->reset();
            Buf L = sine (48000, 1000.0, rate, 0.01), R = L;
            Buf ref = L; auto c2 = make (rate); setParam (*c2, param, from); c2->reset(); Buf r2 = ref; runModule (*c2, ref, r2);
            Buf a (L.begin(), L.begin() + 24000), b (R.begin(), R.begin() + 24000);
            runModule (*c, a, b);
            setParam (*c, param, to);
            Buf a2 (L.begin() + 24000, L.end()), b2 (R.begin() + 24000, R.end());
            runModule (*c, a2, b2);
            Buf all = a; all.insert (all.end(), a2.begin(), a2.end());
            // pente maximale autour du changement vs pente maximale en régime établi (du même signal)
            double changeSlope = 0.0, steady = 0.0;
            for (std::size_t i = 24000; i < 26000; ++i) changeSlope = std::max (changeSlope, std::abs (all[i] - all[i - 1]));
            for (std::size_t i = 20000; i < 22000; ++i) steady = std::max (steady, std::abs (all[i] - all[i - 1]));
            for (std::size_t i = 44000; i < 46000; ++i) steady = std::max (steady, std::abs (all[i] - all[i - 1]));   // après le changement (le gain peut avoir changé)
            return changeSlope / steady;
        };
        check (clickRatio (P::gain, 3.0, 4.0) < 1.6, "Changement de cran de gain : pas de clic (pente ≤ 1,6× celle du sinus)", clickRatio (P::gain, 3.0, 4.0));
        check (clickRatio (P::phase, 0.0, 3.0) < 1.6, "Changement de polarité : pas de clic", clickRatio (P::phase, 0.0, 3.0));
        check (clickRatio (P::input, 0.0, 1.0) < 1.6, "Mic -> Inst/DI : transition sans clic", clickRatio (P::input, 0.0, 1.0));
        check (clickRatio (P::hpf, 0.0, 2.0) < 1.6, "HPF Off -> 120 Hz : sans clic", clickRatio (P::hpf, 0.0, 2.0));
        check (clickRatio (P::pad, 0.0, 1.0) < 1.6, "Pad : sans clic", clickRatio (P::pad, 0.0, 1.0));
    }

    // --- Saturation : propre au niveau nominal, plafond +24 dBu, voyant avant l'écrêtage.
    {
        auto c = make();
        setParam (*c, P::gain, 4); c->reset();
        const auto nominal = run (*c, 1000.0, -18.0);   // 0 dBu
        check (toneDb (nominal.L, 3000.0, fs) - toneDb (nominal.L, 1000.0, fs) < -65.0, "Niveau nominal (0 dBu) : harmonique 3 < -65 dB", toneDb (nominal.L, 3000.0, fs) - toneDb (nominal.L, 1000.0, fs), "dB");
        check (c->takeStatusPeak() < std::pow (10.0, (P::kClipLampDbu + P::kDbuToDbfs) / 20.0), "Voyant : vert au niveau nominal");

        auto hot = make();
        setParam (*hot, P::gain, 8); hot->reset();   // +24 dB net sur un sinus -6 dBFS : +18 dBFS avant l'étage
        const auto o = run (*hot, 1000.0, -6.0);
        double peak = 0.0; for (std::size_t i = o.L.size() / 2; i < o.L.size(); ++i) peak = std::max (peak, std::abs (o.L[i]));
        check (peak < 2.3 && peak > 1.6, "Surcharge : l'étage plafonne autour de +24 dBu (+6 dBFS)", dbOf (peak) , "dBFS");
        check (hot->takeStatusPeak() > std::pow (10.0, (P::kClipLampDbu + P::kDbuToDbfs) / 20.0), "Voyant : rouge en surcharge");
        auto mid = make();
        setParam (*mid, P::gain, 6); mid->reset();   // +12 dB net : sinus -12 dBFS -> 0 dBFS (+18 dBu)
        const auto m = run (*mid, 1000.0, -12.0);
        const double thdMid = toneDb (m.L, 3000.0, fs) - toneDb (m.L, 1000.0, fs), thdHot = toneDb (o.L, 3000.0, fs) - toneDb (o.L, 1000.0, fs);
        check (thdHot > thdMid + 10.0, "La distorsion croît avec le niveau (surcharge vs +18 dBu)", thdHot - thdMid, "dB de plus");
    }

    // --- Bande passante 10 Hz … 35 kHz à ±1 dB (384 kHz), Mic et DI.
    for (int di = 0; di < 2; ++di)
    {
        const double rate = 384000.0;
        double worst = 0.0;
        for (double f : { 10.0, 20.0, 100.0, 1000.0, 10000.0, 20000.0, 35000.0 })
        {
            auto c = make (rate);
            setParam (*c, P::input, di); setParam (*c, P::gain, 4); c->reset();
            const auto o = run (*c, f, -40.0, rate, f < 50.0 ? 2.0 : 0.5);
            worst = std::max (worst, std::abs (gainDb (o, f, rate)));
        }
        check (worst < 1.0, di ? "Inst/DI : 10 Hz … 35 kHz à ±1 dB (384 kHz)" : "Mic (JT-16) : 10 Hz … 35 kHz à ±1 dB (384 kHz)", worst, "dB max");
    }

    // --- Mic (JT-16 + Twin OPA à pleine charge) ≠ Inst/DI : différence mesurable ; DI plus neutre, HF sans pôle.
    {
        auto h = [&] (int di, double gIdx, double ampDb, double* hf = nullptr)
        {
            auto c = make (384000.0);
            setParam (*c, P::input, di); setParam (*c, P::gain, gIdx); c->reset();
            const auto o = run (*c, 1000.0, ampDb, 384000.0, 0.5);
            if (hf) { auto c2 = make (384000.0); setParam (*c2, P::input, di); setParam (*c2, P::gain, 4); c2->reset(); *hf = gainDb (run (*c2, 35000.0, -40.0, 384000.0, 0.5), 35000.0, 384000.0); }
            return std::pair { toneDb (o.L, 3000.0, 384000.0) - toneDb (o.L, 1000.0, 384000.0), toneDb (o.L, 2000.0, 384000.0) - toneDb (o.L, 1000.0, 384000.0) };
        };
        double hfMic = 0.0, hfDi = 0.0;
        const auto mic = h (0, 6.0, -12.0, &hfMic), di = h (1, 6.0, -12.0, &hfDi);
        char what[200];
        std::snprintf (what, sizeof what, "Mic vs DI à +18 dBu : H3 %.0f / %.0f dBc, H2 %.0f / %.0f dBc ; 35 kHz : %.2f / %.2f dB", mic.first, di.first, mic.second, di.second, hfMic, hfDi);
        check (mic.first > di.first + 6.0 && mic.second > di.second + 6.0 && hfMic < hfDi - 0.5 && std::abs (hfDi) < 0.05, what, mic.first - di.first, "dB d'écart H3");
        const auto lowMic = h (0, 0.0, -30.0), hiMic = h (0, 10.0, -30.0);
        check (hiMic.first > lowMic.first + 6.0, "Mic : à gain élevé l'étage Twin OPA travaille « à fond » : plus d'harmoniques qu'à gain bas (même niveau de sortie)", std::min (hiMic.first - lowMic.first, 60.0), "dB");
        check (mic.first < -50.0 && mic.second < -45.0, "Mic à +18 dBu : harmoniques riches mais maîtrisées (H3 < -50, H2 < -45 dBc)", mic.first, "dBc");
    }

    // --- Sortie propre : linéaire jusqu'à +21 dBu, genou doux jusqu'à +24 dBu ; marge +24 dBu ; pas de repliement.
    {
        auto c = make (384000.0);
        setParam (*c, P::input, 1); setParam (*c, P::gain, 4); c->reset();
        auto thd = [&] (double dbfs)
        {
            auto c2 = make (384000.0);
            setParam (*c2, P::input, 1); setParam (*c2, P::gain, 4); c2->reset();
            const auto o = run (*c2, 1000.0, dbfs, 384000.0, 0.3);
            return toneDb (o.L, 3000.0, 384000.0) - toneDb (o.L, 1000.0, 384000.0);
        };
        check (thd (0.0 - 0.1) < -64.0, "Sortie type THAT 1646 : à +17,9 dBu (DI) distorsion H3 < -64 dBc", thd (-0.1), "dBc");
        check (thd (5.0) > thd (0.0) + 10.0, "Au-delà de +21 dBu la sortie entre en compression douce", thd (5.0) - thd (0.0), "dB de plus");
        // Repliement à 44,1 kHz : 15 kHz très fort ; H3 = 45 kHz se replierait à 0,9 kHz
        // Repliement à 44,1 kHz : 7 kHz à +22 dBu (dans le genou) ; le 5e harmonique (35 kHz) se replierait à 9,1 kHz.
        auto c3 = make (44100.0);
        setParam (*c3, P::input, 1); setParam (*c3, P::gain, 6); c3->reset();   // +12 dB net : sinus -8 dBFS -> +4 dBFS (+22 dBu, dans le genou)
        const auto a = run (*c3, 7000.0, -8.0, 44100.0, 1.0);
        double worstSpur = -200.0; const double ref = toneDb (a.L, 7000.0, 44100.0);
        for (double f : { 9100.0, 16100.0, 2100.0, 4900.0 }) worstSpur = std::max (worstSpur, toneDb (a.L, f, 44100.0) - ref);
        check (worstSpur < -50.0, "7 kHz à +22 dBu (genou de sortie) à 44,1 kHz : repliements < -50 dBc (ADAA 1er ordre)", worstSpur, "dBc");
    }

    // --- 44,1 / 384 kHz : même gain, tenue DXD.
    {
        double g[2];
        int k = 0;
        for (double rate : { 44100.0, 384000.0 })
        {
            auto c = make (rate);
            setParam (*c, P::gain, 6); c->reset();
            g[k++] = gainDb (run (*c, 1000.0, -60.0, rate), 1000.0, rate);
        }
        check (std::abs (g[0] - g[1]) < 0.02, "Gain identique à 44,1 et 384 kHz", std::abs (g[0] - g[1]), "dB");
        auto c = make (384000.0);
        Buf L (384000), R; PortableGauss gs (5, 0.5); for (auto& v : L) v = gs(); R = L;
        setParam (*c, P::gain, 10); runModule (*c, L, R);
        bool finite = true; for (double v : L) finite = finite && std::isfinite (v);
        check (finite, "Bruit fort, gain maximal à 384 kHz : sortie finie");
    }
}

static void testLookahead()
{
    std::puts ("\n== 20. E9DS · Lookahead");
    using E = AuraE9DS;
    for (double fs : { 48000.0, 384000.0 })
    {
        E c; c.prepare (fs, 256);
        setParam (c, E::lookahead, 2.0);   // 1 ms
        check (c.getLatencySamples() == (int) std::lround (0.001 * fs), fs < 100000.0 ? "Lookahead 1 ms : latence annoncée = 48 échantillons (48 kHz)" : "Lookahead 1 ms : latence annoncée = 384 échantillons (384 kHz)", c.getLatencySamples());
        setParam (c, E::lookahead, 0.0);
        check (c.getLatencySamples() == 0, "Lookahead Off : latence 0");
    }
    {   // Sous le seuil : sortie = entrée retardée, bit pour bit.
        const double fs = 96000.0;
        E c; c.prepare (fs, 256);
        setParam (c, E::lookahead, 3.0); c.reset();   // 2 ms
        const int lat = c.getLatencySamples();
        Buf x (96000); PortableGauss g (9, 0.003);
        for (auto& v : x) v = g();
        Buf y = x, r = x;
        runModule (c, y, r);
        double maxDiff = 0.0;
        for (std::size_t i = (std::size_t) lat + 400; i < x.size(); ++i) maxDiff = std::max (maxDiff, std::abs (y[i] - x[i - (std::size_t) lat]));
        check (maxDiff == 0.0, "Sous le seuil : sortie = entrée retardée de la latence annoncée, bit-identique (après le fondu de mise en place)", maxDiff);
    }
    {   // Le « t » très bref : le pic de sortie est plus bas avec Lookahead.
        const double fs = 96000.0;
        auto burstPeak = [&] (int look)
        {
            E c; c.prepare (fs, 256);
            setParam (c, E::lookahead, look); setParam (c, E::range, 18.0); c.reset();
            Buf x (48000, 0.0001);
            const std::size_t t0 = 20000, len = (std::size_t) (0.004 * fs);   // « t » de 4 ms
            for (std::size_t i = 0; i < len; ++i) x[t0 + i] = 0.5 * std::sin (dsp::kTwoPi * 6500.0 * (double) i / fs);
            Buf y = x, r = x;
            runModule (c, y, r);
            const std::size_t a = t0 + (std::size_t) c.getLatencySamples(), b = a + (std::size_t) (0.0006 * fs);   // 0,6 ms de sortie du burst
            double peak = 0.0;
            for (std::size_t i = a; i < b; ++i) peak = std::max (peak, std::abs (y[i]));
            return dbOf (peak);
        };
        const double off = burstPeak (0), look = burstPeak (3);
        check (look < off - 2.0, "« t » de 4 ms à 6,5 kHz : crête des 0,6 premières ms de sortie plus basse avec Lookahead 2 ms", off - look, "dB de moins");
    }
    {   // Régime établi identique, quel que soit le cran, et même réduction à 48 et 384 kHz.
        double gr[2];
        int k = 0;
        for (double fs : { 48000.0, 384000.0 })
        {
            E c; c.prepare (fs, 256);
            setParam (c, E::lookahead, 2.0); c.reset();
            Buf x = sine ((std::size_t) fs, 6500.0, fs, 0.25), r = x;
            runModule (c, x, r);
            gr[k++] = c.getGainReductionDb();
        }
        check (std::abs (gr[0] - gr[1]) < 0.2, "Lookahead : même réduction à 48 et 384 kHz", std::abs (gr[0] - gr[1]), "dB");
    }
    {   // Changement de cran en marche : pas de clic.
        const double fs = 96000.0;
        E c; c.prepare (fs, 256); c.reset();
        Buf x = sine (96000, 1000.0, fs, 0.2), r = x;
        Buf a (x.begin(), x.begin() + 48000), b (r.begin(), r.begin() + 48000);
        runModule (c, a, b);
        setParam (c, E::lookahead, 3.0);
        Buf a2 (x.begin() + 48000, x.end()), b2 (r.begin() + 48000, r.end());
        runModule (c, a2, b2);
        a.insert (a.end(), a2.begin(), a2.end());
        const double steady = maxStep (a, 40000, 46000), around = maxStep (a, 48000, 52000);
        check (around < 1.3 * steady, "Changement de Lookahead en marche : fondu sans clic (pente ≤ 1,3× celle du sinus)", around / steady);
    }
    {   // Dans le moteur : latence relue à la volée, Dry compensé (une seule impulsion, amplitude exacte).
        const double fs = 48000.0;
        RackEngine e;
        TestParams p;
        p.bind (e);
        e.prepare (fs, 512);
        p.setType (e, 0, ModuleType::e9ds);
        p.slots[0].mix = 50.0f;
        p.setPlain (0, ModuleType::e9ds, E::lookahead, 2.0);   // 1 ms = 48 échantillons
        Buf L (8192, 0.0), R;
        L[1000] = 0.005;
        R = L;
        runEngine (e, L, R, 0, 2048, 512);   // laisse le moteur relire la latence et finir le fondu
        check (e.getRequestedLatency() == 48, "Moteur : la latence du Lookahead est relue et annoncée (48)", e.getRequestedLatency());
        Buf L2 (8192, 0.0), R2;
        L2[4000] = 0.005; R2 = L2;
        runEngine (e, L2, R2, 0, L2.size(), 512);
        int count = 0; std::size_t at = 0;
        for (std::size_t i = 0; i < L2.size(); ++i) if (std::abs (L2[i]) > 1.0e-6) { ++count; at = i; }
        check (count == 1 && at == 4048 && std::abs (L2[4048] - 0.005) < 1.0e-12, "Moteur : Mix 50 % + Lookahead : une seule impulsion, amplitude exacte, à la latence", L2[at]);
    }
}

//==============================================================================
static double rmsOf (const Buf& x, std::size_t a, std::size_t b)
{
    double s = 0.0;
    for (std::size_t i = a; i < b; ++i) s += x[i] * x[i];
    return std::sqrt (s / (double) (b - a));
}

static void testAutoGain()
{
    std::puts ("\n== 21. Auto-gain (par module et rack)");
    const double fs = 48000.0;
    const std::size_t sec = (std::size_t) fs;

    auto runSlot = [&] (double eqOutDb, bool agc, double seconds, double ampDb, TestParams& p, RackEngine& e, Buf& L, Buf& R, std::size_t from = 0)
    {
        p.setType (e, 0, ModuleType::eq);
        p.setPlain (0, ModuleType::eq, AuraEQ::outGain, eqOutDb);
        p.slots[0].agc = agc ? 1.0f : 0.0f;
        const auto n = (std::size_t) (seconds * fs);
        if (L.size() < from + n) { L.resize (from + n); R.resize (from + n); }
        for (std::size_t i = 0; i < n; ++i) L[from + i] = R[from + i] = std::pow (10.0, ampDb / 20.0) * std::sin (dsp::kTwoPi * 1000.0 * (double) (from + i) / fs);
        runEngine (e, L, R, from, from + n, 256);
    };

    {   // +9 dB de sortie : l'auto-gain ramène le niveau à celui de l'entrée (±0,3 dB), sans l'option : +9 dB.
        RackEngine e; TestParams p; p.bind (e); e.prepare (fs, 256);
        Buf L, R;
        runSlot (9.0, false, 6.0, -20.0, p, e, L, R);
        const double plainDb = dbOf (rmsOf (L, 5 * sec, 6 * sec)) - dbOf (std::pow (10.0, -20.0 / 20.0) / std::sqrt (2.0));
        check (std::abs (plainDb - 9.0) < 0.1, "Sans auto-gain : +9 dB en sortie (module seul)", plainDb, "dB");

        RackEngine e2; TestParams p2; p2.bind (e2); e2.prepare (fs, 256);
        Buf L2, R2;
        runSlot (9.0, true, 10.0, -20.0, p2, e2, L2, R2);
        const double ref = dbOf (std::pow (10.0, -20.0 / 20.0) / std::sqrt (2.0));
        const double after = dbOf (rmsOf (L2, 9 * sec, 10 * sec)) - ref;
        check (std::abs (after) < 0.3, "Auto-gain : le niveau de sortie revient à celui de l'entrée (±0,3 dB après 10 s)", after, "dB");
        check (std::abs (e2.getMeters().slotAgcDb[0].load() + 9.0f) < 0.3f, "Correction publiée ≈ -9 dB", e2.getMeters().slotAgcDb[0].load(), "dB");
    }
    {   // Changement de réglage en marche : le niveau se recale (« ratio / saturation » changent le niveau, l'option le suit).
        RackEngine e; TestParams p; p.bind (e); e.prepare (fs, 256);
        Buf L, R;
        runSlot (6.0, true, 8.0, -20.0, p, e, L, R);
        p.setPlain (0, ModuleType::eq, AuraEQ::outGain, -6.0);
        const auto from = L.size();
        runSlot (-6.0, true, 10.0, -20.0, p, e, L, R, from);
        const double ref = dbOf (std::pow (10.0, -20.0 / 20.0) / std::sqrt (2.0));
        const double after = dbOf (rmsOf (L, from + 9 * sec, from + 10 * sec)) - ref;
        check (std::abs (after) < 0.3, "Réglage changé de +6 à -6 dB pendant la lecture : niveau recalé à ±0,3 dB en 10 s", after, "dB");
    }
    {   // Plafond ±12 dB.
        RackEngine e; TestParams p; p.bind (e); e.prepare (fs, 256);
        Buf L, R;
        runSlot (20.0, true, 8.0, -40.0, p, e, L, R);
        check (std::abs (e.getMeters().slotAgcDb[0].load() + 12.0f) < 0.05f, "Correction limitée à -12 dB (module à +20 dB)", e.getMeters().slotAgcDb[0].load(), "dB");
    }
    {   // Silence et creux : correction gelée. Bypass : gain acquis conservé. Retrait de l'option : retour exact à l'identité.
        RackEngine e; TestParams p; p.bind (e); e.prepare (fs, 256);
        Buf L, R;
        runSlot (9.0, true, 8.0, -20.0, p, e, L, R);
        const float acquired = e.getMeters().slotAgcDb[0].load();
        const auto from = L.size();
        L.resize (from + 5 * sec, 0.0); R = L;
        for (std::size_t i = 0; i < 5 * sec; ++i) L[from + i] = R[from + i] = 1.0e-5 * std::sin (dsp::kTwoPi * 1000.0 * (double) i / fs);   // -100 dBFS
        runEngine (e, L, R, from, from + 5 * sec, 256);
        check (std::abs (e.getMeters().slotAgcDb[0].load() - acquired) < 0.01f, "Silence de 5 s : correction gelée (pas de remontée du bruit)", e.getMeters().slotAgcDb[0].load() - acquired, "dB");

        p.slots[0].bypass = 1.0f;
        const auto from2 = L.size();
        L.resize (from2 + 3 * sec); R.resize (L.size());
        for (std::size_t i = 0; i < 3 * sec; ++i) L[from2 + i] = R[from2 + i] = 0.1 * std::sin (dsp::kTwoPi * 440.0 * (double) i / fs);
        runEngine (e, L, R, from2, from2 + 3 * sec, 256);
        p.slots[0].bypass = 0.0f;
        check (std::abs (e.getMeters().slotAgcDb[0].load() - acquired) < 0.01f, "Bypass : le gain acquis est conservé", e.getMeters().slotAgcDb[0].load() - acquired, "dB");

        p.slots[0].agc = 0.0f;   // retire l'option
        const auto from3 = L.size();
        L.resize (from3 + sec); R.resize (L.size());
        for (std::size_t i = 0; i < sec; ++i) L[from3 + i] = R[from3 + i] = 0.1 * std::sin (dsp::kTwoPi * 440.0 * (double) i / fs);
        runEngine (e, L, R, from3, from3 + sec, 256);
        check (e.getMeters().slotAgcDb[0].load() == 0.0f, "Option retirée : correction ramenée à 0 dB", e.getMeters().slotAgcDb[0].load(), "dB");
    }
    {   // Option désactivée : sortie bit-identique à un moteur qui ne connaît pas l'option (défaut), et zéro CPU de mesure.
        RackEngine a, b; TestParams pa, pb; pa.bind (a); pb.bind (b); a.prepare (fs, 256); b.prepare (fs, 256);
        pa.setType (a, 0, ModuleType::eq); pb.setType (b, 0, ModuleType::eq);
        pa.setPlain (0, ModuleType::eq, AuraEQ::pkGain, 5.0); pb.setPlain (0, ModuleType::eq, AuraEQ::pkGain, 5.0);
        pa.slots[0].agc = 0.0f; pa.rackAgc = 0.0f;
        Buf L = sine (48000, 1000.0, fs, 0.2), R = L, L2 = L, R2 = L;
        runEngine (a, L, R, 0, L.size(), 256); runEngine (b, L2, R2, 0, L2.size(), 256);
        check (L == L2 && R == R2, "Auto-gain désactivé : sortie bit-identique");
    }
    {   // Rack entier.
        RackEngine e; TestParams p; p.bind (e); e.prepare (fs, 256);
        p.setType (e, 0, ModuleType::eq); p.setPlain (0, ModuleType::eq, AuraEQ::outGain, 9.0);
        p.setType (e, 3, ModuleType::eq); p.setPlain (3, ModuleType::eq, AuraEQ::outGain, 3.0);
        p.rackAgc = 1.0f;
        Buf L = sine (12 * sec, 1000.0, fs, 0.1), R = L;
        runEngine (e, L, R, 0, L.size(), 256);
        const double after = dbOf (rmsOf (L, 11 * sec, 12 * sec)) - dbOf (0.1 / std::sqrt (2.0));
        check (std::abs (after) < 0.3 && std::abs (e.getMeters().rackAgcDb.load() + 12.0f) < 0.3f, "Auto-gain du rack : +12 dB cumulés ramenés à ±0,3 dB (correction -12 dB)", after, "dB");
    }
}

static void testStereoUtility()
{
    std::puts ("\n== 22. Utilitaire Stéréo / Largeur / Mono grave");
    auto setup = [] (double fs, double width, int bass, bool mono, TestParams& p, RackEngine& e)
    {
        p.bind (e); e.prepare (fs, 256);
        p.width = (float) width; p.bassMono = (float) bass; p.mono = mono ? 1.0f : 0.0f;
    };
    auto stereoSine = [] (double fs, double fL, double fR, double aL, double aR, std::size_t n, double phaseR = 0.0)
    {
        std::pair<Buf, Buf> io { sine (n, fL, fs, aL), sine (n, fR, fs, aR, phaseR) };
        return io;
    };
    const double fs = 48000.0;
    {   // Identité bit-à-bit.
        RackEngine e; TestParams p; setup (fs, 100.0, 0, false, p, e);
        auto [L, R] = stereoSine (fs, 440.0, 1330.0, 0.3, 0.2, 48000);
        const Buf l0 = L, r0 = R;
        runEngine (e, L, R, 0, L.size(), 256);
        check (L == l0 && R == r0, "Width 100 %, Bass Mono Off, Mono Off : sortie bit-identique à l'entrée");
    }
    {   // Width 0 / 200, Mono.
        auto level = [&] (double width, bool mono)
        {
            RackEngine e; TestParams p; setup (fs, width, 0, mono, p, e);
            // Mid = 0,2 sin(1 kHz), Side = 0,1 sin(3 kHz)
            Buf L (48000), R (48000);
            for (std::size_t i = 0; i < L.size(); ++i)
            {
                const double t = (double) i / fs, m = 0.2 * std::sin (dsp::kTwoPi * 1000.0 * t), sd = 0.1 * std::sin (dsp::kTwoPi * 3000.0 * t);
                L[i] = m + sd; R[i] = m - sd;
            }
            runEngine (e, L, R, 0, L.size(), 256);
            Buf M (L.size()), S (L.size());
            for (std::size_t i = 0; i < L.size(); ++i) { M[i] = 0.5 * (L[i] + R[i]); S[i] = 0.5 * (L[i] - R[i]); }
            return std::pair { amplitudeAt (M, 24000, 24000, 1000.0, fs), amplitudeAt (S, 24000, 24000, 3000.0, fs) };
        };
        const auto w0 = level (0.0, false), w200 = level (200.0, false), w50 = level (50.0, false), mo = level (100.0, true);
        check (w0.second < 1.0e-6 && std::abs (w0.first - 0.2) < 1.0e-6, "Width 0 % : Side nul, Mid intact (mono)", w0.second);
        check (std::abs (w200.second - 0.2) < 1.0e-4 && std::abs (w200.first - 0.2) < 1.0e-6, "Width 200 % : Side doublé, Mid intact", w200.second);
        check (std::abs (w50.second - 0.05) < 1.0e-4, "Width 50 % : Side divisé par deux", w50.second);
        check (mo.second < 1.0e-6 && std::abs (mo.first - 0.2) < 1.0e-6, "Mono : Side nul, Mid intact", mo.second);
    }
    {   // Mono grave : Side filtré LR4, Mid intact, même résultat à 44,1 et 384 kHz.
        double att[2], atFc[2], hi[2];
        int k = 0;
        for (double rate : { 44100.0, 384000.0 })
        {
            auto measure = [&] (double f, double wantFc)
            {
                RackEngine e; TestParams p; setup (rate, 100.0, wantFc == 150.0 ? 3 : 2, false, p, e);
                const auto n = (std::size_t) (rate * 1.5);
                Buf L = sine (n, f, rate, 0.2), R = L;
                for (auto& v : R) v = -v;   // Side pur
                const Buf in = L;
                runEngine (e, L, R, 0, n, 256);
                return dbOf (amplitudeAt (L, n / 2, n / 2, f, rate)) - dbOf (amplitudeAt (in, n / 2, n / 2, f, rate));
            };
            att[k] = measure (37.5, 150.0);     // 2 octaves sous 150 Hz : LR4 -> ≈ -48 dB
            atFc[k] = measure (150.0, 150.0);   // à la coupure : -6 dB
            hi[k] = measure (3000.0, 150.0);
            ++k;
        }
        check (att[0] < -35.0 && att[1] < -35.0, "Mono grave 150 Hz : 37,5 Hz (Side pur) atténué de plus de 35 dB", att[1], "dB");
        check (std::abs (atFc[0] + 6.02) < 0.3 && std::abs (atFc[1] + 6.02) < 0.3, "À la fréquence de coupure : -6 dB (Linkwitz-Riley), 44,1 et 384 kHz", atFc[1], "dB");
        check (std::abs (hi[0]) < 0.05 && std::abs (hi[1]) < 0.05, "Au-dessus : Side intact (3 kHz)", hi[1], "dB");
        check (std::abs (att[0] - att[1]) < 6.0, "Même profondeur de coupure à 44,1 et 384 kHz (±6 dB à -45 dB)", att[0] - att[1], "dB");
        // Mid intact : un Mid grave identique sur L et R traverse sans changement.
        RackEngine e; TestParams p; setup (fs, 100.0, 4, false, p, e);
        Buf L = sine (48000, 40.0, fs, 0.3), R = L; const Buf in = L;
        runEngine (e, L, R, 0, L.size(), 256);
        double d = 0.0; for (std::size_t i = 4800; i < L.size(); ++i) d = std::max (d, std::abs (L[i] - in[i]));
        check (d < 1.0e-12 && L == R, "Mid grave (L = R) à 40 Hz, Bass Mono 250 Hz : inchangé et toujours mono", d);
    }
    {   // Pas de clic : Mono, Width, Bass Mono commutés en lecture.
        RackEngine e; TestParams p; setup (fs, 100.0, 0, false, p, e);
        Buf L = sine (96000, 500.0, fs, 0.3), R = sine (96000, 500.0, fs, 0.3, 1.0);
        runEngine (e, L, R, 0, 24000, 256);
        p.mono = 1.0f;
        runEngine (e, L, R, 24000, 48000, 256);
        p.mono = 0.0f; p.bassMono = 2.0f; p.width = 150.0f;
        runEngine (e, L, R, 48000, 96000, 256);
        const double steady = maxStep (L, 10000, 20000);
        check (maxStep (L, 24000, 26000) < 1.3 * steady && maxStep (L, 48000, 50000) < 1.5 * steady, "Mono / Width / Bass Mono commutés en lecture : sans clic", maxStep (L, 24000, 26000) / steady);
    }
    {   // Corrélation.
        auto corr = [&] (double phaseR, bool independent)
        {
            RackEngine e; TestParams p; setup (fs, 100.0, 0, false, p, e);
            Buf L (96000), R (96000);
            PortableGauss g1 (1, 0.1), g2 (2, 0.1);
            for (std::size_t i = 0; i < L.size(); ++i)
            {
                L[i] = g1();
                R[i] = independent ? g2() : (phaseR > 3.0 ? -L[i] : L[i]);
            }
            runEngine (e, L, R, 0, L.size(), 256);
            return e.getMeters().correlation.load();
        };
        check (corr (0.0, false) > 0.999f && corr (3.14, false) < -0.999f && std::abs (corr (0.0, true)) < 0.1f, "Corrélation L/R : +1 (mono), -1 (opposition), ≈ 0 (indépendants)", corr (0.0, true));
    }
}


//==============================================================================
// Nouveaux modules : F976, D9X, AuraSculpt, AuraTone
//==============================================================================
struct GrRun { Buf out, in; double gr; };

template <class M>
static GrRun runTone (M& c, double f, double ampDb, double rate, double seconds = 1.2)
{
    c.reset();
    Buf x = sine ((std::size_t) (rate * seconds), f, rate, std::pow (10.0, ampDb / 20.0)), r = x;
    const Buf in = x;
    double gr = 0.0; int cnt = 0;
    for (std::size_t off = 0; off < x.size(); off += 256)
    {
        const int n = (int) std::min<std::size_t> (256, x.size() - off);
        c.process (x.data() + off, r.data() + off, n, {});
        if (off >= x.size() * 2 / 3) { gr += c.getGainReductionDb(); ++cnt; }
    }
    return { x, in, gr / std::max (cnt, 1) };
}

/** Temps (s) pour que la réduction atteigne 63 % de sa valeur finale sur un échelon continu de niveau `levelDb`. */
template <class M>
static double stepT63 (M& c, double levelDb, double rate, double* finalGr = nullptr)
{
    c.reset();
    const double a = std::pow (10.0, levelDb / 20.0);
    const int total = (int) (rate * 0.5);
    std::vector<double> gr ((std::size_t) total);
    for (int i = 0; i < total; ++i) { double l = a, r = a; c.process (&l, &r, 1, {}); gr[(std::size_t) i] = c.getGainReductionDb(); }
    const double fin = gr[(std::size_t) total - 1];
    if (finalGr) *finalGr = fin;
    for (int i = 0; i < total; ++i) if (gr[(std::size_t) i] >= 0.63 * fin) return (double) i / rate;
    return 1.0;
}

static void testF976()
{
    std::puts ("\n== 21. F976 (compresseur FET 1176)");
    using F = AuraF976;
    check (F::info().params.size() == 8 && F::info().visibleMask (0) == 0xFFu, "8 paramètres, tous visibles");
    const double fs = 96000.0;
    auto make = [&] (double rate = 96000.0) { auto c = std::make_unique<F>(); c->prepare (rate, 256); return c; };

    {   // Sous le seuil, le gain est unité (Input 10 / Output -10 par défaut).
        auto c = make();
        const auto o = runTone (*c, 4000.0, -40.0, fs);
        const double g = toneDb (o.out, 4000.0, fs) - toneDb (o.in, 4000.0, fs);
        check (std::abs (g) < 0.1 && o.gr < 0.01, "Défaut (Input 10 / Output -10) sous le seuil : gain unité", g, "dB");
    }
    // Courbes statiques : Input 0 / Output 0, tone 4 kHz, 8 dB au-dessus du seuil fixe (-10 dBFS) -> -2 dBFS.
    for (int ratio = 0; ratio < 5; ++ratio)
        for (int rev = 0; rev < 2; ++rev)
        {
            auto c = make();
            setParam (*c, F::input, 0.0); setParam (*c, F::output, 0.0); setParam (*c, F::ratio, ratio); setParam (*c, F::revision, rev);
            const double level = -2.0;
            const auto o = runTone (*c, 4000.0, level, fs, 1.5);
            const double expected = F::staticReduction (level - 0.4, ratio, rev == 0);   // crête ≈ 0,4 dB sous l'amplitude (ondulation du détecteur)
            char what[160];
            std::snprintf (what, sizeof what, "Ratio %s %s : réduction %.1f dB (théorie %.1f dB) à 8 dB au-dessus du seuil", std::array<const char*, 5> { "4:1", "8:1", "12:1", "20:1", "ALL" }[(std::size_t) ratio], rev ? "Rev D/E" : "Rev A", o.gr, expected);
            check (std::abs (o.gr - expected) < 0.9, what, o.gr - expected, "dB d'écart");
        }
    {   // ALL écrase plus fort que 20:1 à niveau égal.
        auto a = make(), b = make();
        for (auto* c : { a.get(), b.get() }) { setParam (*c, F::input, 0.0); setParam (*c, F::output, 0.0); setParam (*c, F::revision, 1); }
        setParam (*a, F::ratio, 3.0); setParam (*b, F::ratio, 4.0);
        const double ga = runTone (*a, 4000.0, -2.0, fs, 1.5).gr, gb = runTone (*b, 4000.0, -2.0, fs, 1.5).gr;
        check (gb > ga + 1.0, "ALL (tous boutons) : réduction plus forte que 20:1 au même niveau", gb - ga, "dB de plus");
    }
    // Input pousse la compression, Output ne la change pas.
    {
        auto c = make(); setParam (*c, F::ratio, 1.0);
        setParam (*c, F::input, 0.0); const double g0 = runTone (*c, 4000.0, -14.0, fs).gr;
        setParam (*c, F::input, 12.0); const double g12 = runTone (*c, 4000.0, -14.0, fs).gr;
        setParam (*c, F::input, 12.0); setParam (*c, F::output, -20.0); const double go = runTone (*c, 4000.0, -14.0, fs).gr;
        check (g0 < 0.01 && g12 > 3.0 && std::abs (go - g12) < 0.01, "Input 0 -> 12 dB : la compression apparaît (seuil fixe) ; Output ne la change pas", g12, "dB de GR");
    }
    // Attack en µs : échelon continu, 20 / 100 / 400 / 800 µs à 384 kHz.
    {
        double t[4]; int k = 0;
        for (double us : { 20.0, 100.0, 400.0, 800.0 })
        {
            auto c = make (384000.0);
            setParam (*c, F::input, 0.0); setParam (*c, F::output, 0.0); setParam (*c, F::ratio, 1.0); setParam (*c, F::release, 1100.0); setParam (*c, F::attack, us);
            t[k++] = stepT63 (*c, -2.0, 384000.0);
        }
        char what[200];
        std::snprintf (what, sizeof what, "Attack 20 / 100 / 400 / 800 µs : t63 mesuré %.0f / %.0f / %.0f / %.0f µs", t[0] * 1e6, t[1] * 1e6, t[2] * 1e6, t[3] * 1e6);
        check (std::abs (t[0] - 20.0e-6) < 8.0e-6 && std::abs (t[1] - 100.0e-6) < 20.0e-6 && std::abs (t[2] - 400.0e-6) < 60.0e-6 && std::abs (t[3] - 800.0e-6) < 120.0e-6, what);
    }
    {   // Même attack à 44,1 kHz (échantillons de 22,7 µs) : 800 µs reste ≈ 800 µs.
        auto c = make (44100.0);
        setParam (*c, F::input, 0.0); setParam (*c, F::output, 0.0); setParam (*c, F::ratio, 1.0); setParam (*c, F::attack, 800.0);
        auto d = make (384000.0);
        setParam (*d, F::input, 0.0); setParam (*d, F::output, 0.0); setParam (*d, F::ratio, 1.0); setParam (*d, F::attack, 800.0);
        const double a = stepT63 (*c, -2.0, 44100.0), b = stepT63 (*d, -2.0, 384000.0);
        check (std::abs (a - b) < 0.2 * b + 30.0e-6, "Attack 800 µs : même durée à 44,1 et 384 kHz (±20 %)", (a - b) * 1e6, "µs d'écart");
    }
    // Release : 50 ms vs 1,1 s, retour à 63 % depuis une courte compression.
    {
        double t[2]; int k = 0;
        for (double ms : { 50.0, 1100.0 })
        {
            auto c = make();
            setParam (*c, F::input, 0.0); setParam (*c, F::output, 0.0); setParam (*c, F::ratio, 1.0); setParam (*c, F::release, ms);
            c->reset();
            const double a = std::pow (10.0, -2.0 / 20.0);
            double peakGr = 0.0;
            for (int i = 0; i < 4800; ++i) { double l = a, r = a; c->process (&l, &r, 1, {}); peakGr = std::max (peakGr, c->getGainReductionDb()); }
            int released = 0;
            for (int i = 0; i < (int) (fs * 3); ++i) { double l = 0.0, r = 0.0; c->process (&l, &r, 1, {}); if (c->getGainReductionDb() < 0.37 * peakGr) { released = i; break; } }
            t[k++] = (double) released / fs;
        }
        char what[160];
        std::snprintf (what, sizeof what, "Release 50 ms / 1,1 s : retour à 37 %% en %.0f ms / %.0f ms", t[0] * 1e3, t[1] * 1e3);
        check (t[0] < 0.15 && t[1] > 4.0 * t[0] && t[1] > 0.15, what);
    }
    {   // Dépendant du programme : après une compression longue le retour est plus lent.
        auto run = [&] (double holdSeconds)
        {
            auto c = make();
            setParam (*c, F::input, 0.0); setParam (*c, F::output, 0.0); setParam (*c, F::ratio, 1.0); setParam (*c, F::release, 400.0);
            c->reset();
            const double a = std::pow (10.0, -2.0 / 20.0);
            for (int i = 0; i < (int) (fs * holdSeconds); ++i) { double l = a, r = a; c->process (&l, &r, 1, {}); }
            const double g0 = c->getGainReductionDb();
            for (int i = 0; i < (int) (fs * 0.3); ++i) { double l = 0.0, r = 0.0; c->process (&l, &r, 1, {}); }
            return c->getGainReductionDb() / g0;
        };
        const double brief = run (0.02), longer = run (6.0);
        check (longer > brief + 0.1, "Release dépendant du programme : plus lent après une compression soutenue", longer - brief);
    }
    {   // Rev A plus riche en harmoniques que Rev D/E, et le défaut est propre.
        auto h = [&] (int rev, double inDb)
        {
            auto c = make (384000.0);
            setParam (*c, F::input, inDb); setParam (*c, F::output, -inDb); setParam (*c, F::ratio, 3.0); setParam (*c, F::revision, rev);
            auto o = runTone (*c, 1000.0, -30.0, 384000.0, 0.5);
            return std::pair { toneDb (o.out, 3000.0, 384000.0) - toneDb (o.out, 1000.0, 384000.0), toneDb (o.out, 2000.0, 384000.0) - toneDb (o.out, 1000.0, 384000.0) };
        };
        const auto a = h (0, 24.0), d = h (1, 24.0);
        char what[160];
        std::snprintf (what, sizeof what, "Rev A (H2 %.0f / H3 %.0f dBc) plus colorée que Rev D/E (H2 %.0f / H3 %.0f dBc) à Input +24 dB", a.second, a.first, d.second, d.first);
        check (a.first > d.first + 6.0 && a.second > d.second + 6.0, what);
    }
    {   // SC HPF : un 40 Hz fort ne comprime plus avec 130 Hz.
        auto c = make();
        setParam (*c, F::input, 0.0); setParam (*c, F::output, 0.0); setParam (*c, F::ratio, 1.0);
        const double off = runTone (*c, 40.0, -3.0, fs, 2.0).gr;
        setParam (*c, F::scHpf, 3.0);
        const double on = runTone (*c, 40.0, -3.0, fs, 2.0).gr;
        check (off > 3.0 && on < off - 2.0, "SC HPF 130 Hz : un 40 Hz fort comprime nettement moins", off - on, "dB de moins");
    }
    {   // Repliement : 15 kHz fort comprimé à 44,1 kHz (attack 20 µs).
        auto c = make (44100.0);
        setParam (*c, F::input, 12.0); setParam (*c, F::output, -12.0); setParam (*c, F::ratio, 3.0); setParam (*c, F::attack, 20.0); setParam (*c, F::release, 50.0);
        const auto o = runTone (*c, 5000.0, -12.0, 44100.0, 1.0);
        const double ref = toneDb (o.out, 5000.0, 44100.0);
        double worst = -200.0;
        for (double f : { 900.0, 3300.0, 6700.0, 8800.0, 10000.0, 14400.0 }) worst = std::max (worst, toneDb (o.out, f, 44100.0) - ref);
        check (worst < -50.0, "5 kHz comprimé 20:1, Attack 20 µs, à 44,1 kHz : produits d'intermodulation / repliement < -50 dBc", worst, "dBc");
    }
    {   // 44,1 vs 384 kHz : même réduction statique.
        double g[2]; int k = 0;
        for (double rate : { 44100.0, 384000.0 })
        {
            auto c = make (rate);
            setParam (*c, F::input, 6.0); setParam (*c, F::output, -6.0); setParam (*c, F::ratio, 1.0);
            g[k++] = runTone (*c, 4000.0, -12.0, rate, 1.5).gr;
        }
        check (std::abs (g[0] - g[1]) < 0.4, "Réduction identique à 44,1 et 384 kHz (±0,4 dB)", std::abs (g[0] - g[1]), "dB");
        auto c = make (384000.0);
        Buf L (384000), R; PortableGauss gs (7, 0.5); for (auto& v : L) v = gs(); R = L;
        setParam (*c, F::input, 30.0); setParam (*c, F::ratio, 4.0); runModule (*c, L, R);
        bool finite = true; for (double v : L) finite = finite && std::isfinite (v);
        check (finite, "Bruit fort, Input +30 dB, ALL à 384 kHz : sortie finie");
    }
}


static void testD9x()
{
    std::puts ("\n== 22. D9X (Distressor, 8 rapports + distorsions)");
    using D = AuraD9X;
    check (D::info().params.size() == 10 && D::info().visibleMask (0) == 0x3FFu, "10 paramètres, tous visibles");
    const double fs = 96000.0;
    auto make = [&] (double rate = 96000.0) { auto c = std::make_unique<D>(); c->prepare (rate, 256); setParam (*c, D::input, 0.0); return c; };
    const char* names[8] = { "1:1", "2:1", "3:1", "4:1", "6:1", "10:1 Opto", "20:1", "NUKE" };

    {   // 1:1 Clean, Input 0 / Output 0 : transparent bit pour bit ; avec Input 6 : gain +6 dB exact.
        auto c = make();
        setParam (*c, D::ratio, 0.0);
        const auto o = runTone (*c, 1000.0, -6.0, fs, 0.5);
        check (o.out == o.in && o.gr == 0.0, "1:1 Clean, Input 0 / Output 0 : sortie = entrée bit pour bit");
    }
    // Courbes statiques des 7 rapports (hors 1:1) : 12 dB au-dessus du seuil fixe (-18 dBFS) -> -6 dBFS, tone 4 kHz.
    for (bool brit : { false, true })
        for (int r = 1; r < 8; ++r)
        {
            auto c = make();
            setParam (*c, D::ratio, r); setParam (*c, D::british, brit ? 1.0 : 0.0); setParam (*c, D::attack, 0.05);
            const double level = -6.0;
            const auto o = runTone (*c, 4000.0, level, fs, 1.5);
            const double expected = D::staticReduction (level - 0.4, D::kThresholdDbfs, r, brit);
            char what[160];
            std::snprintf (what, sizeof what, "%s%s : réduction %.1f dB (théorie %.1f dB) à 12 dB au-dessus du seuil", names[r], brit ? " British" : "", o.gr, expected);
            check (std::abs (o.gr - expected) < (r == 7 ? 1.4 : 1.0), what, o.gr - expected, "dB d'écart");
        }
    {   // Les genoux sont différents d'un rapport à l'autre : réduction à 0 dB de dépassement (théorie genou/8·(1-1/r)).
        double g[8];
        for (int r = 1; r < 8; ++r) g[r] = D::staticReduction (D::kThresholdDbfs, D::kThresholdDbfs, r, false);
        check (g[1] > g[6] + 0.2 && g[2] > g[6] && g[7] == 0.0, "Genoux propres à chaque rapport : 2:1 plus doux que 20:1, NUKE sans genou");
    }
    {   // British : plus de réduction, au même réglage.
        auto a = make(), b = make();
        for (auto* c : { a.get(), b.get() }) setParam (*c, D::ratio, 3.0);
        setParam (*b, D::british, 1.0);
        const double ga = runTone (*a, 4000.0, -6.0, fs, 1.5).gr, gb = runTone (*b, 4000.0, -6.0, fs, 1.5).gr;
        check (gb > ga + 1.0, "British : réduction plus forte que le mode normal (4:1, 12 dB au-dessus)", gb - ga, "dB de plus");
        double t[2]; int k = 0;
        for (bool brit : { false, true }) { auto c = make (384000.0); setParam (*c, D::ratio, 3.0); setParam (*c, D::attack, 1.0); setParam (*c, D::british, brit ? 1.0 : 0.0); t[k++] = stepT63 (*c, -6.0, 384000.0); }
        check (t[1] < 0.7 * t[0], "British : attaque plus rapide (≈ ÷ 2)", t[0] / t[1]);
    }
    {   // NUKE : crête de sortie collée au seuil (pas d'anticipation : tolérance 1,5 dB), même sur +24 dB.
        auto c = make();
        setParam (*c, D::ratio, 7.0);
        const auto o = runTone (*c, 1000.0, 0.0, fs, 1.0);
        double peak = 0.0; for (std::size_t i = o.out.size() * 2 / 3; i < o.out.size(); ++i) peak = std::max (peak, std::abs (o.out[i]));
        check (dbOf (peak) < D::kThresholdDbfs + 1.5 && dbOf (peak) > D::kThresholdDbfs - 1.5, "NUKE : sinus 0 dBFS écrêté à la crête du seuil (±1,5 dB)", dbOf (peak) - D::kThresholdDbfs, "dB");
    }
    {   // Opto : release lent (deux voies) vs 4:1 au même réglage ; attaque 0,05 / 30 ms.
        auto relTime = [&] (int ratio)
        {
            auto c = make();
            setParam (*c, D::ratio, ratio); setParam (*c, D::release, 300.0); setParam (*c, D::attack, 1.0);
            c->reset();
            const double a = std::pow (10.0, -6.0 / 20.0); double peakGr = 0.0;
            for (int i = 0; i < (int) (fs * 6.0); ++i) { double l = a, r = a; c->process (&l, &r, 1, {}); peakGr = std::max (peakGr, c->getGainReductionDb()); }
            for (int i = 0; i < (int) (fs * 6); ++i) { double l = 0.0, r = 0.0; c->process (&l, &r, 1, {}); if (c->getGainReductionDb() < 0.37 * peakGr) return (double) i / fs; }
            return 6.0;
        };
        const double opto = relTime (5), plain = relTime (3);
        check (opto > 1.5 * plain, "10:1 Opto : relâchement nettement plus lent que 4:1 (deux voies, dépendant du programme)", opto / plain);
        double t[2]; int k = 0;
        for (double ms : { 0.05, 30.0 }) { auto c = make (384000.0); setParam (*c, D::ratio, 3.0); setParam (*c, D::attack, ms); t[k++] = stepT63 (*c, -6.0, 384000.0); }
        check (t[0] < 0.2e-3 && t[1] > 15.0e-3 && t[1] < 45.0e-3, "Attack 0,05 ms et 30 ms : constantes de temps respectées", t[1] * 1e3, "ms (30 ms)");
    }
    // Distorsions : Dist 2 = H2, Dist 3 = H3, Clean propre ; lampes THD.
    {
        auto meas = [&] (int mode, int ratio, double inDb, double rate = 384000.0, double f = 1000.0)
        {
            auto c = make (rate);
            setParam (*c, D::ratio, ratio); setParam (*c, D::distortion, mode); setParam (*c, D::input, inDb); setParam (*c, D::output, -inDb);
            auto o = runTone (*c, f, -30.0, rate, 0.8);
            struct R { double h2, h3, thd; } r { toneDb (o.out, 2.0 * f, rate) - toneDb (o.out, f, rate), toneDb (o.out, 3.0 * f, rate) - toneDb (o.out, f, rate), c->getMeterAux() };
            return r;
        };
        const auto clean = meas (0, 0, 20.0), d2 = meas (1, 0, 20.0), d3 = meas (2, 0, 20.0);
        char what[200];
        std::snprintf (what, sizeof what, "Clean H2 %.0f / H3 %.0f ; Dist 2 H2 %.0f / H3 %.0f ; Dist 3 H2 %.0f / H3 %.0f dBc (1:1, Input +20 dB)", clean.h2, clean.h3, d2.h2, d2.h3, d3.h2, d3.h3);
        check (clean.h2 < -110.0 && clean.h3 < -110.0 && d2.h2 > d2.h3 + 12.0 && d3.h3 > d3.h2 + 12.0 && d2.h2 > -50.0 && d3.h3 > -55.0, what);
        const auto lo = meas (2, 0, 10.0), hi = meas (2, 0, 24.0);
        check (hi.h3 > lo.h3 + 10.0, "La distorsion croît avec le niveau poussé par Input", hi.h3 - lo.h3, "dB de plus");
        const double thdMeas = 100.0 * std::pow (10.0, d3.h3 / 20.0);
        std::snprintf (what, sizeof what, "Lampes THD : estimation %.2f %% pour H3 %.0f dBc mesuré (%.2f %%)", d3.thd, d3.h3, thdMeas);
        check (d3.thd > 0.6 * thdMeas && d3.thd < 1.5 * thdMeas, what);
        const auto comp = meas (2, 3, 20.0);
        check (clean.thd == 0.0 && comp.thd > 0.0, "Clean : lampes THD éteintes (0 %) ; Dist 3 : allumées", comp.thd);
    }
    {   // Repliement 44,1 kHz : 15 kHz très fort en Dist 3.
        auto c = make (44100.0);
        setParam (*c, D::ratio, 3.0); setParam (*c, D::distortion, 2.0); setParam (*c, D::input, 24.0); setParam (*c, D::output, -24.0);
        const auto o = runTone (*c, 15000.0, -20.0, 44100.0, 1.0);
        double worst = -200.0; const double ref = toneDb (o.out, 15000.0, 44100.0);
        for (double f : { 900.0, 3000.0, 5100.0, 7000.0, 14100.0 }) worst = std::max (worst, toneDb (o.out, f, 44100.0) - ref);
        check (worst < -65.0, "15 kHz Dist 3 + compression à 44,1 kHz : repliements < -65 dBc", worst, "dBc");
    }
    {   // Detector HPF et Band Emphasis : agissent sur le détecteur seulement.
        auto c = make();
        setParam (*c, D::ratio, 3.0);
        const double off = runTone (*c, 40.0, -6.0, fs, 2.0).gr;
        setParam (*c, D::detHpf, 3.0);
        const double on = runTone (*c, 40.0, -6.0, fs, 2.0).gr;
        check (off > 4.0 && on < off - 3.0, "Detector HPF 240 Hz : un 40 Hz fort ne comprime presque plus", off - on, "dB de moins");
        auto e = make();
        setParam (*e, D::ratio, 3.0);
        const double flat = runTone (*e, 6000.0, -22.0, fs, 1.2).gr;
        setParam (*e, D::bandEmphasis, 12.0);
        const auto o = runTone (*e, 6000.0, -22.0, fs, 1.2);
        check (flat < 0.1 && o.gr > 3.0, "Band Emphasis +12 dB (6 kHz) : un 6 kHz sous le seuil déclenche la compression", o.gr, "dB de GR");
    }
    {   // Auto Focus : réduction moyenne ≈ cible, à deux niveaux d'entrée.
        for (double inLevel : { -26.0, -14.0 })
        {
            auto c = make (48000.0);
            setParam (*c, D::ratio, 3.0); setParam (*c, D::autoFocus, 2.0);   // 6 dB
            c->reset();
            const double rate = 48000.0; const int total = (int) (rate * 40.0);
            double sum = 0.0; int cnt = 0;
            PortableGauss g (3, 1.0);
            for (int blk = 0; blk < total / 256; ++blk)
            {
                Buf l (256), r;
                for (int i = 0; i < 256; ++i)
                {
                    const double t = (double) (blk * 256 + i) / rate;
                    const double env = 0.55 + 0.45 * std::sin (dsp::kTwoPi * 3.0 * t);   // « syllabes » à 3 Hz
                    l[(std::size_t) i] = std::pow (10.0, inLevel / 20.0) * env * std::sin (dsp::kTwoPi * 700.0 * t) * 1.0;
                }
                r = l;
                c->process (l.data(), r.data(), 256, {});
                if (blk * 256 > total / 2) { sum += c->getGainReductionDb(); ++cnt; }
            }
            char what[160];
            std::snprintf (what, sizeof what, "Auto Focus 6 dB, entrée %.0f dBFS : réduction moyenne %.1f dB", inLevel, sum / cnt);
            check (std::abs (sum / cnt - 6.0) < 2.0, what, sum / cnt - 6.0, "dB d'écart");
        }
    }
    {   // 44,1 vs 384 kHz : même réduction ; bruit fort fini.
        double g[2]; int k = 0;
        for (double rate : { 44100.0, 384000.0 }) { auto c = make (rate); setParam (*c, D::ratio, 3.0); setParam (*c, D::distortion, 1.0); g[k++] = runTone (*c, 4000.0, -10.0, rate, 1.5).gr; }
        check (std::abs (g[0] - g[1]) < 0.4, "Réduction identique à 44,1 et 384 kHz (±0,4 dB)", std::abs (g[0] - g[1]), "dB");
        auto c = make (384000.0);
        Buf L (384000), R; PortableGauss gs (11, 0.5); for (auto& v : L) v = gs(); R = L;
        setParam (*c, D::input, 30.0); setParam (*c, D::ratio, 7.0); setParam (*c, D::distortion, 2.0); setParam (*c, D::british, 1.0); runModule (*c, L, R);
        bool finite = true; for (double v : L) finite = finite && std::isfinite (v);
        check (finite, "Bruit fort, NUKE + Dist 3 + British à 384 kHz : sortie finie");
    }
}

/** Salve : `onMs` de sinus à `f` précédée de 100 ms de silence, suivie de `tailMs` de silence. */
static Buf burst (double f, double dbfs, double rate, double onMs, double tailMs)
{
    Buf x ((std::size_t) (rate * (0.1 + (onMs + tailMs) * 0.001)), 0.0);
    const std::size_t a = (std::size_t) (rate * 0.1), n = (std::size_t) (rate * onMs * 0.001);
    for (std::size_t i = 0; i < n; ++i) x[a + i] = std::pow (10.0, dbfs / 20.0) * std::sin (dsp::kTwoPi * f * (double) i / rate);
    return x;
}
/** Note percussive : attaque franche, décroissance exponentielle (constante tauMs), précédée de 100 ms de silence. */
static Buf note (double f, double dbfs, double rate, double tauMs, double lenMs = 500.0)
{
    Buf x ((std::size_t) (rate * (0.1 + lenMs * 0.001)), 0.0);
    const std::size_t a = (std::size_t) (rate * 0.1);
    for (std::size_t i = 0; a + i < x.size(); ++i) { const double t = (double) i / rate; x[a + i] = std::pow (10.0, dbfs / 20.0) * std::exp (-t / (tauMs * 0.001)) * std::sin (dsp::kTwoPi * f * t); }
    return x;
}
static double windowDb (const Buf& x, double rate, double t0Ms, double t1Ms)
{
    return dbOf (rmsOf (x, (std::size_t) (rate * (0.1 + t0Ms * 0.001)), (std::size_t) (rate * (0.1 + t1Ms * 0.001))));
}

static void testSculpt()
{
    std::puts ("\n== 23. AuraSculpt (transitoires / dynamique)");
    using S = AuraSculpt;
    check (S::info().params.size() == 7 && S::info().visibleMask (0) == 0x7Fu, "7 paramètres, tous visibles");
    const double fs = 96000.0;
    auto make = [&] (double rate = 96000.0) { auto c = std::make_unique<S>(); c->prepare (rate, 256); return c; };
    {
        auto c = make();
        c->reset();
        Buf L = sine (48000, 1000.0, fs, 0.5), R = L; const Buf in = L;
        runModule (*c, L, R);
        check (L == in, "Tout à 0 (Ceiling Off) : sortie = entrée bit pour bit");
    }
    {   // Régime établi : gain 0 dB quels que soient Attack / Sustain.
        auto c = make();
        setParam (*c, S::attack, 12.0); setParam (*c, S::sustain, -12.0);
        const auto o = runTone (*c, 1000.0, -12.0, fs, 2.0);
        const double g = toneDb (o.out, 1000.0, fs) - toneDb (o.in, 1000.0, fs);
        check (std::abs (g) < 0.1, "Sinus continu : gain 0 dB même avec Attack +12 / Sustain -12 (régime établi intact)", g, "dB");
    }
    {   // Attack : l'attaque est renforcée, la queue ne bouge pas.
        auto run = [&] (double att, double sus, double dbfs)
        {
            auto c = make(); setParam (*c, S::attack, att); setParam (*c, S::sustain, sus);
            Buf x = note (200.0, dbfs, fs, 120.0), r = x; const Buf in = x;
            runModule (*c, x, r);
            struct O { double early, late, tail; } o { windowDb (x, fs, 0.0, 6.0) - windowDb (in, fs, 0.0, 6.0), windowDb (x, fs, 40.0, 60.0) - windowDb (in, fs, 40.0, 60.0), windowDb (x, fs, 150.0, 200.0) - windowDb (in, fs, 150.0, 200.0) };
            return o;
        };
        const auto base = run (0.0, 0.0, -12.0), up = run (9.0, 0.0, -12.0), dn = run (-9.0, 0.0, -12.0);
        char what[200];
        std::snprintf (what, sizeof what, "Attack +9 dB : attaque %+.1f dB, corps %+.1f dB ; Attack -9 dB : attaque %+.1f dB", up.early - base.early, up.late - base.late, dn.early - base.early);
        check (up.early - base.early > 3.0 && dn.early - base.early < -3.0 && std::abs (up.late - base.late) < 0.5, what);
        const auto su = run (0.0, -12.0, -12.0), sd = run (0.0, 6.0, -12.0);
        std::snprintf (what, sizeof what, "Sustain -12 dB : queue %+.1f dB, attaque %+.1f dB ; Sustain +6 dB : queue %+.1f dB", su.tail - base.tail, su.early - base.early, sd.tail - base.tail);
        check (su.tail - base.tail < -3.0 && std::abs (su.early - base.early) < 1.5 && sd.tail - base.tail > 0.0, what);
        const auto lo = run (9.0, 0.0, -32.0);
        check (std::abs ((lo.early - base.early) - (up.early - base.early)) < 1.5, "Indépendant du niveau : même renforcement à -32 dBFS qu'à -12 dBFS (±1,5 dB)", (lo.early) - (up.early), "dB");
    }
    {   // Focus : seules les transitoires de la bande visée bougent.
        auto run = [&] (double f)
        {
            auto c = make(); setParam (*c, S::attack, 12.0); setParam (*c, S::focus, 6000.0); setParam (*c, S::focusAmount, 100.0);
            Buf x = burst (f, -12.0, fs, 100.0, 100.0), r = x; const Buf in = x;
            runModule (*c, x, r);
            return windowDb (x, fs, 0.0, 6.0) - windowDb (in, fs, 0.0, 6.0);
        };
        const double hi = run (6000.0), lo = run (200.0);
        check (hi > 3.0 && std::abs (lo) < 0.6, "Focus 6 kHz à 100 % : une salve à 6 kHz est renforcée, une salve à 200 Hz intacte", hi - lo, "dB d'écart");
    }
    {   // Ceiling : plafond doux, identité sous le genou, pas de repliement.
        auto c = make();
        setParam (*c, S::ceiling, -6.0);
        const auto quiet = runTone (*c, 1000.0, -12.0, fs, 0.4);
        check (quiet.out == quiet.in, "Ceiling -6 : un signal sous le genou (-9 dBFS) passe bit pour bit");
        const auto loud = runTone (*c, 1000.0, 6.0, fs, 0.4);
        double peak = 0.0; for (double v : loud.out) peak = std::max (peak, std::abs (v));
        check (dbOf (peak) <= -4.9 && dbOf (peak) > -6.5, "Ceiling -6 : une crête à +6 dBFS (+12 dB de surcharge) reste ≤ -4,9 dBFS (plafond doux : jusqu'à +1 dB de dépassement)", dbOf (peak), "dBFS");
        auto d = make (44100.0);
        setParam (*d, S::ceiling, -6.0);
        const auto o = runTone (*d, 9000.0, -1.0, 44100.0, 1.0);
        double worst = -200.0; const double ref = toneDb (o.out, 9000.0, 44100.0);
        for (double f : { 900.0, 4500.0, 13500.0, 18000.0 }) worst = std::max (worst, toneDb (o.out, f, 44100.0) - ref);
        check (worst < -40.0, "9 kHz plafonné à 44,1 kHz : repliements < -40 dBc (ADAA du 1er ordre)", worst, "dBc");
    }
    {   // 44,1 / 384 kHz : même effet ; bruit fort fini.
        double g[2]; int k = 0;
        for (double rate : { 44100.0, 384000.0 })
        {
            auto c = make (rate); setParam (*c, S::attack, 9.0);
            Buf x = burst (200.0, -12.0, rate, 150.0, 100.0), r = x; const Buf in = x;
            runModule (*c, x, r, 256);
            g[k++] = windowDb (x, rate, 0.0, 6.0) - windowDb (in, rate, 0.0, 6.0);
        }
        check (std::abs (g[0] - g[1]) < 1.0, "Attack +9 dB : même renforcement à 44,1 et 384 kHz (±1 dB)", std::abs (g[0] - g[1]), "dB");
        auto c = make (384000.0);
        Buf L (384000), R; PortableGauss gs (13, 0.5); for (auto& v : L) v = gs(); R = L;
        setParam (*c, S::attack, 12.0); setParam (*c, S::sustain, 12.0); setParam (*c, S::ceiling, -3.0); runModule (*c, L, R);
        bool finite = true; for (double v : L) finite = finite && std::isfinite (v);
        check (finite, "Bruit fort à 384 kHz : sortie finie");
    }
}

static void testTone()
{
    std::puts ("\n== 24. AuraTone (Body / Air / Tilt / harmoniques)");
    using T = AuraTone;
    check (T::info().params.size() == 6 && T::info().visibleMask (0) == 0x3Fu, "6 paramètres, tous visibles");
    const double fs = 96000.0;
    auto make = [&] (double rate = 96000.0) { auto c = std::make_unique<T>(); c->prepare (rate, 256); return c; };
    auto gainAt = [&] (T& c, double f, double rate = 96000.0)
    {
        auto o = runTone (c, f, -30.0, rate, f < 100.0 ? 2.5 : 0.8);
        return toneDb (o.out, f, rate) - toneDb (o.in, f, rate);
    };
    {
        auto c = make(); c->reset();
        Buf L = sine (48000, 1000.0, fs, 0.5), R = L; const Buf in = L;
        runModule (*c, L, R);
        check (L == in, "Tout à 0 : sortie = entrée bit pour bit");
    }
    {
        auto c = make(); setParam (*c, T::body, 6.0);
        const double g180 = gainAt (*c, 180.0), g5k = gainAt (*c, 5000.0), g20k = gainAt (*c, 20000.0);
        char what[160];
        std::snprintf (what, sizeof what, "Body +6 dB : %+.2f dB à 180 Hz, %+.2f dB à 5 kHz, %+.2f dB à 20 kHz", g180, g5k, g20k);
        check (std::abs (g180 - 6.0) < 0.4 && std::abs (g5k) < 0.5 && std::abs (g20k) < 0.2, what);
        auto d = make(); setParam (*d, T::body, -6.0);
        check (std::abs (gainAt (*d, 180.0) + 6.0) < 0.4, "Body -6 dB : -6 dB à 180 Hz", gainAt (*d, 180.0), "dB");
    }
    {
        auto c = make(); setParam (*c, T::air, 6.0);
        const double g1k = gainAt (*c, 1000.0), g11k = gainAt (*c, 11000.0), g20k = gainAt (*c, 20000.0);
        char what[160];
        std::snprintf (what, sizeof what, "Air +6 dB : %+.2f dB à 1 kHz, %+.2f dB à 11 kHz, %+.2f dB à 20 kHz", g1k, g11k, g20k);
        check (std::abs (g1k) < 0.3 && g11k > 2.0 && g11k < 4.0 && g20k > 5.0, what);
        auto d = make (44100.0); setParam (*d, T::air, 6.0);
        const double n = gainAt (*d, 18000.0, 44100.0);
        check (n > 4.0 && n < 6.5, "Air +6 dB à 44,1 kHz : +4 à +6,5 dB à 18 kHz (pas de dérive près de Nyquist)", n, "dB");
        auto e = make (384000.0); setParam (*e, T::air, 6.0);
        check (std::abs (gainAt (*e, 11000.0, 384000.0) - g11k) < 0.2, "Air : même réponse à 384 kHz qu'à 96 kHz (11 kHz)", gainAt (*e, 11000.0, 384000.0) - g11k, "dB");
    }
    {
        auto c = make(); setParam (*c, T::tilt, 4.0);
        const double lo = gainAt (*c, 40.0), mid = gainAt (*c, 650.0), hi = gainAt (*c, 16000.0);
        char what[160];
        std::snprintf (what, sizeof what, "Tilt +4 dB : %+.2f dB à 40 Hz, %+.2f dB à 650 Hz, %+.2f dB à 16 kHz", lo, mid, hi);
        check (std::abs (lo + 2.0) < 0.5 && std::abs (mid) < 0.4 && std::abs (hi - 2.0) < 0.5, what);
    }
    {   // Doseur d'harmoniques.
        auto harm = [&] (double even, double odd, double rate = 384000.0, double f = 1000.0)
        {
            auto c = make (rate); setParam (*c, T::even, even); setParam (*c, T::odd, odd);
            auto o = runTone (*c, f, -12.0, rate, 0.8);
            struct R { double h2, h3; } r { toneDb (o.out, 2.0 * f, rate) - toneDb (o.out, f, rate), toneDb (o.out, 3.0 * f, rate) - toneDb (o.out, f, rate) };
            return r;
        };
        const auto zero = harm (0.0, 0.0), ev = harm (100.0, 0.0), od = harm (0.0, 100.0), half = harm (50.0, 0.0);
        char what[200];
        std::snprintf (what, sizeof what, "Even 100 %% : H2 %.0f / H3 %.0f dBc ; Odd 100 %% : H2 %.0f / H3 %.0f dBc ; zéro : H2 %.0f dBc", ev.h2, ev.h3, od.h2, od.h3, zero.h2);
        check (zero.h2 < -140.0 && ev.h2 > -33.0 && ev.h2 < -22.0 && ev.h3 < ev.h2 - 20.0 && od.h3 > -40.0 && od.h3 < -28.0 && od.h2 < od.h3 - 20.0, what);
        check (std::abs ((ev.h2 - half.h2) - 6.0) < 1.0, "Even 50 % : H2 6 dB sous Even 100 % (dose linéaire)", ev.h2 - half.h2, "dB");
        const auto lo = harm (100.0, 100.0, 96000.0), dxd = harm (100.0, 100.0, 384000.0);
        check (std::abs (lo.h2 - dxd.h2) < 0.6 && std::abs (lo.h3 - dxd.h3) < 0.6, "Harmoniques identiques à 96 et 384 kHz (±0,6 dB)", lo.h2 - dxd.h2, "dB");
        auto c = make (44100.0); setParam (*c, T::even, 100.0); setParam (*c, T::odd, 100.0);
        const auto o = runTone (*c, 15000.0, -3.0, 44100.0, 1.0);
        double worst = -200.0; const double ref = toneDb (o.out, 15000.0, 44100.0);
        for (double f : { 900.0, 3000.0, 5100.0, 14100.0 }) worst = std::max (worst, toneDb (o.out, f, 44100.0) - ref);
        check (worst < -80.0, "15 kHz, doseurs à 100 % à 44,1 kHz : repliements < -80 dBc", worst, "dBc");
    }
    {   // Stabilité DXD : 20 Hz à 384 kHz, bruit fort, réglages extrêmes.
        auto c = make (384000.0);
        setParam (*c, T::body, 12.0); setParam (*c, T::air, 12.0); setParam (*c, T::tilt, 6.0); setParam (*c, T::even, 100.0); setParam (*c, T::odd, 100.0);
        Buf L (384000), R; PortableGauss gs (17, 0.3); for (auto& v : L) v = gs(); R = L;
        runModule (*c, L, R);
        bool finite = true; for (double v : L) finite = finite && std::isfinite (v);
        auto d = make (384000.0); setParam (*d, T::body, 12.0);
        check (finite && std::abs (gainAt (*d, 20.0, 384000.0)) < 3.0, "Réglages extrêmes à 384 kHz : sortie finie ; Body +12 dB à 20 Hz : stable (< +3 dB)", gainAt (*d, 20.0, 384000.0), "dB");
    }
}


static void testRobust()
{
    std::puts ("\n== 25. Robustesse : signaux flottants très forts, tous modules, 44,1 et 384 kHz");
    for (double rate : { 44100.0, 384000.0 })
    {
        bool ok = true; double worst = 0.0; std::string bad;
        for (int t = 1; t < kNumModuleTypes; ++t)
        {
            auto m = createModule ((ModuleType) t);
            m->prepare (rate, 256);
            Buf L = sine ((std::size_t) (rate * 0.5), 997.0, rate, 1000.0), R = L;   // +60 dBFS
            runModule (*m, L, R);
            double peak = 0.0;
            for (double v : L) { if (! std::isfinite (v)) { ok = false; bad = m->getInfo().name; } peak = std::max (peak, std::abs (v)); }
            worst = std::max (worst, peak);
            if (peak > 1.0e6) { ok = false; bad = m->getInfo().name; }
        }
        check (ok, rate < 100000.0 ? "+60 dBFS à 44,1 kHz : tous les modules finis, crête < 1e6 " + bad : "+60 dBFS à 384 kHz : tous les modules finis, crête < 1e6 " + bad, dbOf (worst), "dBFS (pire crête)");
    }
}

int main (int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : "";
    const std::pair<const char*, void (*)()> tests[] = {
        { "svf", testSvf }, { "eq", testEq }, { "comp", testComp }, { "duck", testDuck }, { "clip", testClip },
        { "limit", testLimit }, { "delay", testDelay }, { "verb", testVerb }, { "engine", testEngine },
        { "stress", testStress }, { "move", testMoveSlot }, { "w9b", testW9b }, { "focus", testFocus }, { "l9a", testL9a }, { "rackmods", testRackModules }, { "e9ds", testE9ds }, { "loudness", testLoudness }, { "v9c3", testV9c3 }, { "v9p2", testV9p2 }, { "lookahead", testLookahead }, { "autogain", testAutoGain }, { "utility", testStereoUtility }, { "f976", testF976 }, { "robust", testRobust }, { "d9x", testD9x }, { "sculpt", testSculpt }, { "tone", testTone },
    };
    std::puts ("AuraRack — banc d'essai DSP (64 bits)");
    for (auto& [name, fn] : tests)
        if (filter.empty() || filter == name) fn();

    std::printf ("\n%d / %d vérifications réussies\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
