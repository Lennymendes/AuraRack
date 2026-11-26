// AuraRack — NextDSP · V9P2
// Préampli micro double canal, inspiré d'un préampli discret des années 70 (famille Vertigo VSP-2), version DXD
// (352,8 / 384 kHz). Conception AuraRack : constantes calibrées par mesure, pas par comparaison avec le matériel.
//
//   Contrôles : Gain (12 … 72 dB par pas de 6) + Fine (0 … +6 dB), HPF (Off / 80 / 120 Hz, 12 dB/oct), Pad −20 dB,
//   Phase Ø (Off / L / R / L+R), Input (Mic / Inst-DI), Link (Dual / Linked) + R Offset. Chaque contrôle agit sur le son
//   (le commutateur 48V, purement indicatif, a été supprimé : un plugin n'alimente pas de micro).
//
//   ÉTALONNAGE : le signal reçu est DÉJÀ numérisé. Gain net = Gain + Fine − 36 dB − Pad : 36 dB = gain unité, 12 dB = −24 dB net,
//   72 dB = +36 dB net. 0 dBu = −18 dBFS (+18 dBu = 0 dBFS). Marge : +24 dBu (+6 dBFS) ; le voyant passe au rouge à +19 dBu.
//
//   Chaîne : Pad / Gain (lissés 20 ms) ─► entrée (Mic : transformateur type JT-16 1:2 ; Inst/DI : directement sur l'étage)
//   ─► étage discret « Twin OPA » (harmoniques 2 et 3 additifs) ─► HPF (fondu 10 ms) ─► polarité (rampe 5 ms) ─► sortie
//   symétrique propre (type THAT 1646 : linéaire jusqu'à +21 dBu, genou doux jusqu'à +24 dBu, ADAA sous 176,4 kHz).
//
//   - Mic : transformateur 1:2 très linéaire (+6 dB passifs, déphasage faible, capacité parasite faible, forte tenue en niveau) ;
//     il prend 6 dB du gain, l'étage Twin OPA fournit le reste à pleine charge : harmoniques 2 et 3 plus riches, son ouvert, tranchant.
//     Le transformateur ajoute seulement un pôle HF à 69 kHz (−1 dB à 35 kHz) et une non-linéarité de noyau minuscule sous 60 Hz,
//     qui ne devient mesurable qu'au-dessus de +14 dBu.
//   - Inst/DI : pas de transformateur ; l'étage travaille avec plus de contre-réaction (≈ 0,3 × les harmoniques du trajet Mic)
//     et la bande passante n'a aucune limitation HF : plus neutre, plus lisse. Différence mesurable à tout réglage.
//   - Bande passante : 10 Hz … 35 kHz à ±1 dB (pôle à 4,5 Hz contre le continu ; pôle HF à 69 kHz dès 192 kHz d'échantillonnage).
//   - Harmoniques : termes additifs calculés sur le signal passe-bas (min(20 kHz, 0,45·fs/3)), ils ne se replient donc jamais.
//   - Dual : chaque canal a son gain (R = L + R Offset) ; Linked : les deux canaux ont le même gain.
//   - Voyant : crête à l'entrée de l'étage de sortie ; getMeterAux = niveau de crête en dBu à décroissance lente. Latence 0.

#pragma once

#include "CompCommon.h"

namespace aura
{
class AuraV9P2 final : public AuraModuleDSP
{
public:
    enum Param { gain, fine, hpf, pad, phase, input, link, rOffset, numParams };

    static constexpr double kUnityGainDb = 36.0, kDbuToDbfs = -18.0, kCeilingDbu = 24.0, kClipLampDbu = 19.0, kLinearDbu = 21.0;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* gains[]    = { "12 dB", "18 dB", "24 dB", "30 dB", "36 dB", "42 dB", "48 dB", "54 dB", "60 dB", "66 dB", "72 dB" };
        static constexpr const char* hpfs[]     = { "Off", "80 Hz", "120 Hz" };
        static constexpr const char* pads[]     = { "Off", "-20 dB" };
        static constexpr const char* phases[]   = { "Off", "L", "R", "L+R" };
        static constexpr const char* inputs[]   = { "Mic", "Inst/DI" };
        static constexpr const char* links[]    = { "Dual", "Linked" };
        static constexpr ParamSpec specs[] = {
            { "Gain",     "",   0.0, 10.0, 4.0, S::choice, gains, 11 },
            { "Fine",     "dB", 0.0, 6.0,  0.0, S::linear },
            { "HPF",      "",   0.0, 2.0,  0.0, S::choice, hpfs, 3 },
            { "Pad",      "",   0.0, 1.0,  0.0, S::choice, pads, 2 },
            { "Phase",    "",   0.0, 3.0,  0.0, S::choice, phases, 4 },
            { "Input",    "",   0.0, 1.0,  0.0, S::choice, inputs, 2 },
            { "Link",     "",   0.0, 1.0,  1.0, S::choice, links, 2 },
            { "R Offset", "dB", -6.0, 6.0, 0.0, S::linear },
        };
        static const ModuleInfo i { ModuleType::v9p2, "V9P2", "V9P2", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    /** Gain net (dB) d'un canal pour des réglages donnés — utilisé par les tests et la façade. */
    static double netGainDb (int gainIndex, double fineDb, bool padOn, double offsetDb = 0.0) noexcept
    {
        return 12.0 + 6.0 * (double) std::clamp (gainIndex, 0, 10) + fineDb - kUnityGainDb - (padOn ? 20.0 : 0.0) + offsetDb;
    }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case gain:    gainIndex = std::clamp ((int) std::lround (v), 0, 10); updateGains(); break;
            case fine:    fineDb = v; updateGains(); break;
            case hpf:     hpfIndex = std::clamp ((int) std::lround (v), 0, 2);
                          if (hpfIndex > 0) hp.setup (dsp::TptSvf::Type::highPass, hpfIndex == 1 ? 80.0 : 120.0, 0.70710678118654752, 0.0, sampleRate);
                          hpAmount.setTarget (hpfIndex > 0 ? 1.0 : 0.0); break;
            case pad:     padOn = v >= 0.5; updateGains(); break;
            case phase:   { const int p = std::clamp ((int) std::lround (v), 0, 3);
                            polL.setTarget ((p & 1) != 0 && p != 2 ? -1.0 : 1.0);   // 1 = L, 3 = L+R
                            polR.setTarget (p >= 2 ? -1.0 : 1.0); break; }         // 2 = R, 3 = L+R
            case input:   diAmount.setTarget (v >= 0.5 ? 1.0 : 0.0); break;
            case link:    linked = v >= 0.5; updateGains(); break;
            case rOffset: offsetDb = v; updateGains(); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        for (auto* s : { &gainL, &gainR, &hpAmount, &diAmount, &polL, &polR }) s->reset (s->getTarget());
        hp.reset(); coreLp.reset(); distLp1.reset(); distLp2.reset();
        for (int c = 0; c < 2; ++c) { dcIn[c] = dcSq[c] = hfState[c] = 0.0; xPrev[c] = 0.0; }
        primPrev[0] = primPrev[1] = 0.0;
        peakAux = -120.0;
        statusPeak = 0.0;
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            double x[2] = { L[i] * dsp::dbToGain (gainL.next()), R[i] * dsp::dbToGain (gainR.next()) };
            const double di = diAmount.next(), mic = 1.0 - di;

            // Entrée : pôle 4,5 Hz (continu) ; Mic : pôle HF du transformateur + noyau (graves seulement, tout petit).
            double lo[2];
            for (int c = 0; c < 2; ++c)
            {
                dcIn[c] += dcCoefIn * (x[c] - dcIn[c]);
                x[c] -= dcIn[c];
                lo[c] = coreLp.processSample (x[c], c);
            }
            if (mic > 0.0)
                for (int c = 0; c < 2; ++c)
                {
                    if (hfActive) { hfState[c] += hfCoef * (x[c] - hfState[c]); x[c] += mic * (hfState[c] - x[c]); }
                    x[c] += mic * kCoreK * kCeilingLin * std::pow (lo[c] / kCeilingLin, 3.0) * -1.0;
                }

            // Étage Twin OPA : harmoniques additifs ∝ niveau (alias-free : calculés sur le signal passe-bas).
            const double d = opaDepth * (mic + kDiDepth * di);
            if (d > 1.0e-9)
            {
                const double l0 = distLp2.processSample (distLp1.processSample (x[0], 0), 0), l1 = distLp2.processSample (distLp1.processSample (x[1], 1), 1);
                const double u[2] = { l0 / kCeilingLin, l1 / kCeilingLin };
                for (int c = 0; c < 2; ++c)
                {
                    const double sq = u[c] * u[c];
                    dcSq[c] += dcCoefIn * (sq - dcSq[c]);
                    x[c] += d * kCeilingLin * (-kC3 * sq * u[c] + kC2 * (sq - dcSq[c]));
                }
            }

            const double hpa = hpAmount.next();
            if (hpa > 0.0)
            {
                const double hl = hp.processSample (x[0], 0), hr = hp.processSample (x[1], 1);
                x[0] += hpa * (hl - x[0]);
                x[1] += hpa * (hr - x[1]);
            }

            x[0] *= polL.next();
            x[1] *= polR.next();

            statusPeak = std::max (statusPeak, std::max (std::abs (x[0]), std::abs (x[1])));
            L[i] = output (x[0], 0);
            R[i] = output (x[1], 1);
        }
        // Niveau de crête en dBu, décroissance lente (≈ 20 dB/s) pour l'affichage.
        const double peakDbu = statusPeak > 1.0e-9 ? dsp::gainToDb (statusPeak) - kDbuToDbfs : -120.0;
        const double fall = peakAux - 20.0 * (double) n / sampleRate;
        peakAux = std::max (peakDbu, fall);
    }

    double takeStatusPeak() noexcept override
    {
        const double p = statusPeak;
        statusPeak = 0.0;
        return p;
    }

    double getMeterAux() const noexcept override { return peakAux; }

private:
    static constexpr double kCeilingLin = 1.9952623149688795;   // +24 dBu en pleine échelle (+6 dBFS)
    static constexpr double kKneeLin = 1.4125375446227544;   // +21 dBu = +3 dBFS
    static constexpr double kCoreK = 0.0030;   // noyau du transformateur : H3 ≈ −110 dBc à +14 dBu sous 60 Hz
    static constexpr double kC3 = 0.020, kC2 = 0.0040, kDiDepth = 0.30;

    void prepareInternal() override
    {
        for (auto* s : { &gainL, &gainR }) s->prepare (sampleRate, 20.0);
        hpAmount.prepare (sampleRate, 10.0);
        diAmount.prepare (sampleRate, 20.0);
        for (auto* s : { &polL, &polR }) s->prepare (sampleRate, 5.0);
        coreLp.setup (dsp::TptSvf::Type::lowPass, 60.0, 0.70710678118654752, 0.0, sampleRate);
        hp.setup (dsp::TptSvf::Type::highPass, hpfIndex == 2 ? 120.0 : 80.0, 0.70710678118654752, 0.0, sampleRate);
        const double fc = std::min (20000.0, 0.45 * sampleRate / 3.0);
        distLp1.setup (dsp::TptSvf::Type::lowPass, fc, 0.70710678118654752, 0.0, sampleRate);
        distLp2.setup (dsp::TptSvf::Type::lowPass, fc, 0.70710678118654752, 0.0, sampleRate);
        dcCoefIn = compdetail::alpha (1.0 / (2.0 * dsp::kPi * 4.5), sampleRate);
        // Pôle HF du transformateur (−1 dB à 35 kHz → fc = 68,9 kHz) : n'existe qu'au-delà de ≈ 153 kHz d'échantillonnage.
        const double fcHf = 35000.0 / std::sqrt (std::pow (10.0, 0.1) - 1.0);
        hfActive = fcHf < 0.45 * sampleRate;
        hfCoef = hfActive ? compdetail::alpha (1.0 / (2.0 * dsp::kPi * fcHf), sampleRate) : 0.0;
        polL.reset (1.0); polR.reset (1.0);
        for (int c = 0; c < 2; ++c) { xPrev[c] = 0.0; primPrev[c] = 0.0; }
        updateGains();
    }

    /** Sortie symétrique propre : identité jusqu'à +21 dBu, genou tanh jusqu'à +24 dBu (C¹) ; ADAA du 1er ordre sous 176,4 kHz. */
    double output (double x, int c) noexcept
    {
        const double w = kCeilingLin - kKneeLin;
        auto f = [&] (double v) { const double a = std::abs (v); return a <= kKneeLin ? v : std::copysign (kKneeLin + w * std::tanh ((a - kKneeLin) / w), v); };
        if (sampleRate >= 176400.0 || std::abs (x) < 0.5 * kKneeLin && std::abs (xPrev[c]) < 0.5 * kKneeLin)
        {
            xPrev[c] = x; primPrev[c] = prim (x, w);
            return f (x);
        }
        const double pr = prim (x, w), dx = x - xPrev[c];
        const double y = std::abs (dx) > 1.0e-9 ? (pr - primPrev[c]) / dx : f (0.5 * (x + xPrev[c]));
        xPrev[c] = x; primPrev[c] = pr;
        return y;
    }
    double prim (double x, double w) const noexcept
    {
        const double a = std::abs (x);
        if (a <= kKneeLin) return 0.5 * a * a;
        return 0.5 * kKneeLin * kKneeLin + kKneeLin * (a - kKneeLin) + w * w * compdetail::logCosh ((a - kKneeLin) / w);
    }

    void updateGains() noexcept
    {
        const double l = netGainDb (gainIndex, fineDb, padOn);
        gainL.setTarget (l);
        gainR.setTarget (linked ? l : l + offsetDb);
        // Part de gain fournie par l'étage Twin OPA (plus de gain = plus d'harmoniques) : 0,4 … 1,0 de 12 à 72 dB.
        opaDepth = 0.4 + 0.6 * ((double) gainIndex * 6.0 + fineDb) / 60.0;
    }

    int gainIndex = 4, hpfIndex = 0;
    double fineDb = 0.0, offsetDb = 0.0, peakAux = -120.0, statusPeak = 0.0;
    bool padOn = false, linked = true;

    dsp::OnePoleSmoother gainL, gainR, hpAmount, diAmount, polL, polR;
    dsp::TptSvf hp, coreLp, distLp1, distLp2;
    double dcIn[2] { 0, 0 }, dcSq[2] { 0, 0 }, hfState[2] { 0, 0 }, xPrev[2] { 0, 0 }, primPrev[2] { 0, 0 };
    double dcCoefIn = 0.0, hfCoef = 0.0, opaDepth = 0.6;
    bool hfActive = false;
};

} // namespace aura
