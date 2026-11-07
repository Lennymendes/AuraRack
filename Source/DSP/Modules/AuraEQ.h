// AuraRack — NextDSP · AuraEQ
// Égaliseur paramétrique 3 bandes : Low Shelf, Peak (cloche), High Shelf.
// Trois SVF TPT en série, coefficients recalculés tous les 16 échantillons pendant le lissage
// (fréquence lissée en log, gain en dB, Q) : modulation sans zipper ni instabilité, de 44,1 à 384 kHz.

#pragma once

#include "../AuraModuleDSP.h"

namespace aura
{
class AuraEQ final : public AuraModuleDSP
{
public:
    enum Param { lsFreq, lsGain, pkFreq, pkGain, pkQ, hsFreq, hsGain, shelfQ, outGain, numParams };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr ParamSpec specs[] = {
            { "Low Freq",   "Hz", 20.0,  1000.0,  100.0, S::logarithmic },
            { "Low Gain",   "dB", -18.0, 18.0,    0.0,   S::linear },
            { "Mid Freq",   "Hz", 40.0,  20000.0, 1000.0, S::logarithmic },
            { "Mid Gain",   "dB", -18.0, 18.0,    0.0,   S::linear },
            { "Mid Q",      "",   0.1,   18.0,    0.707, S::logarithmic },
            { "High Freq",  "Hz", 1000.0, 40000.0, 8000.0, S::logarithmic },
            { "High Gain",  "dB", -18.0, 18.0,    0.0,   S::linear },
            { "Shelf Q",    "",   0.3,   2.0,     0.707, S::logarithmic },
            { "Output",     "dB", -24.0, 24.0,    0.0,   S::linear },
        };
        static const ModuleInfo i { ModuleType::eq, "AuraEQ", "EQ", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case lsFreq: case pkFreq: case hsFreq: smooth[(std::size_t) index].setTarget (std::log (v)); break;
            case outGain: smooth[(std::size_t) index].setTarget (dsp::dbToGain (v)); break;
            default:      if (index >= 0 && index < numParams) smooth[(std::size_t) index].setTarget (v); break;
        }
    }

    void reset() noexcept override
    {
        for (auto& s : smooth) s.reset (s.getTarget());
        updateCoefficients();
        low.reset(); mid.reset(); high.reset();
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        double* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            bool moving = false;
            for (auto& s : smooth) { if (s.isSmoothing()) { s.next(); moving = true; } }
            if (moving && (++coefCounter & 15) == 0) updateCoefficients();

            const double g = smooth[outGain].getCurrent();
            for (int c = 0; c < 2; ++c)
            {
                double x = ch[c][i];
                if (lowActive)  x = low.processSample (x, c);
                if (midActive)  x = mid.processSample (x, c);
                if (highActive) x = high.processSample (x, c);
                ch[c][i] = x * g;
            }
        }
    }

private:
    void prepareInternal() override
    {
        for (auto& s : smooth) s.prepare (sampleRate, 20.0);
    }

    void updateCoefficients() noexcept
    {
        using T = dsp::TptSvf::Type;
        const double sq = smooth[shelfQ].getCurrent();
        // Une bande à 0 dB est mathématiquement transparente : on la saute (aucune erreur d'arrondi ajoutée).
        // Une bande réactivée repart d'un état nul : son gain est alors ~0 dB, la transition est inaudible.
        auto activate = [] (bool& flag, bool now, dsp::TptSvf& f) { if (now && ! flag) f.reset(); flag = now; };
        activate (lowActive,  std::abs (smooth[lsGain].getCurrent()) > 1.0e-6, low);
        activate (midActive,  std::abs (smooth[pkGain].getCurrent()) > 1.0e-6, mid);
        activate (highActive, std::abs (smooth[hsGain].getCurrent()) > 1.0e-6, high);
        low.setup  (T::lowShelf,  std::exp (smooth[lsFreq].getCurrent()), sq, smooth[lsGain].getCurrent(), sampleRate);
        mid.setup  (T::bell,      std::exp (smooth[pkFreq].getCurrent()), smooth[pkQ].getCurrent(), smooth[pkGain].getCurrent(), sampleRate);
        high.setup (T::highShelf, std::exp (smooth[hsFreq].getCurrent()), sq, smooth[hsGain].getCurrent(), sampleRate);
    }

    std::array<dsp::OnePoleSmoother, numParams> smooth;
    dsp::TptSvf low, mid, high;
    bool lowActive = false, midActive = false, highActive = false;
    unsigned coefCounter = 0;
};

} // namespace aura
