// AuraRack — NextDSP
// Test de l'éditeur sans fenêtre : glisser-déposer des slots, resynchronisation sur l'ordre du moteur,
// puis rendu de l'interface en PNG (argument facultatif : dossier de sortie).

#include "../Source/PluginEditor.h"

#include <cstdio>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* what)
{
    ++checks;
    if (! ok) ++failures;
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}

juce::MouseEvent mouseEvent (juce::Component& on, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
{
    return { juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &on, &on, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1, dragged };
}

void renderAudio (AuraRackProcessor& p, double fs, int blocks)
{
    juce::AudioBuffer<double> buf (4, 512);   // 2 canaux + bus Sidechain stéréo
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (double) (b * 512 + i) / fs;
            const double kick = (std::fmod (t, 0.5) < 0.1) ? 0.9 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t) : 0.0;
            const double music = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) + 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * t);
            buf.setSample (0, i, music + kick); buf.setSample (1, i, music - kick * 0.5); buf.setSample (2, i, kick); buf.setSample (3, i, kick);
        }
        p.processBlock (buf, midi);
    }
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // une panne doit laisser la trace de la dernière étape
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::puts ("AuraRack — test de l'éditeur");

    const double fs = 384000.0;
    AuraRackProcessor processor;
    processor.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    processor.setPlayConfigDetails (4, 2, fs, 512);
    processor.prepareToPlay (fs, 512);

    auto setParam = [&] (const juce::String& id, float plain)
    {
        auto* p = processor.getValueTreeState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    auto setNormalised = [&] (const juce::String& id, float n) { processor.getValueTreeState().getParameter (id)->setValueNotifyingHost (n); };

    using aura::ModuleType;
    const ModuleType layout[] = { ModuleType::eq, ModuleType::comp, ModuleType::comp, ModuleType::comp, ModuleType::duck,
                                  ModuleType::clip, ModuleType::limit, ModuleType::delay, ModuleType::verb, ModuleType::w9b };
    for (int s = 0; s < 10; ++s)
        processor.setSlotModule (s, layout[s]);
    // Slot 2 : Aura-Tube ; slot 3 : L-Comp (choix d'algorithme = paramètre 1, normalisé 0 / 0,5 / 1).
    setNormalised (aura::params::generic (2, aura::AuraComp::algorithm), 0.5f);
    setNormalised (aura::params::generic (3, aura::AuraComp::algorithm), 1.0f);
    setNormalised (aura::params::generic (1, aura::AuraComp::peakReduction), 0.75f);
    setNormalised (aura::params::generic (2, aura::AuraComp::threshold), 0.65f);
    setParam (aura::params::mix (7), 30.0f);
    using W = aura::AuraW9B;
    setNormalised (aura::params::generic (9, W::input), 0.55f);
    setNormalised (aura::params::generic (9, W::threshold), 0.55f);
    setNormalised (aura::params::generic (9, W::ratio), 0.60f);
    setNormalised (aura::params::generic (9, W::attack), 0.45f);
    setNormalised (aura::params::generic (9, W::release), 0.55f);
    setNormalised (aura::params::generic (9, W::output), 0.62f);
    renderAudio (processor, fs, 200);

    AuraRackEditor editor (processor);
    editor.setBounds (0, 0, AuraRackEditor::kDesignWidth, AuraRackEditor::kDesignHeight);
    for (int i = 0; i < 20; ++i) { renderAudio (processor, fs, 20); editor.tickForTesting (1.0f / 60.0f); }

    // --- Glisser-déposer : le slot 0 (position 0) est déposé sur la cellule de la position 3.
    auto* rack = dynamic_cast<aura::ui::RackView*> (editor.getChildComponent (0)->getChildComponent (1));
    check (rack != nullptr, "RackView trouvé dans l'éditeur");
    if (rack == nullptr) return 1;

    auto& slot0 = rack->getSlotComponent (0);
    const auto origin = juce::Point<float> (40.0f, 14.0f);   // dans l'en-tête, hors liste déroulante et bouton
    slot0.mouseDown (mouseEvent (slot0, origin, origin, false));
    const auto cell3Centre = rack->getSlotComponent (3).getBounds().getCentre().toFloat();
    const auto delta = cell3Centre - slot0.getBounds().getCentre().toFloat();
    const auto rackStart = slot0.getPosition().toFloat() + origin;   // souris, en coordonnées du rack
    for (int step = 1; step <= 10; ++step)
    {
        const auto mouseInRack = rackStart + delta * ((float) step / 10.0f);
        slot0.mouseDrag (mouseEvent (slot0, mouseInRack - slot0.getPosition().toFloat(), origin, true));   // position locale au slot, comme JUCE
        editor.tickForTesting (1.0f / 60.0f);
    }
    check (processor.getSlotOrder() == aura::RackRouting::identity(), "Pendant le glissement : l'ordre du moteur n'est pas modifié");
    slot0.mouseUp (mouseEvent (slot0, rackStart + delta - slot0.getPosition().toFloat(), origin, true));

    const auto expected = aura::RackRouting::moved (aura::RackRouting::identity(), 0, 3);
    check (processor.getSlotOrder() == expected, "Au relâchement : moveSlot(0 -> 3) appliqué au moteur");
    check (rack->getDisplayedPosition (0) == 3 && rack->getDisplayedPosition (1) == 0, "La vue affiche le nouvel ordre");
    for (int i = 0; i < 90; ++i) editor.tickForTesting (1.0f / 60.0f);
    check (rack->getSlotComponent (0).getBounds().getCentre().toFloat().getDistanceFrom (cell3Centre) < 2.0f, "Animation terminée : le slot 0 occupe la cellule 3");

    // --- Ordre modifié ailleurs (restauration d'état, annulation) : la vue se resynchronise sans verrou.
    processor.setSlotOrder ({ 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 });
    for (int i = 0; i < 120; ++i) editor.tickForTesting (1.0f / 60.0f);
    check (rack->getDisplayedPosition (9) == 0 && rack->getDisplayedPosition (0) == 9, "Ordre changé hors interface : la vue suit");

    // --- Rafale de déplacements depuis le thread de messages, interface qui tourne : la vue suit toujours l'ordre du moteur.
    {
        unsigned x = 17u;
        bool followed = true;
        for (int i = 0; i < 200; ++i)
        {
            x = x * 1664525u + 1013904223u;
            processor.moveSlot ((int) (x >> 8) % 10, (int) (x >> 16) % 10);
            editor.tickForTesting (1.0f / 60.0f);
        }
        for (int i = 0; i < 120; ++i) editor.tickForTesting (1.0f / 60.0f);
        const auto order = processor.getSlotOrder();
        for (int pos = 0; pos < 10; ++pos)
            followed = followed && rack->getDisplayedPosition (order[(std::size_t) pos]) == pos;
        check (aura::RackRouting::isValid (order) && followed, "200 déplacements enchaînés : permutation valide, vue synchronisée");
    }

    // --- Rendu PNG.
    processor.setSlotOrder (aura::RackRouting::identity());
    for (int i = 0; i < 120; ++i) { renderAudio (processor, fs, 8); editor.tickForTesting (1.0f / 60.0f); }
    if (argc > 1)
    {
        const juce::File dir { juce::String (juce::CharPointer_UTF8 (argv[1])) };
        dir.createDirectory();
        auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream out (dir.getChildFile ("auraRack-editor.png"));
        out.setPosition (0); out.truncate();
        png.writeImageToStream (image, out);
        // Façade W9BMK1 seule, mise en page large (façade de rack).
        {
            aura::ui::W9bFaceplate plate (processor.getValueTreeState(), 9);
            plate.setBounds (0, 0, aura::ui::W9bFaceplate::kWideWidth, aura::ui::W9bFaceplate::kWideHeight);
            for (int i = 0; i < 90; ++i) plate.tick (1.0f / 60.0f, 7.5f, 0.85f);
            auto wideImage = plate.createComponentSnapshot (plate.getLocalBounds(), true, 1.0f);
            juce::FileOutputStream wideOut (dir.getChildFile ("w9bmk1-wide.png"));
            wideOut.setPosition (0); wideOut.truncate();
            png.writeImageToStream (wideImage, wideOut);
        }
        std::printf ("  rendu : %s (%d x %d)\n", dir.getChildFile ("auraRack-editor.png").getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    }

    std::printf ("\n%d / %d vérifications réussies\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
