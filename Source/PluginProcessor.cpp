// AuraRack — NextDSP

#include "PluginProcessor.h"

#include <optional>

namespace
{
    constexpr int kStateVersion = 1;
    const juce::Identifier kOrderProperty { "order" };
    const juce::Identifier kVersionProperty { "auraRackVersion" };

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
        for (int p = 0; p < kMaxModuleParams; ++p)
            src.params[(std::size_t) p] = raw (params::generic (s, p));
        engine.bindSlot (s, src);
    }

    engine.bindGlobal ({ raw (params::inGain), raw (params::outGain), raw (params::masterMix), raw (params::scSource) });
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
    auto order = engine.getOrder();
    if (fromPosition < 0 || fromPosition >= aura::kNumSlots || toPosition < 0 || toPosition >= aura::kNumSlots
        || fromPosition == toPosition)
        return;

    const int moved = order[(std::size_t) fromPosition];
    if (fromPosition < toPosition)
        std::copy (order.begin() + fromPosition + 1, order.begin() + toPosition + 1, order.begin() + fromPosition);
    else
        std::copy_backward (order.begin() + toPosition, order.begin() + fromPosition, order.begin() + fromPosition + 1);
    order[(std::size_t) toPosition] = moved;
    setSlotOrder (order);
}

//==============================================================================
void AuraRackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (kOrderProperty, orderToString (engine.getOrder()), nullptr);
    state.setProperty (kVersionProperty, kStateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AuraRackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    std::lock_guard<std::mutex> lock (stateMutex);
    const auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);

    // Les paramètres génériques viennent de la session : on installe les modules sans les remettre par défaut.
    syncModulesFromParameters (false);

    if (auto order = orderFromString (tree.getProperty (kOrderProperty).toString()))
        engine.setOrder (*order);
    else
        engine.setOrder (aura::RackRouting::identity());

    setLatencySamples (engine.getRequestedLatency());
}

//==============================================================================
juce::AudioProcessorEditor* AuraRackProcessor::createEditor()
{
    // Interface provisoire (Étape 3 : PluginEditor NextDSP avec glisser-déposer des slots).
    return new juce::GenericAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuraRackProcessor();
}
