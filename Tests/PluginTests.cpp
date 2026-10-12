// Tests du plugin complet (processeur JUCE + interface), sans DAW.
// Usage : PhractalPluginTests [dossier où écrire les captures de l'interface]

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

static int failures = 0;

static void check (bool condition, const juce::String& message)
{
    if (! condition)
    {
        ++failures;
        std::cout << "  ECHEC : " << message << std::endl;
    }
}

using Ids = PhractalAudioProcessor;

struct TestPlayHead final : juce::AudioPlayHead
{
    double bpm = 120.0;
    bool playing = true;
    juce::Optional<double> beats;   // position en noires, si l'hôte la fournit
    juce::Optional<double> barStart;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setPpqPosition (beats);
        info.setPpqPositionOfLastBarStart (barStart);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};

static PhractalAudioProcessor& phractal (juce::AudioProcessor& p)    { return dynamic_cast<PhractalAudioProcessor&> (p); }

static void setParam (juce::AudioProcessor& p, const juce::String& id, float realValue)
{
    auto* param = phractal (p).apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

static float getParam (juce::AudioProcessor& p, const juce::String& id)
{
    return phractal (p).apvts.getRawParameterValue (id)->load();
}

/** Fait passer "in" dans le plugin bloc par bloc ; "onBlock" est appelé avant chaque bloc (pour déplacer la tête de l'hôte). */
static void process (juce::AudioProcessor& p, const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out, int blockSize,
                     const std::function<void (int)>& onBlock = {})
{
    out.makeCopyOf (in);

    for (int start = 0; start < in.getNumSamples(); start += blockSize)
    {
        const int n = juce::jmin (blockSize, in.getNumSamples() - start);
        juce::MidiBuffer midi;

        if (onBlock)
            onBlock (start);

        juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), out.getNumChannels(), start, n);
        p.processBlock (block, midi);
    }
}

static juce::AudioBuffer<float> makeNoise (int numChannels, int numSamples, int seed, float amplitude = 0.5f)
{
    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    juce::Random rng (seed);

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (ch, i, amplitude * (rng.nextFloat() * 2.0f - 1.0f));

    return buffer;
}

static std::unique_ptr<juce::AudioProcessor> makePlugin (double sampleRate, int blockSize, int numChannels = 2)
{
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());

    if (numChannels == 1)
    {
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p->setBusesLayout (mono), "le plugin refuse le mono");
    }

    p->setRateAndBufferSizeDetails (sampleRate, blockSize);
    p->prepareToPlay (sampleRate, blockSize);
    return p;
}

static bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from, int to)
{
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = from; i < to; ++i)
            if (! juce::exactlyEqual (a.getSample (ch, i), b.getSample (ch, i)))
                return false;

    return true;
}

static bool allFinite (const juce::AudioBuffer<float>& buffer, float limit = 1.0e6f)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (ch, i)) || std::abs (buffer.getSample (ch, i)) > limit)
                return false;

    return true;
}

//==============================================================================
static void testParameters()
{
    std::cout << "- parametres, listes de longueurs" << std::endl;

    auto p = makePlugin (48000.0, 512);
    auto& params = p->getParameters();
    juce::StringArray ids;

    for (auto* parameter : params)
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            ids.add (withId->paramID);

    check (ids.size() == params.size(), "un parametre sans identifiant");
    check (ids.size() == 96, "nombre de parametres : " + juce::String (ids.size()));

    juce::StringArray unique (ids);
    unique.removeDuplicates (false);
    check (unique.size() == ids.size(), "identifiants en double");

    for (int step = 0; step < Ids::numSteps; ++step)
    {
        check (ids.contains (Ids::chanceId (step)), "manque " + Ids::chanceId (step));
        check (ids.contains (Ids::gateStepId (step)), "manque " + Ids::gateStepId (step));
    }

    // Longueurs en SYNC : 37 valeurs, de 1/1 à 1/4096, de la plus longue à la plus courte.
    const auto& names = Ids::getDivisionNames();
    check (names.size() == 37, "nombre de divisions : " + juce::String (names.size()));
    check (names[0] == "1/1" && names[names.size() - 1] == "1/4096", "bornes : " + names[0] + " / " + names[names.size() - 1]);
    check (names[12] == "1/16" && names[18] == "1/64", "1/16 et 1/64 pas a leur place");
    check (names[13] == "1/32D" && names[14] == "1/16T", "ordre pointee / triolet");

    bool decreasing = true;

    for (int i = 1; i < names.size(); ++i)
        decreasing = decreasing && Ids::getDivisionInBeats (i) < Ids::getDivisionInBeats (i - 1);

    check (decreasing, "les divisions ne raccourcissent pas a chaque cran");
    check (std::abs (Ids::getDivisionInBeats (36) - 4.0 / 4096.0) < 1.0e-12, "1/4096 : mauvaise longueur");
    check (std::abs (Ids::getDivisionInBeats (14) - 1.0 / 6.0) < 1.0e-12, "1/16T : mauvaise longueur");
    check (std::abs (Ids::getDivisionInBeats (13) - 0.1875) < 1.0e-12, "1/32D : mauvaise longueur");

    int indices[64];
    check (Ids::collectDivisions (12, 18, true, true, true, indices) == 7, "1/16 a 1/64, tout : 7 valeurs");
    check (Ids::collectDivisions (12, 18, true, false, false, indices) == 3, "1/16 a 1/64, droites seules : 3 valeurs");
    check (Ids::collectDivisions (18, 12, true, false, false, indices) == 3, "bornes inversees");
    check (Ids::collectDivisions (14, 17, true, false, false, indices) == 3 && indices[0] == 14 && indices[2] == 17,
           "les bornes restent dans le tirage meme si leur variante est exclue");
    check (Ids::collectDivisions (12, 18, false, true, true, indices) == 1 && indices[0] == 12, "sans tirage : MAX seul");

    // Textes
    check (Ids::pitchToText (7.0f) == "+7 st" && Ids::pitchToText (-12.0f) == "-12 st" && Ids::pitchToText (3.25f) == "+3.25 st", "texte du pitch");

    auto* maxTime = phractal (*p).apvts.getParameter (Ids::maxTimeId);
    check (maxTime->getText (maxTime->convertTo0to1 (0.31f), 32) == "0.31 ms", "texte d'une duree courte : " + maxTime->getText (maxTime->convertTo0to1 (0.31f), 32));
    check (juce::approximatelyEqual (maxTime->convertFrom0to1 (0.0f), 1000.0f) && juce::approximatelyEqual (maxTime->convertFrom0to1 (1.0f), 0.1f),
           "potard des durees : a droite = plus court");
}

static void testPassthrough()
{
    std::cout << "- reglages par defaut : le son passe intact" << std::endl;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (int block : { 64, 441, 512 })
        {
            auto p = makePlugin (sr, block);
            TestPlayHead head;
            p->setPlayHead (&head);

            const auto in = makeNoise (2, (int) sr, 1);
            juce::AudioBuffer<float> out;
            process (*p, in, out, block, [&head, sr] (int sample) { head.beats = sample / sr * 2.0; });
            check (identical (in, out, 0, in.getNumSamples()), "par defaut, " + juce::String (sr) + " Hz, blocs de " + juce::String (block));
        }
    }

    auto mono = makePlugin (48000.0, 256, 1);
    const auto in = makeNoise (1, 48000, 2);
    juce::AudioBuffer<float> out;
    process (*mono, in, out, 256);
    check (identical (in, out, 0, in.getNumSamples()), "mono, par defaut");
}

static void testSlicesOnTheGrid()
{
    std::cout << "- slices calees sur la mesure de l'hote" << std::endl;

    const double sr = 48000.0;
    auto p = makePlugin (sr, 480);
    TestPlayHead head;
    head.bpm = 120.0;                 // une noire = 24 000 échantillons
    p->setPlayHead (&head);

    setParam (*p, Ids::sliceId, 1.0f);
    setParam (*p, Ids::stepsId, 4.0f);   // un pas par noire
    setParam (*p, Ids::thruId, 0.0f);    // silence entre les slices

    for (int step = 0; step < Ids::numSteps; ++step)
        setParam (*p, Ids::chanceId (step), step == 1 ? 100.0f : 0.0f);   // seul le 2e temps

    setParam (*p, Ids::randomId, 0.0f);
    setParam (*p, Ids::maxDivisionId, 12.0f);   // 1/16 = 6000 échantillons
    setParam (*p, Ids::attackId, 0.0f);
    setParam (*p, Ids::sustainId, 100.0f);
    setParam (*p, Ids::releaseId, 0.0f);

    const auto in = makeNoise (2, 24000 * 8, 3);
    juce::AudioBuffer<float> out;

    // L'hôte démarre au début d'une mesure (barre à 0, puis à 4 noires, etc.).
    process (*p, in, out, 480, [&head, sr] (int sample)
    {
        const double beats = sample / sr * 2.0;
        head.beats = beats;
        head.barStart = 4.0 * std::floor (beats / 4.0);
    });

    // Temps 1 : silence. Temps 2 : la slice (premier passage = l'entrée, puis répétitions de 6000 échantillons).
    check (out.getMagnitude (0, 200, 23700) <= 0.0f, "temps 1 : du son alors que la chance est nulle");

    int mismatches = 0;

    for (int i = 24000 + 200; i < 24000 + 5900; ++i)
        mismatches += std::abs (out.getSample (0, i) - in.getSample (0, i)) > 1.0e-6f;

    for (int i = 24000 + 6000 + 200; i < 24000 + 6000 + 5900; ++i)
        mismatches += std::abs (out.getSample (0, i) - in.getSample (0, i - 6000)) > 1.0e-6f;

    check (mismatches == 0, "temps 2 : la slice ne rejoue pas son premier passage (" + juce::String (mismatches) + ")");
    check (out.getMagnitude (0, 48000 + 200, 47700) <= 0.0f, "temps 3 et 4 : du son");
    check (out.getMagnitude (0, 96000 + 24000 + 200, 4800) > 0.1f, "mesure 2 : pas de slice au 2e temps");

    // La vue de l'interface : le pas 1 est marqué comme slice.
    check ((phractal (*p).getEngine().getSlicedMask() & 2u) != 0, "le pas 1 n'est pas marque comme slice");
}

static void testGateAndOutput()
{
    std::cout << "- gate, mix, niveau, limiteur" << std::endl;

    const double sr = 48000.0;
    auto p = makePlugin (sr, 512);
    TestPlayHead head;
    head.bpm = 120.0;
    p->setPlayHead (&head);

    // Gate sur 1/16 (6000 échantillons), motif x.xx, ouvert 50 % du pas, sans lissage.
    setParam (*p, Ids::gateId, 1.0f);
    setParam (*p, Ids::gateDivisionId, 4.0f);
    setParam (*p, Ids::gateStepsId, 4.0f);
    setParam (*p, Ids::gateLengthId, 50.0f);
    setParam (*p, Ids::gateSmoothId, 0.0f);

    juce::AudioBuffer<float> dc (2, 48000);

    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, 48000);

    juce::AudioBuffer<float> out;
    process (*p, dc, out, 512, [&head, sr] (int sample) { head.beats = sample / sr * 2.0; });

    check (std::abs (out.getSample (0, 1500) - 0.5f) < 1.0e-5f, "gate : pas 0 ouvert");
    check (std::abs (out.getSample (0, 4500)) < 1.0e-5f, "gate : fin du pas 0 fermee (longueur 50 %)");
    check (std::abs (out.getSample (0, 7500)) < 1.0e-5f, "gate : pas 1 ferme (motif x.xx)");
    check (std::abs (out.getSample (0, 13500) - 0.5f) < 1.0e-5f, "gate : pas 2 ouvert");

    // Mix à 0 % : l'entrée, quel que soit le gate.
    setParam (*p, Ids::mixId, 0.0f);
    process (*p, dc, out, 512, [&head, sr] (int sample) { head.beats = 1.0 + sample / sr * 2.0; });
    check (std::abs (out.getSample (0, 40000) - 0.5f) < 1.0e-5f, "mix 0 % : l'entree");

    // Niveau +6 dB et limiteur : 0,5 x 2 = 1, pas au-dessus de 0 dBFS.
    setParam (*p, Ids::levelId, 12.0f);
    process (*p, dc, out, 512);
    check (out.getMagnitude (0, 20000, 28000) <= 1.0001f, "limiteur : depassement de 0 dBFS");
    check (out.getMagnitude (0, 20000, 28000) > 0.99f, "limiteur : le niveau s'est effondre");

    setParam (*p, Ids::limiterId, 0.0f);
    process (*p, dc, out, 512);
    check (std::abs (out.getSample (0, 40000) - 0.5f * juce::Decibels::decibelsToGain (12.0f)) < 1.0e-3f, "niveau +12 dB sans limiteur");
}

static void testPitchAndMicroLoops()
{
    std::cout << "- micro-boucles et pitch dans le plugin" << std::endl;

    const double sr = 48000.0;
    auto p = makePlugin (sr, 512, 1);
    TestPlayHead head;
    head.bpm = 190.0;
    p->setPlayHead (&head);

    setParam (*p, Ids::sliceId, 1.0f);
    setParam (*p, Ids::stepsId, 1.0f);
    setParam (*p, Ids::thruId, 0.0f);

    for (int step = 0; step < Ids::numSteps; ++step)
        setParam (*p, Ids::chanceId (step), 100.0f);

    setParam (*p, Ids::randomId, 0.0f);
    setParam (*p, Ids::maxDivisionId, 27.0f);   // 1/512 à 190 BPM : 2,47 ms, environ 405 Hz
    setParam (*p, Ids::releaseId, 0.0f);

    const auto in = makeNoise (1, 48000, 4);
    juce::AudioBuffer<float> out;
    process (*p, in, out, 512, [&head, sr] (int sample) { head.beats = sample / sr * 190.0 / 60.0; head.barStart = 0.0; });

    // Période dominante (autocorrélation) au milieu de la mesure.
    const auto period = [] (const juce::AudioBuffer<float>& b, int from, int to, int lo, int hi)
    {
        double bestScore = -2.0;
        int best = lo;

        for (int lag = lo; lag <= hi; ++lag)
        {
            double dot = 0.0, e1 = 0.0, e2 = 0.0;

            for (int i = from; i < to; ++i)
            {
                const double x = b.getSample (0, i), y = b.getSample (0, i + lag);
                dot += x * y;
                e1 += x * x;
                e2 += y * y;
            }

            const double score = dot / std::sqrt (e1 * e2 + 1.0e-20);

            if (score > bestScore)
            {
                bestScore = score;
                best = lag;
            }
        }

        return best;
    };

    const double expected = 4.0 / 512.0 * 60.0 / 190.0 * sr;   // 118,4 échantillons
    const int measured = period (out, 20000, 24000, 60, 200);
    check (std::abs (measured - expected) <= 1.5, "1/512 a 190 BPM : periode " + juce::String (measured) + " au lieu de " + juce::String (expected, 1));

    // Pitch +12 : la note de la micro-boucle monte d'une octave.
    setParam (*p, Ids::pitchOnId, 1.0f);
    setParam (*p, Ids::pitchId, 12.0f);
    process (*p, in, out, 512, [&head, sr] (int sample) { head.beats = sample / sr * 190.0 / 60.0; head.barStart = 0.0; });
    const int shifted = period (out, 20000, 24000, 30, 100);
    check (std::abs (shifted - expected * 0.5) <= 1.5, "pitch +12 : periode " + juce::String (shifted) + " au lieu de " + juce::String (expected * 0.5, 1));

    // En SEMI, une valeur décimale est arrondie : +11,6 sonne comme +12.
    setParam (*p, Ids::pitchId, 11.6f);
    process (*p, in, out, 512, [&head, sr] (int sample) { head.beats = sample / sr * 190.0 / 60.0; head.barStart = 0.0; });
    check (std::abs (period (out, 20000, 24000, 30, 100) - expected * 0.5) <= 1.5, "SEMI : +11,6 n'est pas arrondi a +12");
}

static void testStateAndBypass()
{
    std::cout << "- sauvegarde, bypass, reglages au hasard" << std::endl;

    auto p = makePlugin (48000.0, 512);
    setParam (*p, Ids::sliceId, 1.0f);
    setParam (*p, Ids::chanceId (7), 13.0f);
    setParam (*p, Ids::gateStepId (31), 0.0f);
    setParam (*p, Ids::maxDivisionId, 30.0f);
    setParam (*p, Ids::pitchId, -7.25f);
    setParam (*p, Ids::levelId, -3.5f);
    phractal (*p).apvts.state.setProperty ("editorWidth", 900, nullptr);

    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto q = makePlugin (44100.0, 256);
    q->setStateInformation (state.getData(), (int) state.getSize());

    check (juce::approximatelyEqual (getParam (*q, Ids::chanceId (7)), 13.0f), "chance du pas 8 non restauree");
    check (getParam (*q, Ids::gateStepId (31)) < 0.5f, "pas 32 du gate non restaure");
    check (juce::roundToInt (getParam (*q, Ids::maxDivisionId)) == 30, "MAX non restaure");
    check (std::abs (getParam (*q, Ids::pitchId) + 7.25f) < 0.001f, "pitch non restaure");
    check (std::abs (getParam (*q, Ids::levelId) + 3.5f) < 0.001f, "niveau non restaure");
    check ((int) phractal (*q).apvts.state.getProperty ("editorWidth") == 900, "taille de l'interface non restauree");

    // États abîmés : ignorés sans planter.
    const char junk[] = "pas un etat";
    q->setStateInformation (junk, (int) sizeof (junk));
    q->setStateInformation (state.getData(), 10);
    check (juce::approximatelyEqual (getParam (*q, Ids::chanceId (7)), 13.0f), "un etat abime a efface les reglages");

    // Bypass : l'entrée passe, puis on repart proprement.
    const auto in = makeNoise (2, 4800, 5);
    juce::AudioBuffer<float> out (in);
    juce::MidiBuffer midi;
    q->processBlockBypassed (out, midi);
    check (identical (in, out, 0, 4800), "bypass : l'entree n'est pas intacte");

    // Réglages au hasard, toutes fréquences : rien d'aberrant.
    juce::Random rng (77);

    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        auto r = makePlugin (sr, 333);
        TestPlayHead head;
        head.bpm = 60.0 + rng.nextFloat() * 160.0;
        r->setPlayHead (&head);
        auto& params = r->getParameters();
        const auto noise = makeNoise (2, (int) sr * 2, 6, 0.9f);
        juce::AudioBuffer<float> result;
        int blocks = 0;

        process (*r, noise, result, 333, [&] (int sample)
        {
            head.beats = sample / sr * head.bpm / 60.0;
            head.playing = (blocks / 50) % 2 == 0;

            if (blocks++ % 25 == 0)
            {
                for (int k = 0; k < 12; ++k)
                    params[rng.nextInt (params.size())]->setValueNotifyingHost (rng.nextFloat());

                setParam (*r, Ids::limiterId, 1.0f);   // le limiteur garantit la crête mesurée ci-dessous
            }
        });

        check (allFinite (result, 1.01f), "reglages au hasard a " + juce::String (sr) + " Hz : valeurs aberrantes (limiteur allume ?)");
    }
}

static void testEditor (const juce::File& outputDir)
{
    std::cout << "- interface" << std::endl;

    auto p = makePlugin (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    auto* phractalEditor = dynamic_cast<PhractalAudioProcessorEditor*> (editor.get());
    check (phractalEditor != nullptr, "pas d'editeur Phractal");

    if (phractalEditor == nullptr)
        return;

    check (editor->getWidth() == PhractalFacade::width && editor->getHeight() == PhractalFacade::height,
           "taille de l'editeur : " + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()));
    auto& facade = phractalEditor->getFacade();

    const auto find = [&facade] (const juce::String& id) -> juce::Component*
    {
        for (auto* child : facade.getChildren())
            if (child->getComponentID() == id)
                return child;

        return nullptr;
    };

    // Chaque contrôle tient dans la façade, sans chevaucher un autre contrôle visible (sauf les potards superposés).
    for (auto* a : facade.getChildren())
    {
        check (facade.getLocalBounds().contains (a->getBounds()), a->getComponentID() + " deborde");

        for (auto* b : facade.getChildren())
            if (a < b && a->isVisible() && b->isVisible() && a->getBounds().intersects (b->getBounds()))
                check (false, a->getComponentID() + " chevauche " + b->getComponentID());
    }

    // Un clic sur la rangée CHANCE règle le pas visé ; un clic sur le gate inverse un pas.
    auto* lane = dynamic_cast<StepLane*> (find ("chances"));
    auto* gateLane = dynamic_cast<StepLane*> (find ("gatePattern"));
    check (lane != nullptr && gateLane != nullptr, "rangees de pas introuvables");

    if (lane != nullptr && gateLane != nullptr)
    {
        check (lane->getNumShownSteps() == 16, "16 pas affiches par defaut");

        // Pas 3 (sur 16), tout en haut : 100 %.
        const auto area = lane->getLocalBounds().toFloat().reduced (6.0f, 5.0f);
        const juce::Point<float> top { area.getX() + area.getWidth() * (2.5f / 16.0f), area.getY() + 1.0f };
        const auto down = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), top, juce::ModifierKeys::leftButtonModifier,
                                            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, lane, lane, juce::Time::getCurrentTime(), top, juce::Time::getCurrentTime(), 1, false);
        lane->mouseDown (down);
        lane->mouseUp (down);
        check (juce::approximatelyEqual (getParam (*p, Ids::chanceId (2)), 100.0f), "clic sur le pas 3 : " + juce::String (getParam (*p, Ids::chanceId (2))));

        // Glisser vers le bas du pas 1 au pas 6 : tous à 0 % (aucun trou).
        const juce::Point<float> from { area.getX() + area.getWidth() * (0.5f / 16.0f), area.getBottom() - 0.5f };
        const juce::Point<float> to { area.getX() + area.getWidth() * (5.5f / 16.0f), area.getBottom() - 0.5f };
        const auto press = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), from, juce::ModifierKeys::leftButtonModifier,
                                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, lane, lane, juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), 1, false);
        const auto drag = press.withNewPosition (to);
        lane->mouseDown (press);
        lane->mouseDrag (drag);
        lane->mouseUp (drag);

        bool allZero = true;

        for (int step = 0; step < 6; ++step)
            allZero = allZero && getParam (*p, Ids::chanceId (step)) < 0.5f;

        check (allZero, "glisser du pas 1 au pas 6 : il reste des pas non nuls");
        check (getParam (*p, Ids::chanceId (6)) > 49.0f, "le pas 7 a ete touche");

        const auto gateArea = gateLane->getLocalBounds().toFloat().reduced (6.0f, 5.0f);
        const juce::Point<float> second { gateArea.getX() + gateArea.getWidth() * (1.5f / 16.0f), gateArea.getCentreY() };
        const auto click = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), second, juce::ModifierKeys::leftButtonModifier,
                                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, gateLane, gateLane, juce::Time::getCurrentTime(), second, juce::Time::getCurrentTime(), 1, false);
        gateLane->mouseDown (click);
        gateLane->mouseUp (click);
        check (getParam (*p, Ids::gateStepId (1)) > 0.5f, "clic sur le pas 2 du gate : il ne s'allume pas");

        // Le nombre de pas suit le potard STEPS.
        setParam (*p, Ids::stepsId, 7.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        check (lane->getNumShownSteps() == 7, "STEPS = 7 : " + juce::String (lane->getNumShownSteps()) + " pas affiches");
    }

    // Afficheurs de valeur
    auto* readout = dynamic_cast<juce::Label*> (find ("value/1"));
    check (readout != nullptr && readout->getText() == "1/16", "afficheur MAX : " + (readout != nullptr ? readout->getText() : juce::String()));
    setParam (*p, Ids::lengthModeId, 0.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    check (readout != nullptr && readout->getText() == "100 ms", "afficheur MAX en FREE : " + (readout != nullptr ? readout->getText() : juce::String()));
    setParam (*p, Ids::lengthModeId, 1.0f);

    auto* rateReadout = dynamic_cast<juce::Label*> (find ("value/3"));
    setParam (*p, Ids::gateFeelId, 1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    check (rateReadout != nullptr && rateReadout->getText() == "1/16 T", "afficheur RATE en triolet : " + (rateReadout != nullptr ? rateReadout->getText() : juce::String()));
    setParam (*p, Ids::gateFeelId, 0.0f);

    // Redimensionnement : proportions gardées, taille retenue.
    editor->setSize (914, 597);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    check ((int) phractal (*p).apvts.state.getProperty ("editorWidth") == editor->getWidth(), "taille de l'interface non retenue");

    // Captures : par défaut, puis tout allumé et en train de jouer.
    const auto snapshot = [&] (const juce::String& name)
    {
        if (outputDir == juce::File())
            return;

        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = outputDir.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        check (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream), "ecriture de " + file.getFullPathName());
    };

    editor->setSize (PhractalFacade::width, PhractalFacade::height);
    snapshot ("phractal-defaut");

    setParam (*p, Ids::stepsId, 16.0f);
    juce::Random rng (12);

    for (int step = 0; step < Ids::numSteps; ++step)
        setParam (*p, Ids::chanceId (step), (float) rng.nextInt (101));

    for (const char* id : { Ids::sliceId, Ids::pitchOnId, Ids::gateId, Ids::filterId })
        setParam (*p, id, 1.0f);

    setParam (*p, Ids::filterTypeId, 2.0f);
    setParam (*p, Ids::filterCutoffId, 2400.0f);
    setParam (*p, Ids::filterResonanceId, 55.0f);
    setParam (*p, Ids::minDivisionId, 30.0f);
    setParam (*p, Ids::attackId, 10.0f);
    setParam (*p, Ids::releaseId, 35.0f);
    setParam (*p, Ids::sustainId, 60.0f);
    setParam (*p, Ids::gateSmoothId, 40.0f);
    setParam (*p, Ids::pitchId, 7.0f);

    TestPlayHead head;
    head.bpm = 190.0;
    p->setPlayHead (&head);
    const auto loop = makeNoise (2, 48000, 9, 0.6f);
    juce::AudioBuffer<float> out;
    process (*p, loop, out, 512, [&head] (int sample) { head.beats = sample / 48000.0 * 190.0 / 60.0; });

    snapshot ("phractal-allume");

    editor->setSize (731, 478);
    snapshot ("phractal-petit");

    // Comme un hôte : le processeur est prévenu avant que l'éditeur disparaisse.
    p->editorBeingDeleted (editor.get());
    editor.reset();
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    testParameters();
    testPassthrough();
    testSlicesOnTheGrid();
    testGateAndOutput();
    testPitchAndMicroLoops();
    testStateAndBypass();
    testEditor (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])) : juce::File());

    std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT" : juce::String (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}

