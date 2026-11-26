// AuraRack — NextDSP · V9C3
// Compresseur à VCA discret « quad », inspiré d'un compresseur de bus VCA des années 70-80 (famille Vertigo VSC-3),
// version DXD (352,8 / 384 kHz) avec **Auto Focus**. Conception AuraRack : constantes calibrées par mesure, pas par
// comparaison avec le matériel.
//
//   Contrôles : Threshold (dBu), Ratio (Soft + 2/4/8/10:1 + Limit 40:1), Attack (10 crans), Release (10 crans + Auto Fast
//   + Auto Slow), Make-Up, SC HPF (Off / 70 / 140 Hz), Detect (Peak / RMS), Mode (Stereo / Mono / Dual / M-S),
//   Auto Focus (Manual / Auto) + Focus (1 à 12 dB), + le Bypass et le Mix du slot.
//
//   Étalonnage : 0 dBu = −18 dBFS (+18 dBu = 0 dBFS, EBU R68). Threshold −22 … +23 dBu = −40 … +5 dBFS.
//
//   Chaîne : entrée ─► « transformateur » (saturation douce des graves seuls) ─► clé (HP SC) ─► détecteur Peak / RMS
//            ─► courbe (genou 4 dB, ou Soft) ─► cellule d'enveloppe (Attack / Release / Auto) ─► gain ─► Make-Up
//            ─► étage VCA / sortie à saturation progressive ─► sortie.
//
//   - STEREO : clé liée (max des deux canaux), un seul gain : l'image ne dérive pas. MONO : clé = somme. DUAL : deux
//     détecteurs et deux gains indépendants (contrôles communs). M/S : compression séparée du Mid et du Side.
//   - SOFT (« TipToe ») : le ratio croît avec le niveau, de 1:1 au seuil vers 8:1 : r(o) = 0,875·o² / (o + 12), o = dépassement (dB).
//   - Release fixe : constante de temps exacte (cellule sans mémoire). Auto Fast / Auto Slow : double relâchement dépendant
//     du programme (court après un passage bref, long après une compression soutenue).
//   - Coloration : la raideur de l'étage de sortie croît avec (réduction de gain + Make-Up) : presque linéaire au repos,
//     harmoniques 3 et 2 plus présents quand on comprime fort ou qu'on remonte le Make-Up.
//   - AUTO FOCUS (FocusTracker.h, comme le W9BMK1) : le seuil est piloté lentement pour viser la réduction moyenne Focus.
//   - Constantes en secondes (expm1), calcul en double : même réponse de 44,1 à 384 kHz. Latence 0.
//
//   Limites assumées : contrôles COMMUNS aux deux canaux en Dual / M-S (pas de seconde série de potards : 10 paramètres
//   par slot) ; pas de compensation « equal loudness » au changement de ratio.

#pragma once

#include "CompCommon.h"
#include "FocusTracker.h"

namespace aura
{
class AuraV9C3 final : public AuraModuleDSP
{
public:
    enum Param { threshold, ratio, attack, release, makeUp, scHpf, detect, mode, autoFocus, focus, numParams };
    enum Ratio { soft = 0, r2, r4, r8, r10, limit };
    enum Mode { stereo = 0, mono, dual, midSide };

    static constexpr double kDbuToDbfs = -18.0;    // dBFS = dBu − 18
    static constexpr double kKneeDb = 4.0;
    static constexpr std::array<double, 6> kRatios { 1.0, 2.0, 4.0, 8.0, 10.0, 40.0 };
    static constexpr std::array<double, 10> kAttackMs { 0.1, 0.2, 0.4, 0.7, 1.5, 3.0, 6.0, 12.0, 25.0, 40.0 };
    static constexpr std::array<double, 10> kReleaseMs { 50.0, 80.0, 120.0, 180.0, 260.0, 380.0, 550.0, 800.0, 1000.0, 1200.0 };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* ratios[]   = { "Soft", "2:1", "4:1", "8:1", "10:1", "Limit" };
        static constexpr const char* attacks[]  = { "0.1 ms", "0.2 ms", "0.4 ms", "0.7 ms", "1.5 ms", "3 ms", "6 ms", "12 ms", "25 ms", "40 ms" };
        static constexpr const char* releases[] = { "50 ms", "80 ms", "120 ms", "180 ms", "260 ms", "380 ms", "550 ms", "0.8 s", "1.0 s", "1.2 s", "Auto Fast", "Auto Slow" };
        static constexpr const char* hpfs[]     = { "Off", "70 Hz", "140 Hz" };
        static constexpr const char* detects[]  = { "Peak", "RMS" };
        static constexpr const char* modes[]    = { "Stereo", "Mono", "Dual", "M/S" };
        static constexpr const char* autos[]    = { "Manual", "Auto" };
        static constexpr ParamSpec specs[] = {
            { "Threshold", "dBu", -22.0, 23.0, 0.0, S::linear },
            { "Ratio",     "",    0.0, 5.0,  2.0, S::choice, ratios, 6 },
            { "Attack",    "",    0.0, 9.0,  4.0, S::choice, attacks, 10 },
            { "Release",   "",    0.0, 11.0, 3.0, S::choice, releases, 12 },
            { "Make-Up",   "dB",  0.0, 24.0, 0.0, S::linear },
            { "SC HPF",    "",    0.0, 2.0,  0.0, S::choice, hpfs, 3 },
            { "Detect",    "",    0.0, 1.0,  0.0, S::choice, detects, 2 },
            { "Mode",      "",    0.0, 3.0,  0.0, S::choice, modes, 4 },
            { "Auto Focus", "",   0.0, 1.0,  0.0, S::choice, autos, 2 },
            { "Focus",     "dB",  1.0, 12.0, 3.0, S::linear },
        };
        static const ModuleInfo i { ModuleType::v9c3, "V9C3", "V9C3", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    /** Réduction statique (dB) pour un niveau de clé et un seuil en dBFS. Soft : ratio progressif. */
    static double staticReduction (double xDb, double thresholdDb, int ratioIndex) noexcept
    {
        if (ratioIndex == soft)
        {
            const double o = xDb - thresholdDb;
            return o > 0.0 ? 0.875 * o * o / (o + 12.0) : 0.0;
        }
        return compdetail::kneeReduction (xDb, thresholdDb, kRatios[(std::size_t) std::clamp (ratioIndex, 0, 5)], kKneeDb);
    }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case threshold: manualDbu = v; thresholdSm.setTarget (v + kDbuToDbfs); break;
            case ratio:     ratioIndex = std::clamp ((int) std::lround (v), 0, 5); break;
            case attack:    attackIndex = std::clamp ((int) std::lround (v), 0, 9); configureCells(); break;
            case release:   releaseIndex = std::clamp ((int) std::lround (v), 0, 11); configureCells(); break;
            case makeUp:    makeUpSm.setTarget (v); break;
            case scHpf:     hpfIndex = std::clamp ((int) std::lround (v), 0, 2); configureKey(); break;
            case detect:    rms = v >= 0.5; break;
            case mode:      setMode ((Mode) std::clamp ((int) std::lround (v), 0, 3)); break;
            case autoFocus: setAutoFocus (v >= 0.5); break;
            case focus:     for (auto& c : ch) c.tracker.setTargetReduction (v); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        thresholdSm.reset (thresholdSm.getTarget());
        makeUpSm.reset (makeUpSm.getTarget());
        driveSm.reset (kBaseDrive);
        for (auto& c : ch) { c.env = c.power = c.reduction = 0.0; c.cell.reset(); c.tracker.reset (thresholdSm.getCurrent()); }
        keyHp.reset();
        lowpass.reset();
        sat.reset();
        lastReduction = 0.0;
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        const int nCh = (currentMode == dual || currentMode == midSide) ? 2 : 1;
        for (int i = 0; i < n; ++i)
        {
            // 1. « Transformateur » : saturation douce des graves seuls (x − LP + sat(LP) : transparent hors graves).
            double xl = L[i], xr = R[i];
            const double lowL = lowpass.processSample (xl, 0), lowR = lowpass.processSample (xr, 1);
            xl += std::tanh (kXfmrDrive * lowL) / kXfmrDrive - lowL;
            xr += std::tanh (kXfmrDrive * lowR) / kXfmrDrive - lowR;

            // 2. Clé selon le mode, filtrée HP.
            double s0 = xl, s1 = xr;
            if (currentMode == mono || currentMode == midSide) { s0 = 0.5 * (xl + xr); s1 = 0.5 * (xl - xr); }
            const double k0 = hpfIndex > 0 ? keyHp.processSample (s0, 0) : s0;
            const double k1 = hpfIndex > 0 ? keyHp.processSample (s1, 1) : s1;

            double a0, a1 = 0.0;   // niveau (crête) ou puissance (RMS) par voie de détection
            if (rms)
            {
                a0 = currentMode == stereo ? 0.5 * (k0 * k0 + k1 * k1) : k0 * k0;
                a1 = k1 * k1;
            }
            else
            {
                a0 = currentMode == stereo ? std::max (std::abs (k0), std::abs (k1)) : std::abs (k0);
                a1 = std::abs (k1);
            }

            // 3. Détection -> réduction, par voie.
            const double thrFixed = thresholdSm.next();
            for (int c = 0; c < nCh; ++c)
            {
                auto& st = ch[(std::size_t) c];
                const double a = c == 0 ? a0 : a1;
                double xDb;
                if (rms)
                {
                    st.power += rmsCoef * (a - st.power);
                    if (st.power < 1.0e-30) st.power = 0.0;
                    xDb = dsp::gainToDb (std::sqrt (2.0 * st.power));   // référencé crête (+3 dB pour un sinus)
                }
                else
                {
                    st.env += (a > st.env ? peakAttack : peakRelease) * (a - st.env);
                    if (st.env < 1.0e-30) st.env = 0.0;
                    xDb = dsp::gainToDb (st.env);
                }

                double thr = thrFixed;
                if (autoOn)
                {
                    st.tracker.process (xDb, st.reduction, ratioIndex == soft ? 4.0 : kRatios[(std::size_t) ratioIndex], ratioIndex == soft ? 0.0 : kKneeDb);
                    thr = st.tracker.getThreshold();
                }
                st.reduction = st.cell.next (staticReduction (xDb, thr, ratioIndex));
            }

            // 4. Gains (Make-Up compris), application selon le mode.
            const double mu = makeUpSm.next();
            const double g0 = dsp::dbToGain (mu - ch[0].reduction);
            double yl, yr;
            if (currentMode == dual)
            {
                yl = xl * g0;
                yr = xr * dsp::dbToGain (mu - ch[1].reduction);
            }
            else if (currentMode == midSide)
            {
                const double m = 0.5 * (xl + xr) * g0, s = 0.5 * (xl - xr) * dsp::dbToGain (mu - ch[1].reduction);
                yl = m + s;
                yr = m - s;
            }
            else
            {
                yl = xl * g0;
                yr = xr * g0;
            }

            // 5. Étage VCA / sortie : la raideur suit (réduction + Make-Up).
            const double red = nCh == 2 ? 0.5 * (ch[0].reduction + ch[1].reduction) : ch[0].reduction;
            driveSm.setTarget (std::min (kMaxDrive, kBaseDrive + kDriveSlope * (red + mu)));
            sat.setDrive (driveSm.next());
            L[i] = sat.process (yl, 0);
            R[i] = sat.process (yr, 1);
            lastReduction = nCh == 2 ? std::max (ch[0].reduction, ch[1].reduction) : ch[0].reduction;
        }
    }

    double getGainReductionDb() const noexcept override { return lastReduction; }

    /** Seuil effectif en dBu (celui du suiveur en Auto). */
    double getMeterAux() const noexcept override { return (autoOn ? ch[0].tracker.getThreshold() : thresholdSm.getTarget()) - kDbuToDbfs; }

    bool isAutoFocus() const noexcept { return autoOn; }

private:
    static constexpr double kBaseDrive = 0.10, kDriveSlope = 0.0125, kMaxDrive = 0.5, kXfmrDrive = 0.6;

    struct Channel
    {
        double env = 0.0, power = 0.0, reduction = 0.0;
        compdetail::OptoCell cell;
        compdetail::FocusTracker tracker;
    };

    void prepareInternal() override
    {
        thresholdSm.prepare (sampleRate, 20.0);
        makeUpSm.prepare (sampleRate, 20.0);
        driveSm.prepare (sampleRate, 50.0);
        for (auto& c : ch) { c.cell.prepare (sampleRate); c.tracker.prepare (sampleRate); }
        sat.prepare (sampleRate, kBaseDrive, 0.05);
        lowpass.setup (dsp::TptSvf::Type::lowPass, 120.0, 0.70710678118654752, 0.0, sampleRate);
        peakAttack = compdetail::alpha (20.0e-6, sampleRate);
        peakRelease = compdetail::alpha (0.008, sampleRate);
        rmsCoef = compdetail::alpha (0.010, sampleRate);
        configureCells();
        configureKey();
    }

    void configureKey() noexcept
    {
        keyHp.setup (dsp::TptSvf::Type::highPass, hpfIndex == 2 ? 140.0 : 70.0, 0.70710678118654752, 0.0, sampleRate);
    }

    void configureCells() noexcept
    {
        const double att = kAttackMs[(std::size_t) attackIndex] * 0.001;
        for (auto& c : ch)
        {
            c.cell.attackDepthDb = 1.0e9;   // pas d'accélération de l'attaque : Attack est exact
            c.cell.memKneeDb = 6.0;
            if (releaseIndex < 10)
            {
                const double rel = kReleaseMs[(std::size_t) releaseIndex] * 0.001;
                c.cell.fixedBlend = 0.0;    // voie rapide seule : constante de temps exacte
                c.cell.setTimes (att, rel, rel);
            }
            else
            {
                c.cell.fixedBlend = -1.0;   // dépendance au programme
                if (releaseIndex == 10) c.cell.setTimes (att, 0.040, 0.500);
                else                    c.cell.setTimes (att, 0.150, 2.500);
            }
        }
    }

    void setMode (Mode m) noexcept
    {
        if (m == currentMode) return;
        const bool wasTwo = currentMode == dual || currentMode == midSide, isTwo = m == dual || m == midSide;
        if (! wasTwo && isTwo) ch[1] = ch[0];   // la 2e voie part de l'état de la 1re : pas de saut de gain
        currentMode = m;
    }

    void setAutoFocus (bool on) noexcept
    {
        if (on == autoOn) return;
        if (on)
            for (auto& c : ch) c.tracker.reset (thresholdSm.getCurrent());   // part du seuil manuel : aucun saut
        else
        {
            thresholdSm.reset (ch[0].tracker.getThreshold());                // glisse vers le seuil manuel (20 ms)
            thresholdSm.setTarget (manualDbu + kDbuToDbfs);
        }
        autoOn = on;
    }

    int ratioIndex = r4, attackIndex = 4, releaseIndex = 3, hpfIndex = 0;
    bool rms = false, autoOn = false;
    Mode currentMode = stereo;
    double manualDbu = 0.0;
    double peakAttack = 0.0, peakRelease = 0.0, rmsCoef = 0.0, lastReduction = 0.0;

    std::array<Channel, 2> ch;
    dsp::OnePoleSmoother thresholdSm, makeUpSm, driveSm;
    compdetail::SatStage sat;
    dsp::TptSvf keyHp, lowpass;
};

} // namespace aura
