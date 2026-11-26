// AuraRack — NextDSP
// Classe de base des modules d'effet et description de leurs paramètres.
// Sans dépendance à JUCE : le moteur et le banc d'essai l'utilisent directement.

#pragma once

#include "AuraDspCore.h"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace aura
{
/** Types de module. L'ordre est celui du sélecteur de slot (0 = slot vide) et ne doit jamais changer : il est sauvegardé. */
// L'ordre est persisté dans les sessions : on n'ajoute qu'à la fin.
enum class ModuleType : int { none = 0, eq, comp, duck, clip, limit, delay, verb, w9b };
inline constexpr int kNumModuleTypes = 9;

/** Nombre de paramètres génériques par slot (le module le plus riche en utilise 10). */
inline constexpr int kMaxModuleParams = 10;

//==============================================================================
struct ParamSpec
{
    enum class Scale { linear, logarithmic, choice };

    const char* name;
    const char* unit;
    double minValue, maxValue, defaultValue;
    Scale scale = Scale::linear;
    const char* const* choices = nullptr;   // pour Scale::choice
    int numChoices = 0;

    /** Valeur normalisée [0, 1] -> valeur physique. */
    double toPlain (double norm) const noexcept
    {
        norm = std::clamp (norm, 0.0, 1.0);
        switch (scale)
        {
            case Scale::logarithmic: return minValue * std::pow (maxValue / minValue, norm);
            case Scale::choice:      return std::round (norm * (numChoices - 1));
            case Scale::linear:      break;
        }
        // Les hôtes transmettent les valeurs normalisées en float : un « 0 dB » peut arriver à -3 µdB.
        // À moins d'un millionième de la course de zéro, la valeur vaut exactement zéro : un gain neutre
        // reste bit-transparent et une bande d'EQ à 0 dB reste court-circuitée.
        const double plain = minValue + norm * (maxValue - minValue);
        return std::abs (plain) < 1.0e-6 * (maxValue - minValue) ? 0.0 : plain;
    }

    /** Valeur physique -> valeur normalisée [0, 1]. */
    double toNormalised (double plain) const noexcept
    {
        switch (scale)
        {
            case Scale::logarithmic:
                plain = std::clamp (plain, minValue, maxValue);
                return std::log (plain / minValue) / std::log (maxValue / minValue);
            case Scale::choice:
                return numChoices > 1 ? std::clamp (plain, 0.0, (double) (numChoices - 1)) / (numChoices - 1) : 0.0;
            case Scale::linear: break;
        }
        return std::clamp ((plain - minValue) / (maxValue - minValue), 0.0, 1.0);
    }

    double defaultNormalised() const noexcept { return toNormalised (defaultValue); }

    /** Texte affiché (hôte, interface). */
    std::string format (double plain) const
    {
        if (scale == Scale::choice)
        {
            const int i = std::clamp ((int) std::lround (plain), 0, numChoices - 1);
            return choices[i];
        }
        char buf[64];
        const double a = std::abs (plain);
        if (std::string (unit) == "Hz" && a >= 1000.0)
            std::snprintf (buf, sizeof (buf), "%.2f kHz", plain / 1000.0);
        else if (std::string (unit) == "ms" && a >= 1000.0)
            std::snprintf (buf, sizeof (buf), "%.2f s", plain / 1000.0);
        else
            std::snprintf (buf, sizeof (buf), a < 10.0 ? "%.2f %s" : (a < 100.0 ? "%.1f %s" : "%.0f %s"), plain, unit);
        std::string s (buf);
        while (! s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }
};

struct ModuleInfo
{
    ModuleType type;
    const char* name;        // « AuraEQ »
    const char* shortName;   // « EQ » (pastille de slot)
    std::span<const ParamSpec> params;
    double defaultMixPercent = 100.0;   // Mix du slot appliqué quand on choisit ce module (Delay / Verb : 30 %)

    /** Module à variantes (AuraComp : 3 algorithmes) : `variantParam` est l'index du paramètre de choix,
        `variantMasks[v]` le masque des paramètres visibles pour la variante v (bit i = paramètre i).
        Les paramètres masqués existent toujours (identifiants figés) mais le module les ignore. */
    int variantParam = -1;
    std::span<const std::uint32_t> variantMasks {};

    /** Masque des paramètres à afficher pour la valeur physique du paramètre de variante. */
    std::uint32_t visibleMask (double variantPlain) const noexcept
    {
        if (variantParam < 0 || variantMasks.empty()) return (1u << params.size()) - 1u;
        const auto v = (std::size_t) std::clamp ((int) std::lround (variantPlain), 0, (int) variantMasks.size() - 1);
        return variantMasks[v];
    }
};

//==============================================================================
/** Bus de clé (sidechain) fourni à chaque bloc par le moteur. */
struct SidechainBlock
{
    const double* left  = nullptr;
    const double* right = nullptr;
    bool available = false;   // false : aucune clé externe valable, le module se rabat sur sa propre entrée
};

//==============================================================================
/**
    Classe de base de tous les modules AuraRack.

    Contrat temps réel :
      - prepare() et le constructeur allouent ; ils ne sont jamais appelés par le thread audio ;
      - reset(), setParameter(), process() n'allouent pas, ne verrouillent pas et ne font aucun appel système ;
      - process() reçoit au plus maxBlock échantillons, en stéréo (le moteur duplique le mono) ;
      - la latence annoncée est constante entre deux prepare() : le moteur s'en sert pour aligner le Dry.
*/
class AuraModuleDSP
{
public:
    virtual ~AuraModuleDSP() = default;

    virtual const ModuleInfo& getInfo() const noexcept = 0;

    void prepare (double newSampleRate, int newMaxBlock)
    {
        sampleRate = newSampleRate;
        maxBlock   = newMaxBlock;
        prepareInternal();
        for (int i = 0; i < (int) getInfo().params.size(); ++i)
            setParameter (i, getInfo().params[(std::size_t) i].defaultValue);
        reset();
    }

    virtual void reset() noexcept = 0;

    /** Valeur physique (dB, Hz, ms, %, index de choix) du paramètre `index`. */
    virtual void setParameter (int index, double plainValue) noexcept = 0;

    virtual void process (double* left, double* right, int numSamples, const SidechainBlock& key) noexcept = 0;

    virtual int getLatencySamples() const noexcept { return 0; }

    /** Réduction de gain courante en dB (>= 0), pour les indicateurs. */
    virtual double getGainReductionDb() const noexcept { return 0.0; }

    /** Crête de sortie (linéaire) depuis le dernier appel, puis remise à zéro — voyant de saturation (W9BMK1).
        Appelée une fois par bloc par le thread audio. */
    virtual double takeStatusPeak() noexcept { return 0.0; }

    double getSampleRate() const noexcept { return sampleRate; }

protected:
    virtual void prepareInternal() = 0;

    double sampleRate = 48000.0;
    int maxBlock = 512;
};

} // namespace aura
