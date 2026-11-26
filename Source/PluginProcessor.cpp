// AuraRack — NextDSP

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <map>
#include <optional>

namespace
{
    constexpr int kStateVersion = 4;   // 4 : auto-gain, utilitaire stéréo, A / B, presets, V9C3 / V9P2 ; 3 : E9DS, réglages d'interface (langue, aide, LUFS) ; 2 : AuraComp à 3 algorithmes, 10 paramètres génériques
    const juce::Identifier kOrderProperty { "order" };
    const juce::Identifier kVersionProperty { "auraRackVersion" };
    const juce::Identifier kAbActive { "abActive" }, kPresetIndex { "presetIndex" }, kAbNode { "AB" }, kSnapNode { "SNAP" }, kValueNode { "V" };
    const juce::Identifier kUiLanguage { "uiLanguage" }, kUiInfo { "uiInfo" }, kUiLoudness { "uiLoudness" }, kUiSlot { "uiSlot" };

    juce::String orderToString (const aura::SlotOrder& o)
    {
        juce::StringArray parts;
        for (int s : o) parts.add (juce::String (s));
        return parts.joinIntoString (",");
    }

    std::optional<aura::SlotOrder> orderFromString (const juce::String& text)
    {
        juce::StringArray parts;
        parts.addTokens (text, ",", "");
        if (parts.size() != aura::kNumSlots) return std::nullopt;
        aura::SlotOrder o {};
        for (int i = 0; i < aura::kNumSlots; ++i) o[(std::size_t) i] = parts[i].getIntValue();
        if (! aura::RackRouting::isValid (o)) return std::nullopt;
        return o;
    }
}

//==============================================================================
juce::AudioProcessor::BusesProperties AuraRackProcessor::makeBuses()
{
    return BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true);   // clé Kick / 808 (VST3 kAux, AU)
}

AuraRackProcessor::AuraRackProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "AuraRack",
             aura::params::createLayout ([this] (int slot) { return engine.getModuleType (slot); }))
{
    bindEngine();
    startTimerHz (30);
}

AuraRackProcessor::~AuraRackProcessor()
{
    stopTimer();
}

void AuraRackProcessor::bindEngine()
{
    using namespace aura;
    auto raw = [this] (const juce::String& id) -> const std::atomic<float>*
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    for (int s = 0; s < kNumSlots; ++s)
    {
        SlotParameterSources src;
        src.bypass  = raw (params::bypass (s));
        src.mix     = raw (params::mix (s));
        src.inTrim  = raw (params::inTrim (s));
        src.outTrim = raw (params::outTrim (s));
        src.autoGain = raw (params::autoGainSlot (s));
        for (int p = 0; p < kMaxModuleParams; ++p)
            src.params[(std::size_t) p] = raw (params::generic (s, p));
        engine.bindSlot (s, src);
    }

    engine.bindGlobal ({ raw (params::inGain), raw (params::outGain), raw (params::masterMix), raw (params::scSource),
                         raw (params::autoGain), raw (params::width), raw (params::bassMono), raw (params::mono) });
}

//==============================================================================
bool AuraRackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != mainOut) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void AuraRackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    engine.setLoudnessMono (getMainBusNumInputChannels() == 1);
    engine.prepare (sampleRate, samplesPerBlock);

    // 4 canaux de travail : L, R, Sidechain L, Sidechain R. Un bloc plus grand est traité par morceaux.
    workCapacity = std::max (samplesPerBlock, 512);
    work.setSize (4, workCapacity, false, true, false);

    setLatencySamples (engine.getRequestedLatency());
}

void AuraRackProcessor::releaseResources()
{
    engine.releaseResources();
}

void AuraRackProcessor::reset()
{
    // Appelé par l'hôte au stop / repositionnement, parfois sur le thread audio : sans allocation ni verrou.
    engine.reset();
}

double AuraRackProcessor::getTailLengthSeconds() const
{
    // RT60 max de AuraVerb (20 s) + Pre-Delay + queue de AuraDelay.
    return 22.0;
}

//==============================================================================
void AuraRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processAnyPrecision (buffer);
}

void AuraRackProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processAnyPrecision (buffer);
}

template <typename Sample>
void AuraRackProcessor::processAnyPrecision (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    const int numSamples = buffer.getNumSamples();

    auto mainBus = getBusBuffer (buffer, true, 0);
    const int mainChannels = mainBus.getNumChannels();

    const auto* scBusInfo = getBus (true, 1);
    const bool scOn = scBusInfo != nullptr && scBusInfo->isEnabled() && getChannelCountOfBus (true, 1) > 0;
    sidechainConnected.store (scOn, std::memory_order_relaxed);

    // Canaux de sortie sans entrée correspondante : silence.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (mainChannels == 0 || workCapacity == 0) return;

    double* L = work.getWritePointer (0);
    double* R = work.getWritePointer (1);
    double* scL = work.getWritePointer (2);
    double* scR = work.getWritePointer (3);

    for (int offset = 0; offset < numSamples; offset += workCapacity)
    {
        const int n = std::min (workCapacity, numSamples - offset);

        const Sample* inL = mainBus.getReadPointer (0, offset);
        const Sample* inR = mainBus.getReadPointer (mainChannels > 1 ? 1 : 0, offset);
        for (int i = 0; i < n; ++i) { L[i] = (double) inL[i]; R[i] = (double) inR[i]; }

        if (scOn)
        {
            auto scBus = getBusBuffer (buffer, true, 1);
            const Sample* sL = scBus.getReadPointer (0, offset);
            const Sample* sR = scBus.getReadPointer (scBus.getNumChannels() > 1 ? 1 : 0, offset);
            for (int i = 0; i < n; ++i) { scL[i] = (double) sL[i]; scR[i] = (double) sR[i]; }
        }

        processDouble (L, R, n, scOn ? scL : nullptr, scOn ? scR : nullptr, scOn);

        Sample* outL = mainBus.getWritePointer (0, offset);
        if (mainChannels > 1)
        {
            Sample* outR = mainBus.getWritePointer (1, offset);
            for (int i = 0; i < n; ++i) { outL[i] = (Sample) L[i]; outR[i] = (Sample) R[i]; }
        }
        else
        {
            for (int i = 0; i < n; ++i) outL[i] = (Sample) (0.5 * (L[i] + R[i]));
        }
    }

    // Charge CPU : temps de calcul / durée du bloc, lissée.
    const double sr = currentSampleRate.load (std::memory_order_relaxed);
    if (sr > 0.0 && numSamples > 0)
    {
        const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
        const double load = elapsed / ((double) numSamples / sr);
        const float previous = cpuLoad.load (std::memory_order_relaxed);
        cpuLoad.store (previous + 0.1f * ((float) load - previous), std::memory_order_relaxed);
    }
}

void AuraRackProcessor::processDouble (double* L, double* R, int n, const double* scL, const double* scR, bool scOn) noexcept
{
    engine.process (L, R, n, scL, scR, scOn);
}

//==============================================================================
// Message thread
//==============================================================================
void AuraRackProcessor::timerCallback()
{
    std::unique_lock<std::mutex> lock (stateMutex, std::try_to_lock);
    if (! lock.owns_lock()) return;

    syncModulesFromParameters (true);
    engine.collectGarbage();

    const int latency = engine.getRequestedLatency();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

void AuraRackProcessor::syncModulesFromParameters (bool resetParametersToModuleDefaults)
{
    for (int s = 0; s < aura::kNumSlots; ++s)
    {
        auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (aura::params::type (s)));
        if (p == nullptr) continue;
        const auto wanted = (aura::ModuleType) juce::jlimit (0, aura::kNumModuleTypes - 1, p->getIndex());
        if (wanted == engine.getModuleType (s)) continue;

        if (resetParametersToModuleDefaults)
            applyModuleDefaults (s, wanted);
        engine.setModuleType (s, wanted);
    }
}

void AuraRackProcessor::applyModuleDefaults (int slot, aura::ModuleType type)
{
    const auto& info = aura::moduleInfo (type);
    for (int p = 0; p < aura::kMaxModuleParams; ++p)
    {
        auto* param = apvts.getParameter (aura::params::generic (slot, p));
        const float v = p < (int) info.params.size() ? (float) info.params[(std::size_t) p].defaultNormalised() : 0.5f;
        param->setValueNotifyingHost (v);
    }
    if (auto* mix = apvts.getParameter (aura::params::mix (slot)))
        mix->setValueNotifyingHost (mix->convertTo0to1 ((float) info.defaultMixPercent));
}

void AuraRackProcessor::setSlotModule (int slot, aura::ModuleType type)
{
    if (slot < 0 || slot >= aura::kNumSlots) return;
    std::lock_guard<std::mutex> lock (stateMutex);
    if (auto* p = apvts.getParameter (aura::params::type (slot)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) type));
    syncModulesFromParameters (true);
    setLatencySamples (engine.getRequestedLatency());
}

void AuraRackProcessor::setSlotOrder (const aura::SlotOrder& order)
{
    if (! aura::RackRouting::isValid (order)) return;
    engine.setOrder (order);
    apvts.state.setProperty (kOrderProperty, orderToString (order), nullptr);
}

void AuraRackProcessor::moveSlot (int fromPosition, int toPosition)
{
    if (engine.moveSlot (fromPosition, toPosition))
        apvts.state.setProperty (kOrderProperty, orderToString (engine.getOrder()), nullptr);
}

AuraRackProcessor::UiSettings AuraRackProcessor::getUiSettings() const
{
    UiSettings u;
    u.language = juce::jlimit (0, 15, (int) apvts.state.getProperty (kUiLanguage, 0));
    u.info = (bool) apvts.state.getProperty (kUiInfo, false);
    u.loudness = (bool) apvts.state.getProperty (kUiLoudness, false);
    u.selectedSlot = juce::jlimit (0, aura::kNumSlots - 1, (int) apvts.state.getProperty (kUiSlot, 0));
    return u;
}

void AuraRackProcessor::setUiSettings (const UiSettings& u)
{
    const auto before = getUiSettings();
    apvts.state.setProperty (kUiLanguage, u.language, nullptr);
    apvts.state.setProperty (kUiInfo, u.info, nullptr);
    apvts.state.setProperty (kUiLoudness, u.loudness, nullptr);
    apvts.state.setProperty (kUiSlot, juce::jlimit (0, aura::kNumSlots - 1, u.selectedSlot), nullptr);
    if (u.loudness && ! before.loudness) engine.requestLoudnessReset();   // nouvelle mesure à chaque activation
    engine.setLoudnessEnabled (u.loudness);
}

//==============================================================================
// Instantanés, A / B, presets
//==============================================================================
AuraRackProcessor::RackSnapshot AuraRackProcessor::captureSnapshot() const
{
    RackSnapshot snap;
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            snap.values.emplace_back (withId->paramID, withId->getValue());
    snap.order = engine.getOrder();
    snap.valid = true;
    return snap;
}

void AuraRackProcessor::applySnapshot (const RackSnapshot& snap)
{
    std::lock_guard<std::mutex> lock (stateMutex);
    applySnapshotLocked (snap);
}

void AuraRackProcessor::applySnapshotLocked (const RackSnapshot& snap)
{
    if (! snap.valid) return;
    auto setNormalised = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
    };

    // 1. Les types de module d'abord, installés sans remise aux valeurs par défaut ; 2. puis tous les réglages.
    for (const auto& [id, v] : snap.values)
        if (id.endsWith ("_type")) setNormalised (id, v);
    syncModulesFromParameters (false);
    for (const auto& [id, v] : snap.values)
        if (! id.endsWith ("_type")) setNormalised (id, v);

    if (aura::RackRouting::isValid (snap.order))
    {
        engine.setOrder (snap.order);
        apvts.state.setProperty (kOrderProperty, orderToString (snap.order), nullptr);
    }
    setLatencySamples (engine.getRequestedLatency());
}

bool AuraRackProcessor::snapshotsEqual (const RackSnapshot& a, const RackSnapshot& b) noexcept
{
    if (a.valid != b.valid || a.values.size() != b.values.size() || a.order != b.order) return false;
    for (std::size_t i = 0; i < a.values.size(); ++i)
        if (a.values[i].first != b.values[i].first || std::abs (a.values[i].second - b.values[i].second) > 1.0e-5f) return false;
    return true;
}

void AuraRackProcessor::selectAb (int which)
{
    if (which < 0 || which > 1) return;
    std::lock_guard<std::mutex> lock (stateMutex);
    if (which == abActive) return;
    abSlots[(std::size_t) abActive] = captureSnapshot();     // garde les retouches de l'emplacement quitté
    if (! abSlots[(std::size_t) which].valid)
        abSlots[(std::size_t) which] = abSlots[(std::size_t) abActive];   // B démarre comme A
    applySnapshotLocked (abSlots[(std::size_t) which]);
    abActive = which;
    apvts.state.setProperty (kAbActive, abActive, nullptr);
}

void AuraRackProcessor::copyToOtherAb()
{
    std::lock_guard<std::mutex> lock (stateMutex);
    abSlots[(std::size_t) (1 - abActive)] = captureSnapshot();
}

AuraRackProcessor::RackSnapshot AuraRackProcessor::buildPresetSnapshot (const aura::presets::Preset& preset) const
{
    using namespace aura;
    RackSnapshot snap;
    std::map<juce::String, float> values;
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            values[withId->paramID] = withId->getDefaultValue();

    auto setPlain = [this, &values] (const juce::String& id, double plain)
    {
        if (auto* p = apvts.getParameter (id)) values[id] = p->convertTo0to1 ((float) plain);
    };
    const auto& g = preset.global;
    setPlain (params::inGain, g.inGain);
    setPlain (params::outGain, g.outGain);
    setPlain (params::autoGain, g.autoGain ? 1.0 : 0.0);
    setPlain (params::width, g.widthPercent);
    setPlain (params::bassMono, g.bassMono);
    setPlain (params::mono, g.mono ? 1.0 : 0.0);

    for (int s = 0; s < kNumSlots; ++s)
    {
        const presets::SlotDef empty;
        const auto& def = s < (int) preset.slots.size() ? preset.slots[(std::size_t) s] : empty;
        const auto& info = moduleInfo (def.type);
        setPlain (params::type (s), (double) (int) def.type);
        for (int p = 0; p < kMaxModuleParams; ++p)
            values[params::generic (s, p)] = p < (int) info.params.size() ? (float) info.params[(std::size_t) p].defaultNormalised() : 0.5f;
        for (const auto& st : def.settings)
            if (st.param >= 0 && st.param < (int) info.params.size())
                values[params::generic (s, st.param)] = (float) info.params[(std::size_t) st.param].toNormalised (st.plain);
        setPlain (params::mix (s), def.mixPercent >= 0.0 ? def.mixPercent : info.defaultMixPercent);
        setPlain (params::autoGainSlot (s), def.autoGain ? 1.0 : 0.0);
    }

    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            snap.values.emplace_back (withId->paramID, values[withId->paramID]);
    snap.order = RackRouting::identity();
    snap.valid = true;
    return snap;
}

void AuraRackProcessor::loadFactoryPreset (int index)
{
    if (index < 0 || index >= getNumFactoryPresets()) return;
    auto snap = buildPresetSnapshot (aura::presets::factory()[(std::size_t) index]);
    std::lock_guard<std::mutex> lock (stateMutex);
    applySnapshotLocked (snap);
    presetIndex = index;
    presetBaseline = std::move (snap);
    apvts.state.setProperty (kPresetIndex, presetIndex, nullptr);
}

void AuraRackProcessor::clearRack()
{
    aura::presets::Preset empty { "Vide", "Empty", "", "", {}, {} };
    auto snap = buildPresetSnapshot (empty);
    std::lock_guard<std::mutex> lock (stateMutex);
    applySnapshotLocked (snap);
    presetIndex = -1;
    presetBaseline = {};
    apvts.state.setProperty (kPresetIndex, -1, nullptr);
}

bool AuraRackProcessor::isPresetModified() const
{
    if (presetIndex < 0 || ! presetBaseline.valid) return false;
    return ! snapshotsEqual (captureSnapshot(), presetBaseline);
}


//==============================================================================
void AuraRackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::lock_guard<std::mutex> lock (stateMutex);
    auto state = apvts.copyState();
    state.setProperty (kOrderProperty, orderToString (engine.getOrder()), nullptr);
    state.setProperty (kVersionProperty, kStateVersion, nullptr);

    // A / B : l'emplacement actif reflète l'état courant ; les deux sont enregistrés avec la session.
    abSlots[(std::size_t) abActive] = captureSnapshot();
    state.removeChild (state.getChildWithName (kAbNode), nullptr);
    juce::ValueTree ab (kAbNode);
    ab.setProperty (kAbActive, abActive, nullptr);
    for (int i = 0; i < 2; ++i)
    {
        const auto& snap = abSlots[(std::size_t) i];
        if (! snap.valid) continue;
        juce::ValueTree node (kSnapNode);
        node.setProperty ("slot", i, nullptr);
        node.setProperty (kOrderProperty, orderToString (snap.order), nullptr);
        for (const auto& [id, v] : snap.values)
        {
            juce::ValueTree vt (kValueNode);
            vt.setProperty ("id", id, nullptr);
            vt.setProperty ("v", (double) v, nullptr);
            node.appendChild (vt, nullptr);
        }
        ab.appendChild (node, nullptr);
    }
    state.appendChild (ab, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AuraRackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    std::lock_guard<std::mutex> lock (stateMutex);
    auto tree = juce::ValueTree::fromXml (*xml);

    // A / B : lus puis retirés de l'arbre des paramètres.
    abSlots = {};
    abActive = 0;
    if (auto ab = tree.getChildWithName (kAbNode); ab.isValid())
    {
        abActive = juce::jlimit (0, 1, (int) ab.getProperty (kAbActive, 0));
        for (auto node : ab)
        {
            const int slotIndex = juce::jlimit (0, 1, (int) node.getProperty ("slot", 0));
            RackSnapshot snap;
            for (auto vt : node)
                snap.values.emplace_back (vt.getProperty ("id").toString(), (float) (double) vt.getProperty ("v", 0.0));
            if (auto order = orderFromString (node.getProperty (kOrderProperty).toString())) snap.order = *order;
            snap.valid = ! snap.values.empty();
            abSlots[(std::size_t) slotIndex] = std::move (snap);
        }
        tree.removeChild (ab, nullptr);
    }
    apvts.replaceState (tree);

    // Les paramètres génériques viennent de la session : on installe les modules sans les remettre par défaut.
    syncModulesFromParameters (false);

    if (auto order = orderFromString (tree.getProperty (kOrderProperty).toString()))
        engine.setOrder (*order);
    else
        engine.setOrder (aura::RackRouting::identity());

    engine.setLoudnessEnabled (getUiSettings().loudness);

    presetIndex = juce::jlimit (-1, getNumFactoryPresets() - 1, (int) tree.getProperty (kPresetIndex, -1));
    presetBaseline = presetIndex >= 0 ? buildPresetSnapshot (aura::presets::factory()[(std::size_t) presetIndex]) : RackSnapshot {};
    setLatencySamples (engine.getRequestedLatency());
}

//==============================================================================
juce::AudioProcessorEditor* AuraRackProcessor::createEditor()
{
    return new AuraRackEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuraRackProcessor();
}
