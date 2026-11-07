// AuraRack — NextDSP
// Hôte de test : charge le binaire AuraRack.vst3 comme le ferait un DAW et vérifie les bus (Sidechain),
// le traitement 64 bits, le changement de module, la latence annoncée, le ducking par clé externe
// et la sauvegarde / restauration de l'état.
//   aurarack_host_test <chemin/AuraRack.vst3>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdio>

namespace
{
int checks = 0, failures = 0;

void check (bool ok, const std::string& what, double value = 0.0)
{
    ++checks;
    if (! ok) ++failures;
    std::printf ("  [%s] %-70s %12.4f\n", ok ? " OK " : "FAIL", what.c_str(), value);
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* param : p.getParameters())
        if (param->getName (128) == name) return param;
    return nullptr;
}

void setParam (juce::AudioPluginInstance& p, const juce::String& name, float normalised)
{
    if (auto* param = findParam (p, name))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (normalised);
        param->endChangeGesture();
    }
    else
        check (false, "Paramètre introuvable : " + name.toStdString());
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

/** Traite `total` échantillons en double précision, par blocs de 512, avec bus Sidechain éventuel. */
void render (juce::AudioPluginInstance& p, std::vector<double>& L, std::vector<double>& R,
             const std::vector<double>* key, int numChannelsTotal)
{
    const int block = 512;
    juce::AudioBuffer<double> buf (numChannelsTotal, block);
    juce::MidiBuffer midi;
    for (std::size_t off = 0; off < L.size(); off += block)
    {
        const int n = (int) std::min<std::size_t> (block, L.size() - off);
        buf.setSize (numChannelsTotal, n, false, false, true);
        buf.clear();
        for (int i = 0; i < n; ++i)
        {
            buf.setSample (0, i, L[off + (std::size_t) i]);
            buf.setSample (1, i, R[off + (std::size_t) i]);
            if (key != nullptr && numChannelsTotal >= 4)
            {
                buf.setSample (2, i, (*key)[off + (std::size_t) i]);
                buf.setSample (3, i, (*key)[off + (std::size_t) i]);
            }
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < n; ++i)
        {
            L[off + (std::size_t) i] = buf.getSample (0, i);
            R[off + (std::size_t) i] = buf.getSample (1, i);
        }
    }
}

double toneAmp (const std::vector<double>& x, std::size_t a, std::size_t len, double f, double fs)
{
    double s = 0.0, c = 0.0;
    for (std::size_t i = a; i < a + len; ++i)
    {
        const double ph = juce::MathConstants<double>::twoPi * f * (double) i / fs;
        s += x[i] * std::sin (ph);
        c += x[i] * std::cos (ph);
    }
    return 2.0 * std::sqrt (s * s + c * c) / (double) len;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::puts ("usage : aurarack_host_test <AuraRack.vst3>"); return 2; }
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::puts ("AuraRack — hôte de test VST3");
    const double fs = 352800.0;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, juce::String (juce::CharPointer_UTF8 (argv[1])));
    check (found.size() == 1, "Binaire VST3 reconnu", found.size());
    if (found.isEmpty()) return 1;

    juce::String error;
    auto plugin = format.createInstanceFromDescription (*found[0], fs, 512, error);
    check (plugin != nullptr, "Instanciation (" + found[0]->name.toStdString() + ", " + found[0]->manufacturerName.toStdString() + ")");
    if (plugin == nullptr) { std::printf ("%s\n", error.toRawUTF8()); return 1; }

    // Bus : principal stéréo + Sidechain stéréo activé.
    check (plugin->getBusCount (true) == 2, "Deux bus d'entrée (principal + Sidechain)", plugin->getBusCount (true));
    auto layout = plugin->getBusesLayout();
    if (layout.inputBuses.size() == 2) layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    check (plugin->setBusesLayout (layout), "Disposition stéréo + Sidechain stéréo acceptée");
    check (plugin->supportsDoublePrecisionProcessing(), "Traitement 64 bits annoncé");
    plugin->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    check (findParam (*plugin, "Slot 10 P9") != nullptr && findParam (*plugin, "Sidechain Source") != nullptr,
           "Paramètres exposés (globaux + 10 slots)", plugin->getParameters().size());

    // Slot 1 = AuraLimit, Slot 2 = AuraEQ (latence nulle). Le type est appliqué par le message thread du plugin.
    setParam (*plugin, "Slot 1 Type", 5.0f / 7.0f);
    setParam (*plugin, "Slot 2 Type", 1.0f / 7.0f);
    pump (200);
    plugin->prepareToPlay (fs, 512);
    pump (100);
    const int latency = plugin->getLatencySamples();
    const int expected = (int) std::lround (1.5e-3 * fs) + 17;
    check (latency == expected, "Latence annoncée à l'hôte = AuraLimit (1,5 ms + détection)", latency);

    const int channels = plugin->getTotalNumInputChannels();
    std::vector<double> L (8192, 0.0), R (8192, 0.0);
    L[1000] = R[1000] = 0.25;
    render (*plugin, L, R, nullptr, channels);
    std::size_t peakAt = 0;
    for (std::size_t i = 0; i < L.size(); ++i) if (std::abs (L[i]) > std::abs (L[peakAt])) peakAt = i;
    const double atLatency = L[(std::size_t) (1000 + latency)];
    if (std::abs (atLatency - 0.25) >= 1e-12)
    {
        std::printf ("    valeur = %.17g, pic à l'échantillon %zu (%.17g), attendu %d\n", atLatency, peakAt, L[peakAt], 1000 + latency);
        for (std::size_t i = peakAt - 3; i <= peakAt + 3; ++i) std::printf ("      L[%zu] = %.17g   R = %.17g\n", i, L[i], R[i]);
        for (auto* param : plugin->getParameters())
        {
            const auto name = param->getName (64);
            if (name.startsWith ("Slot 1") || name.startsWith ("Slot 2") || ! name.startsWith ("Slot"))
                std::printf ("      %-22s %.9g  (%s)\n", name.toRawUTF8(), (double) param->getValue(), param->getCurrentValueAsText().toRawUTF8());
        }
    }
    check (std::abs (atLatency - 0.25) < 1e-12, "Impulsion 64 bits intacte, à la latence annoncée", atLatency);

    // Slot 3 = AuraDuck (Dynamic EQ, clé = bus Sidechain) : un kick sur le Sidechain baisse le sub.
    setParam (*plugin, "Slot 1 Type", 0.0f);
    setParam (*plugin, "Slot 3 Type", 3.0f / 7.0f);
    pump (200);
    const auto n = (std::size_t) (fs * 0.6);
    std::vector<double> mainL (n), mainR (n), key (n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        mainL[i] = mainR[i] = 0.5 * std::sin (juce::MathConstants<double>::twoPi * 45.0 * (double) i / fs);
        const double t = (double) i / fs - 0.3;
        if (t >= 0.0 && t < 0.15) key[i] = 0.8 * std::sin (juce::MathConstants<double>::twoPi * 60.0 * t);
    }
    render (*plugin, mainL, mainR, &key, channels);
    const auto per = (std::size_t) std::lround (fs / 45.0);
    const double before = juce::Decibels::gainToDecibels (toneAmp (mainL, (std::size_t) (0.2 * fs), per * 4, 45.0, fs) / 0.5);
    const double during = juce::Decibels::gainToDecibels (toneAmp (mainL, (std::size_t) (0.36 * fs), per * 3, 45.0, fs) / 0.5);
    check (std::abs (before) < 0.1, "Sub 45 Hz sans kick sur le Sidechain", before);
    check (during < -9.0, "Sub 45 Hz pendant le kick du bus Sidechain (Depth 12 dB)", during);

    // État : sauvegarde, nouvelle instance, restauration, même rendu.
    setParam (*plugin, "Slot 2 P4", 0.8f);   // Mid Gain de l'EQ
    pump (100);
    juce::MemoryBlock state;
    plugin->getStateInformation (state);

    auto restored = format.createInstanceFromDescription (*found[0], fs, 512, error);
    restored->setBusesLayout (layout);
    restored->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    restored->setStateInformation (state.getData(), (int) state.getSize());
    pump (200);
    restored->prepareToPlay (fs, 512);
    plugin->prepareToPlay (fs, 512);
    pump (100);
    check (std::abs (findParam (*restored, "Slot 2 P4")->getValue() - 0.8f) < 1e-6f, "Paramètre générique restauré", findParam (*restored, "Slot 2 P4")->getValue());
    check (findParam (*restored, "Slot 2 P4")->getCurrentValueAsText().contains ("dB"), "Texte du paramètre suivant le module : " + findParam (*restored, "Slot 2 P4")->getCurrentValueAsText().toStdString());

    // Référence : instance neuve configurée par paramètres (et non par état), préparée une seule fois.
    auto fresh = format.createInstanceFromDescription (*found[0], fs, 512, error);
    fresh->setBusesLayout (layout);
    fresh->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    setParam (*fresh, "Slot 2 Type", 1.0f / 7.0f);
    setParam (*fresh, "Slot 3 Type", 3.0f / 7.0f);
    pump (200);
    setParam (*fresh, "Slot 2 P4", 0.8f);
    fresh->prepareToPlay (fs, 512);
    pump (100);

    std::vector<double> a (n), b (n), c (n), d (n);
    for (std::size_t i = 0; i < n; ++i) a[i] = b[i] = c[i] = d[i] = 0.3 * std::sin (0.01 * (double) i);
    render (*fresh, a, b, nullptr, channels);
    render (*restored, c, d, nullptr, channels);
    double diff = 0.0;
    for (std::size_t i = 0; i < n; ++i) diff = std::max (diff, std::abs (a[i] - c[i]));
    check (! (diff > 0.0), "Instance restaurée : rendu bit-identique à une instance neuve réglée pareil", diff);

    // reset() (stop / repositionnement de l'hôte) : l'original repart d'un état propre, même rendu.
    plugin->reset();
    std::vector<double> e (n), f (n);
    for (std::size_t i = 0; i < n; ++i) e[i] = f[i] = 0.3 * std::sin (0.01 * (double) i);
    render (*plugin, e, f, nullptr, channels);
    double diffReset = 0.0;
    for (std::size_t i = 0; i < n; ++i) diffReset = std::max (diffReset, std::abs (a[i] - e[i]));
    check (diffReset < 1e-9, "reset() de l'hôte : état vidé, rendu identique", diffReset);

    fresh->releaseResources();
    fresh.reset();
    plugin->releaseResources();
    restored->releaseResources();
    plugin.reset();
    restored.reset();

    std::printf ("\n%d / %d vérifications réussies\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
