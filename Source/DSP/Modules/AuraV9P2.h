// AuraRack — NextDSP · V9P2
// Préampli micro double canal, inspiré d'un préampli discret des années 70 (famille Vertigo VSP-2), version DXD
// (352,8 / 384 kHz). Conception AuraRack : constantes calibrées par mesure, pas par comparaison avec le matériel.
//
//   Contrôles : Gain (12 … 72 dB par pas de 6) + Fine (0 … +6 dB), HPF (Off / 80 / 120 Hz, 12 dB/oct), Pad −20 dB,
//   Phase Ø (Off / L / R / L+R), Input (Mic / Inst-DI), 48V (indicateur d'état), Link (Dual / Linked) + R Offset.
//
//   ÉTALONNAGE (important, c'est un plugin et non un micro) : le signal reçu est DÉJÀ numérisé. Le gain net appliqué est
//   donc Gain + Fine − 36 dB − Pad : 36 dB = gain unité (position nominale d'un micro dynamique), 12 dB = −24 dB net,
//   72 dB = +36 dB net. 0 dBu = −18 dBFS (+18 dBu = 0 dBFS). Plafond de l'étage : +24 dBu (+6 dBFS) ; le voyant passe au rouge
//   à +19 dBu (+1 dBFS), c'est-à-dire AVANT l'écrêtage de l'étage.
//
//   Chaîne : Pad / Gain (lissés 20 ms, aucun clic aux changements de cran) ─► « transformateur » (saturation douce des graves,
//   trajet Mic seulement ; Inst/DI le contourne par fondu de 20 ms) ─► HPF (fondu 10 ms) ─► polarité (rampe 5 ms)
//   ─► étage discret à plafond +24 dBu (tanh à gain unité, un peu d'harmonique 2, ADAA sous 176,4 kHz) ─► sortie.
//
//   - Dual : chaque canal a son gain (R = L + R Offset) ; Linked : les deux canaux ont le même gain.
//   - 48V : simple mémoire d'état affichée dans le rack (un plugin n'alimente pas de micro) : aucun effet sur le son.
//   - Voyant : crête à l'entrée de l'étage (takeStatusPeak, linéaire) ; getMeterAux = niveau de crête en dBu à décroissance lente.
//   - Constantes en secondes, calcul en double : même réponse de 44,1 à 384 kHz. Latence 0.

#pragma once

#include "CompCommon.h"

namespace aura
{
class AuraV9P2 final : public AuraModuleDSP
{
public:
    enum Param { gain, fine, hpf, pad, phase, input, phantom, link, rOffset, numParams };

    static constexpr double kUnityGainDb = 36.0, kDbuToDbfs = -18.0, kCeilingDbu = 24.0, kClipLampDbu = 19.0;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* gains[]    = { "12 dB", "18 dB", "24 dB", "30 dB", "36 dB", "42 dB", "48 dB", "54 dB", "60 dB", "66 dB", "72 dB" };
        static constexpr const char* hpfs[]     = { "Off", "80 Hz", "120 Hz" };
        static constexpr const char* pads[]     = { "Off", "-20 dB" };
        static constexpr const char* phases[]   = { "Off", "L", "R", "L+R" };
        static constexpr const char* inputs[]   = { "Mic", "Inst/DI" };
        static constexpr const char* phantoms[] = { "Off", "48V" };
        static constexpr const char* links[]    = { "Dual", "Linked" };
        static constexpr ParamSpec specs[] = {
            { "Gain",     "",   0.0, 10.0, 4.0, S::choice, gains, 11 },
            { "Fine",     "dB", 0.0, 6.0,  0.0, S::linear },
            { "HPF",      "",   0.0, 2.0,  0.0, S::choice, hpfs, 3 },
            { "Pad",      "",   0.0, 1.0,  0.0, S::choice, pads, 2 },
            { "Phase",    "",   0.0, 3.0,  0.0, S::choice, phases, 4 },
            { "Input",    "",   0.0, 1.0,  0.0, S::choice, inputs, 2 },
            { "48V",      "",   0.0, 1.0,  0.0, S::choice, phantoms, 2 },
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
            case phantom: break;   // état affiché seulement
            case link:    linked = v >= 0.5; updateGains(); break;
            case rOffset: offsetDb = v; updateGains(); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        for (auto* s : { &gainL, &gainR, &hpAmount, &diAmount, &polL, &polR }) s->reset (s->getTarget());
        hp.reset();
        lowpass.reset();
        stage.reset();
        peakAux = -120.0;
        statusPeak = 0.0;
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            double xl = L[i] * dsp::dbToGain (gainL.next()), xr = R[i] * dsp::dbToGain (gainR.next());

            // « Transformateur » (trajet Mic) : saturation douce des graves seuls ; Inst/DI le contourne.
            const double di = diAmount.next();
            if (di < 1.0)
            {
                const double lowL = lowpass.processSample (xl, 0), lowR = lowpass.processSample (xr, 1);
                xl += (1.0 - di) * (kCore * std::tanh (lowL / kCore) - lowL);
                xr += (1.0 - di) * (kCore * std::tanh (lowR / kCore) - lowR);
            }
            else { lowpass.processSample (xl, 0); lowpass.processSample (xr, 1); }

            const double hpa = hpAmount.next();
            if (hpa > 0.0)
            {
                const double hl = hp.processSample (xl, 0), hr = hp.processSample (xr, 1);
                xl += hpa * (hl - xl);
                xr += hpa * (hr - xr);
            }

            xl *= polL.next();
            xr *= polR.next();

            statusPeak = std::max (statusPeak, std::max (std::abs (xl), std::abs (xr)));
            L[i] = stage.process (xl, 0);
            R[i] = stage.process (xr, 1);
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
    static constexpr double kCore = 2.0;   // « noyau » du transformateur : les graves se compriment au-delà de ≈ +24 dBu

    void prepareInternal() override
    {
        for (auto* s : { &gainL, &gainR }) s->prepare (sampleRate, 20.0);
        hpAmount.prepare (sampleRate, 10.0);
        diAmount.prepare (sampleRate, 20.0);
        for (auto* s : { &polL, &polR }) s->prepare (sampleRate, 5.0);
        lowpass.setup (dsp::TptSvf::Type::lowPass, 60.0, 0.70710678118654752, 0.0, sampleRate);
        hp.setup (dsp::TptSvf::Type::highPass, 80.0, 0.70710678118654752, 0.0, sampleRate);
        // Plafond +24 dBu = +6 dBFS : a = 1 / 1,995.
        stage.prepare (sampleRate, 1.0 / dsp::dbToGain (kCeilingDbu + kDbuToDbfs), 0.04);
        polL.reset (1.0); polR.reset (1.0);
        updateGains();
    }

    void updateGains() noexcept
    {
        const double l = netGainDb (gainIndex, fineDb, padOn);
        gainL.setTarget (l);
        gainR.setTarget (linked ? l : l + offsetDb);
    }

    int gainIndex = 4, hpfIndex = 0;
    double fineDb = 0.0, offsetDb = 0.0, peakAux = -120.0, statusPeak = 0.0;
    bool padOn = false, linked = true;

    dsp::OnePoleSmoother gainL, gainR, hpAmount, diAmount, polL, polR;
    dsp::TptSvf hp, lowpass;
    compdetail::SatStage stage;
};

} // namespace aura
