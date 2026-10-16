#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String timeToText (float ms, int)
    {
        if (ms >= 1000.0f)
            return juce::String (ms / 1000.0f, 2) + " s";

        if (ms < 9.995f)
            return juce::String (ms, 2) + " ms";

        if (ms < 99.95f)
            return juce::String (ms, 1) + " ms";

        return juce::String (juce::roundToInt (ms)) + " ms";
    }

    float textToNumber (const juce::String& text)
    {
        return text.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue();
    }

    float textToTime (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();

        // "1.5 s" -> 1500 ms ; "250" ou "250 ms" -> 250 ms
        return (t.endsWithChar ('s') && ! t.endsWith ("ms")) ? value * 1000.0f : value;
    }

    // Plage de temps exponentielle et INVERSÉE : 0 = le plus long, 1 = le plus court.
    // Tourner le potard à droite raccourcit, comme pour les divisions.
    juce::NormalisableRange<float> makeReversedTimeRange (float shortestMs, float longestMs)
    {
        return { shortestMs, longestMs,
                 [] (float start, float end, float normalised)
                 {
                     return end * std::pow (start / end, juce::jlimit (0.0f, 1.0f, normalised));
                 },
                 [] (float start, float end, float value)
                 {
                     return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (start, end, value) / end) / std::log (start / end));
                 },
                 [] (float start, float end, float value)
                 {
                     const float step = value < 1.0f ? 0.01f : value < 100.0f ? 0.1f : 1.0f;   // plus fin sur les durées très courtes
                     return juce::jlimit (start, end, std::round (value / step) * step);
                 } };
    }

    juce::String frequencyToText (float hertz, int)
    {
        if (hertz >= 1000.0f)
            return juce::String (hertz / 1000.0f, 2) + " kHz";

        return juce::String (juce::roundToInt (hertz)) + " Hz";
    }

    float textToFrequency (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();

        // "1.2k" ou "1.2 kHz" -> 1200 Hz ; "250" ou "250 Hz" -> 250 Hz
        return t.containsChar ('k') ? value * 1000.0f : value;
    }

    std::unique_ptr<juce::AudioParameterFloat> makePercentParameter (const juce::String& id, const juce::String& name, float defaultPercent)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), defaultPercent,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; })
                                                                .withValueFromStringFunction (textToNumber));
    }

    std::unique_ptr<juce::AudioParameterBool> makeSwitch (const juce::String& id, const juce::String& name, bool defaultValue)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> makeChoice (const char* id, const char* name, const juce::StringArray& choices, int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, defaultIndex);
    }

    std::unique_ptr<juce::AudioParameterFloat> makeTimeParameter (const char* id, const char* name, float defaultMs)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            makeReversedTimeRange (PhractalAudioProcessor::minTimeMs, PhractalAudioProcessor::maxTimeMs), defaultMs,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction (timeToText)
                                                                .withValueFromStringFunction (textToTime));
    }

    std::unique_ptr<juce::AudioParameterInt> makeCount (const char* id, const char* name, int lowest, int highest, int defaultValue)
    {
        return std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lowest, highest, defaultValue,
                                                          juce::AudioParameterIntAttributes()
                                                              .withStringFromValueFunction ([] (int value, int) { return juce::String (value); })
                                                              .withValueFromStringFunction ([] (const juce::String& text) { return text.getIntValue(); }));
    }

    struct DivisionTable
    {
        juce::StringArray names;
        std::vector<double> beats;
        std::vector<int> feels;

        DivisionTable()
        {
            add ("1/1", 4.0, PhractalAudioProcessor::straight);

            // Par octave : 1/(2n) pointée, 1/n triolet, 1/(2n) — de la plus longue à la plus courte.
            for (int n = 1; n <= 2048; n *= 2)
            {
                add ("1/" + juce::String (2 * n) + "D", 6.0 / (2.0 * n), PhractalAudioProcessor::dotted);
                add ("1/" + juce::String (n) + "T", 8.0 / (3.0 * n), PhractalAudioProcessor::triplet);
                add ("1/" + juce::String (2 * n), 4.0 / (2.0 * n), PhractalAudioProcessor::straight);
            }
        }

        void add (const juce::String& name, double inBeats, int feel)
        {
            names.add (name);
            beats.push_back (inBeats);
            feels.push_back (feel);
        }
    };

    const DivisionTable& divisionTable()
    {
        static const DivisionTable table;
        return table;
    }

    float peakOf (const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
    {
        float peak = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));

        return std::isfinite (peak) ? peak : 0.0f;
    }

    void raisePeak (std::atomic<float>& holder, float value)
    {
        float previous = holder.load (std::memory_order_relaxed);

        while (value > previous && ! holder.compare_exchange_weak (previous, value, std::memory_order_relaxed)) {}
    }
}

//==============================================================================
const juce::StringArray& PhractalAudioProcessor::getDivisionNames()
{
    return divisionTable().names;
}

double PhractalAudioProcessor::getDivisionInBeats (int index)
{
    const auto& beats = divisionTable().beats;
    return beats[(size_t) juce::jlimit (0, (int) beats.size() - 1, index)];
}

int PhractalAudioProcessor::getDivisionFeel (int index)
{
    const auto& feels = divisionTable().feels;
    return feels[(size_t) juce::jlimit (0, (int) feels.size() - 1, index)];
}

const juce::StringArray& PhractalAudioProcessor::getGateDivisionNames()
{
    static const juce::StringArray names { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    return names;
}

double PhractalAudioProcessor::getGateDivisionInBeats (int index)
{
    static constexpr double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
    return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
}

double PhractalAudioProcessor::getFeelFactor (int feel)
{
    return feel == triplet ? 2.0 / 3.0 : feel == dotted ? 1.5 : 1.0;
}

juce::String PhractalAudioProcessor::pitchToText (float semitones)
{
    const float rounded = std::round (semitones);
    const bool whole = std::abs (semitones - rounded) < 0.005f;
    const auto number = whole ? juce::String (juce::roundToInt (rounded)) : juce::String (semitones, 2);

    return ((whole ? rounded : semitones) > 0.0f ? "+" : "") + number + " st";
}

int PhractalAudioProcessor::collectDivisions (int from, int to, int* indices)
{
    const int last = getNumDivisions() - 1;
    const int a = juce::jlimit (0, last, juce::jmin (from, to)), b = juce::jlimit (0, last, juce::jmax (from, to));
    int count = 0;

    for (int i = a; i <= b; ++i)
        indices[count++] = i;

    return count;
}

juce::AudioProcessorValueTreeState::ParameterLayout PhractalAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Slices ---
    layout.add (makeSwitch (sliceId, "Slice", false));
    layout.add (makeCount (stepsId, "Grid", 1, numSteps, 16));
    layout.add (makePercentParameter (chanceId, "Chance", 50.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { durationId, 1 }, "Duration",
                                                             juce::NormalisableRange<float> (5.0f, 100.0f, 1.0f), 100.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; })
                                                                 .withValueFromStringFunction (textToNumber)));
    layout.add (makeChoice (thruId, "Thru", { "Off", "Input", "Random" }, SliceEngine::thruInput));
    layout.add (makePercentParameter (attackId, "Attack", 0.0f));
    layout.add (makePercentParameter (releaseId, "Release", 10.0f));

    // --- Longueur des boucles : tirée au hasard entre LONG et SHORT ---
    layout.add (makeChoice (lengthModeId, "Length Mode", { "Free", "Sync" }, modeSync));
    layout.add (makeChoice (maxDivisionId, "Long", getDivisionNames(), 12));    // 1/16
    layout.add (makeChoice (minDivisionId, "Short", getDivisionNames(), 18));   // 1/64
    layout.add (makeTimeParameter (maxTimeId, "Long Time", 100.0f));
    layout.add (makeTimeParameter (minTimeId, "Short Time", 10.0f));

    // --- Pitch des slices ---
    layout.add (makeSwitch (pitchOnId, "Pitch On", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pitchId, 1 }, "Pitch",
                                                             juce::NormalisableRange<float> (-maxPitch, maxPitch, 0.01f), -12.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float value, int) { return pitchToText (value); })
                                                                 .withValueFromStringFunction (textToNumber)));
    layout.add (makeChoice (pitchModeId, "Pitch Mode", { "Semitones", "Free" }, 0));

    // --- Gate ---
    layout.add (makeSwitch (gateId, "Gate", false));
    layout.add (makeChoice (gateDivisionId, "Gate Rate", getGateDivisionNames(), 4));   // 1/16
    layout.add (makeChoice (gateFeelId, "Gate Feel", { "Straight", "Triplet", "Dotted" }, straight));
    layout.add (makePercentParameter (gateLengthId, "Gate Length", 75.0f));
    layout.add (makePercentParameter (gateSmoothId, "Gate Smooth", 10.0f));

    // --- Sortie ---
    layout.add (makeSwitch (filterId, "Filter", false));
    layout.add (makeChoice (filterTypeId, "Filter Type", { "High Pass", "Band Pass", "Low Pass" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { filterCutoffId, 1 }, "Filter Cutoff",
                                                             juce::NormalisableRange<float> ((float) OutputFilter::minFrequency, (float) OutputFilter::maxFrequency,
                                                                 [] (float start, float end, float normalised) { return start * std::pow (end / start, juce::jlimit (0.0f, 1.0f, normalised)); },
                                                                 [] (float start, float end, float value) { return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (start, end, value) / start) / std::log (end / start)); },
                                                                 [] (float start, float end, float value)
                                                                 {
                                                                     const float step = value < 1000.0f ? 1.0f : 10.0f;
                                                                     return juce::jlimit (start, end, std::round (value / step) * step);
                                                                 }),
                                                             20.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction (frequencyToText)
                                                                 .withValueFromStringFunction (textToFrequency)));
    layout.add (makePercentParameter (filterResonanceId, "Filter Resonance", 20.0f));
    layout.add (makeChoice (filterSlopeId, "Filter Slope", { "12 dB", "24 dB" }, 1));
    layout.add (makeSwitch (limiterId, "Limiter", true));
    layout.add (makePercentParameter (mixId, "Mix", 100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { levelId, 1 }, "Level",
                                                             juce::NormalisableRange<float> (minLevelDb, maxLevelDb, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float value, int)
                                                                 {
                                                                     return (value > 0.04f ? "+" : "") + juce::String (value, 1) + " dB";
                                                                 })
                                                                 .withValueFromStringFunction (textToNumber)));

    return layout;
}

//==============================================================================
PhractalAudioProcessor::PhractalAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Phractal", createParameterLayout()),
      presets (apvts, "Phractal", ".phcpreset", PresetLibrary::defaultsOnly (*this, {}), {}, "UNTITLED")
{
    presets.restoreFromState();   // projet neuf : DEFAULT
    const auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    sliceParam           = raw (sliceId);
    stepsParam           = raw (stepsId);
    thruParam            = raw (thruId);
    chanceParam          = raw (chanceId);
    durationParam        = raw (durationId);
    lengthModeParam      = raw (lengthModeId);
    maxDivisionParam     = raw (maxDivisionId);
    minDivisionParam     = raw (minDivisionId);
    maxTimeParam         = raw (maxTimeId);
    minTimeParam         = raw (minTimeId);
    attackParam          = raw (attackId);
    releaseParam         = raw (releaseId);
    pitchOnParam         = raw (pitchOnId);
    pitchParam           = raw (pitchId);
    pitchModeParam       = raw (pitchModeId);
    gateParam            = raw (gateId);
    gateDivisionParam    = raw (gateDivisionId);
    gateFeelParam        = raw (gateFeelId);
    gateLengthParam      = raw (gateLengthId);
    gateSmoothParam      = raw (gateSmoothId);
    filterParam          = raw (filterId);
    filterTypeParam      = raw (filterTypeId);
    filterCutoffParam    = raw (filterCutoffId);
    filterResonanceParam = raw (filterResonanceId);
    filterSlopeParam     = raw (filterSlopeId);
    limiterParam         = raw (limiterId);
    mixParam             = raw (mixId);
    levelParam           = raw (levelId);
}

bool PhractalAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == out;
}

void PhractalAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    engine.prepare (currentSampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    engine.seed ((std::uint64_t) juce::Time::getHighResolutionTicks() | 1u);

    filter.setFrequency ((double) filterCutoffParam->load (std::memory_order_relaxed));
    filter.setResonance (0.01 * (double) filterResonanceParam->load (std::memory_order_relaxed));
    filter.setType (juce::roundToInt (filterTypeParam->load (std::memory_order_relaxed)));
    filter.setSteep (filterSlopeParam->load (std::memory_order_relaxed) > 0.5f);
    filter.setLimiter (false);   // le limiteur est en toute fin de chaîne, après le niveau de sortie
    filter.setActive (filterParam->load (std::memory_order_relaxed) > 0.5f);
    filter.prepare (currentSampleRate);

    // Filtre éteint + limiteur allumé : un limiteur seul (il protège dès le premier échantillon).
    limiterStage.setActive (false);
    limiterStage.setLimiter (limiterParam->load (std::memory_order_relaxed) > 0.5f);
    limiterStage.prepare (currentSampleRate);

    dryBuffer.setSize (juce::jmax (1, getTotalNumOutputChannels()), juce::jmax (4096, samplesPerBlock), false, true, false);
    smoothing = (float) (1.0 - std::exp (-1.0 / (0.01 * currentSampleRate)));   // ~10 ms
    mixNow = 0.01f * mixParam->load (std::memory_order_relaxed);
    levelNow = juce::Decibels::decibelsToGain (levelParam->load (std::memory_order_relaxed));
}

void PhractalAudioProcessor::reset()
{
    engine.reset();
    filter.reset();
    limiterStage.reset();
}

void PhractalAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto value = [] (const std::atomic<float>* parameter) { return parameter->load (std::memory_order_relaxed); };
    const auto isOn = [&value] (const std::atomic<float>* parameter) { return value (parameter) > 0.5f; };
    const auto choice = [&value] (const std::atomic<float>* parameter) { return juce::roundToInt (value (parameter)); };

    // --- Tempo, position et mesure de l'hôte (on garde le dernier tempo connu si l'hôte n'en fournit pas) ---
    double hostBeats = 0.0, barStart = 0.0, beatsPerBar = 4.0;
    bool hostIsMoving = false, knowsBarStart = false;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                if (*bpm > 1.0)
                    lastBpm = *bpm;

            if (const auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    beatsPerBar = 4.0 * signature->numerator / signature->denominator;

            if (const auto beats = position->getPpqPosition())
            {
                hostBeats = *beats;
                hostIsMoving = position->getIsPlaying();

                if (const auto lastBar = position->getPpqPositionOfLastBarStart())
                {
                    barStart = *lastBar;
                    knowsBarStart = true;
                }
            }
        }
    }

    bpmNow.store (lastBpm, std::memory_order_relaxed);
    const double samplesPerBeat = 60.0 / lastBpm * currentSampleRate;
    const double samplesPerMs = 0.001 * currentSampleRate;

    // --- Dry / wet : on garde l'entrée de côté ---
    const int numChannels = juce::jmin (numIn, numOut, buffer.getNumChannels());
    raisePeak (inputPeak, peakOf (buffer, numChannels, numSamples));

    dryBuffer.setSize (juce::jmax (dryBuffer.getNumChannels(), numChannels), juce::jmax (dryBuffer.getNumSamples(), numSamples), true, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // --- Grille : nombre de pas dans la mesure ---
    const int steps = juce::jlimit (1, numSteps, choice (stepsParam));
    const double stepBeats = beatsPerBar / steps;
    engine.setGrid (steps, stepBeats * samplesPerBeat);

    if (hostIsMoving)
    {
        double inBar = knowsBarStart ? hostBeats - barStart : std::fmod (hostBeats, beatsPerBar);

        if (inBar < 0.0 || inBar >= beatsPerBar + 1.0e-6)
            inBar = std::fmod (std::fmod (hostBeats, beatsPerBar) + beatsPerBar, beatsPerBar);

        engine.syncGrid (inBar / stepBeats);
    }

    for (int step = 0; step < numSteps; ++step)
        engine.setChance (step, 0.01f * value (chanceParam));

    engine.setThru (choice (thruParam));
    engine.setDuration (0.01 * value (durationParam));

    // --- Longueur des boucles : une valeur de note tirée entre LONG et SHORT, ou une durée ---
    if (choice (lengthModeParam) == modeSync)
    {
        int indices[64];
        double lengths[64];
        const int count = collectDivisions (choice (maxDivisionParam), choice (minDivisionParam), indices);

        for (int i = 0; i < count; ++i)
            lengths[i] = getDivisionInBeats (indices[i]) * samplesPerBeat;

        engine.setLengthChoices (lengths, count);
    }
    else
    {
        engine.setLengthSpan ((double) value (minTimeParam) * samplesPerMs, (double) value (maxTimeParam) * samplesPerMs);
    }

    // Enveloppe : attaque et relâchement, maintien plein.
    engine.setEnvelope (0.01 * value (attackParam), 0.0, 1.0, 0.01 * value (releaseParam));

    // --- Pitch des slices : en demi-tons entiers, ou libre ---
    const double semitones = (double) value (pitchParam);
    engine.setPitch (isOn (pitchOnParam) ? (choice (pitchModeParam) == 1 ? semitones : std::round (semitones)) : 0.0);

    // --- Gate ---
    // Un pulse régulier : chaque pas s'ouvre pendant LENGTH.
    const double gateStepBeats = getGateDivisionInBeats (choice (gateDivisionParam)) * getFeelFactor (choice (gateFeelParam));
    engine.setGate (2, gateStepBeats * samplesPerBeat, 0xffffffffu, 0.01 * value (gateLengthParam), 0.01 * value (gateSmoothParam));
    engine.setGateActive (isOn (gateParam));

    if (hostIsMoving)
        engine.syncGate (hostBeats / gateStepBeats);

    engine.setSlicing (isOn (sliceParam));

    // --- Slices, THRU, pitch et gate ---
    const auto channels = buffer.getArrayOfWritePointers();
    engine.process (channels, numChannels, numSamples);

    // --- Filtre ---
    filter.setFrequency ((double) value (filterCutoffParam));
    filter.setResonance (0.01 * (double) value (filterResonanceParam));
    filter.setType (choice (filterTypeParam));
    filter.setSteep (isOn (filterSlopeParam));
    filter.setActive (isOn (filterParam));
    filter.process (channels, numChannels, numSamples);

    // --- Mélange avec l'entrée, niveau de sortie ---
    const float mixTarget = juce::jlimit (0.0f, 1.0f, 0.01f * value (mixParam));
    const float levelTarget = juce::Decibels::decibelsToGain (juce::jlimit (minLevelDb, maxLevelDb, value (levelParam)));

    if (mixTarget < 1.0f || mixNow < 1.0f || ! juce::exactlyEqual (levelTarget, 1.0f) || ! juce::exactlyEqual (levelNow, 1.0f))
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float mixDelta = mixTarget - mixNow;
            mixNow = std::abs (mixDelta) < 1.0e-4f ? mixTarget : mixNow + mixDelta * smoothing;

            const float levelDelta = levelTarget - levelNow;
            levelNow = std::abs (levelDelta) < 1.0e-5f ? levelTarget : levelNow + levelDelta * smoothing;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float dry = dryBuffer.getSample (ch, n);
                channels[ch][n] = levelNow * (dry + mixNow * (channels[ch][n] - dry));
            }
        }
    }

    // --- Limiteur de sécurité, en toute fin ---
    limiterStage.setLimiter (isOn (limiterParam));
    limiterStage.process (channels, numChannels, numSamples);

    raisePeak (outputPeak, peakOf (buffer, numChannels, numSamples));
}

void PhractalAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // En bypass on repart d'un état propre : pas de vieille slice rejouée à la réactivation.
    engine.reset();
    filter.reset();
    limiterStage.reset();
    AudioProcessor::processBlockBypassed (buffer, midi);
}

//==============================================================================
juce::AudioProcessorEditor* PhractalAudioProcessor::createEditor()
{
    return new PhractalAudioProcessorEditor (*this);
}

void PhractalAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PhractalAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        presets.restoreFromState();
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhractalAudioProcessor();
}
