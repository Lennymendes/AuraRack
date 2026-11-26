// AuraRack — NextDSP

#include "Parameters.h"

namespace aura::params
{
namespace
{
    juce::String formatDb (float v, int)        { return juce::String (v, 1) + " dB"; }
    juce::String formatPercent (float v, int)   { return juce::String (juce::roundToInt (v)) + " %"; }

    // Pas de quantification (interval 0) : -24 + 48·0,5 vaut exactement 0, même quand le compilateur fusionne le
    // calcul en FMA (arm64). Avec un pas de 0,01, JUCE calculait -24 + 0,01f·2400 = -5,4e-7 dB sur Apple Silicon.
    juce::NormalisableRange<float> trimRange()  { return { -24.0f, 24.0f }; }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout (SlotTypeProvider slotType)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Barre globale.
    auto global = std::make_unique<AudioProcessorParameterGroup> ("global", "Global", " | ");
    global->addChild (std::make_unique<AudioParameterFloat> (ParameterID { inGain, 1 }, "Input Gain", trimRange(), 0.0f,
                                                             AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (formatDb)));
    global->addChild (std::make_unique<AudioParameterFloat> (ParameterID { outGain, 1 }, "Output Gain", trimRange(), 0.0f,
                                                             AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (formatDb)));
    global->addChild (std::make_unique<AudioParameterFloat> (ParameterID { masterMix, 1 }, "Master Mix", NormalisableRange<float> (0.0f, 100.0f), 100.0f,
                                                             AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (formatPercent)));
    global->addChild (std::make_unique<AudioParameterChoice> (ParameterID { scSource, 1 }, "Sidechain Source",
                                                              StringArray { "Bus Sidechain", "Entree du rack" }, 0));
    global->addChild (std::make_unique<AudioParameterBool> (ParameterID { autoGain, 1 }, "Auto Gain (Rack)", false));
    global->addChild (std::make_unique<AudioParameterFloat> (ParameterID { width, 1 }, "Stereo Width", NormalisableRange<float> (0.0f, 200.0f), 100.0f,
                                                             AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (formatPercent)));
    global->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bassMono, 1 }, "Bass Mono",
                                                              StringArray { "Off", "60 Hz", "100 Hz", "150 Hz", "250 Hz" }, 0));
    global->addChild (std::make_unique<AudioParameterBool> (ParameterID { mono, 1 }, "Mono", false));
    layout.add (std::move (global));

    // Liste des types de module (index = ModuleType).
    StringArray typeNames;
    for (int t = 0; t < kNumModuleTypes; ++t)
        typeNames.add (moduleInfo ((ModuleType) t).name);

    for (int s = 0; s < kNumSlots; ++s)
    {
        const String n = "Slot " + String (s + 1);
        auto group = std::make_unique<AudioProcessorParameterGroup> ("slot" + String (s + 1), n, " | ");

        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { type (s), 1 }, n + " Type", typeNames, 0,
                                                                 AudioParameterChoiceAttributes().withAutomatable (false)));
        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { bypass (s), 1 }, n + " Bypass", false));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { mix (s), 1 }, n + " Mix", NormalisableRange<float> (0.0f, 100.0f), 100.0f,
                                                                AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (formatPercent)));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { inTrim (s), 1 }, n + " In Trim", trimRange(), 0.0f,
                                                                AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (formatDb)));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { outTrim (s), 1 }, n + " Out Trim", trimRange(), 0.0f,
                                                                AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (formatDb)));

        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { autoGainSlot (s), 1 }, n + " Auto Gain", false));

        for (int p = 0; p < kMaxModuleParams; ++p)
        {
            // Texte et saisie suivent le module installé dans le slot (Hz, dB, ms, choix…).
            auto toText = [slotType, s, p] (float v, int) -> String
            {
                const auto& info = moduleInfo (slotType (s));
                if (p >= (int) info.params.size()) return "-";
                const auto& spec = info.params[(std::size_t) p];
                return String (spec.format (spec.toPlain (v)));
            };
            auto fromText = [slotType, s, p] (const String& text) -> float
            {
                const auto& info = moduleInfo (slotType (s));
                if (p >= (int) info.params.size()) return 0.0f;
                const auto& spec = info.params[(std::size_t) p];
                if (spec.scale == ParamSpec::Scale::choice)
                {
                    for (int c = 0; c < spec.numChoices; ++c)
                        if (text.trim().equalsIgnoreCase (spec.choices[c]))
                            return (float) spec.toNormalised ((double) c);
                }
                double plain = text.getDoubleValue();
                if (text.containsIgnoreCase ("k") && spec.unit == std::string ("Hz")) plain *= 1000.0;
                if (text.trim().endsWithIgnoreCase (" s") && spec.unit == std::string ("ms")) plain *= 1000.0;
                return (float) spec.toNormalised (plain);
            };

            group->addChild (std::make_unique<AudioParameterFloat> (
                ParameterID { generic (s, p), 1 }, n + " P" + String (p + 1),
                NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                AudioParameterFloatAttributes().withStringFromValueFunction (toText).withValueFromStringFunction (fromText)));
        }
        layout.add (std::move (group));
    }

    return layout;
}

} // namespace aura::params
