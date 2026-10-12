#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "SliceEngine.h"
#include "OutputFilter.h"

class PhractalAudioProcessor final : public juce::AudioProcessor
{
public:
    //==========================================================================
    // Identifiants des paramètres (ne pas les renommer : les projets DAW sauvegardés en dépendent)

    // Grille des slices
    static constexpr const char* sliceId          = "slice";
    static constexpr const char* stepsId          = "steps";
    static constexpr const char* thruId           = "thru";
    // + "chance1" à "chance32" (voir chanceId)

    // Longueur des boucles
    static constexpr const char* lengthModeId     = "lengthMode";
    static constexpr const char* maxDivisionId    = "maxDivision";
    static constexpr const char* minDivisionId    = "minDivision";
    static constexpr const char* maxTimeId        = "maxTime";
    static constexpr const char* minTimeId        = "minTime";
    static constexpr const char* randomId         = "random";
    static constexpr const char* allowTripletId   = "allowTriplet";
    static constexpr const char* allowDottedId    = "allowDotted";

    // Enveloppe des slices
    static constexpr const char* attackId         = "attack";
    static constexpr const char* decayId          = "decay";
    static constexpr const char* sustainId        = "sustain";
    static constexpr const char* releaseId        = "release";

    // Pitch des slices
    static constexpr const char* pitchOnId        = "pitchOn";
    static constexpr const char* pitchId          = "pitch";
    static constexpr const char* pitchModeId      = "pitchMode";

    // Gate
    static constexpr const char* gateId           = "gate";
    static constexpr const char* gateDivisionId   = "gateDivision";
    static constexpr const char* gateFeelId       = "gateFeel";
    static constexpr const char* gateStepsId      = "gateSteps";
    static constexpr const char* gateLengthId     = "gateLength";
    static constexpr const char* gateSmoothId     = "gateSmooth";
    // + "gateStep1" à "gateStep32" (voir gateStepId)

    // Sortie
    static constexpr const char* filterId          = "filter";
    static constexpr const char* filterTypeId      = "filterType";
    static constexpr const char* filterCutoffId    = "filterCutoff";
    static constexpr const char* filterResonanceId = "filterResonance";
    static constexpr const char* filterSlopeId     = "filterSlope";
    static constexpr const char* limiterId         = "limiter";
    static constexpr const char* mixId             = "mix";
    static constexpr const char* levelId           = "level";

    static juce::String chanceId (int step)         { return "chance" + juce::String (step + 1); }
    static juce::String gateStepId (int step)       { return "gateStep" + juce::String (step + 1); }

    static constexpr int numSteps = SliceEngine::maxSteps;
    static constexpr float minTimeMs = 0.1f, maxTimeMs = 1000.0f;
    static constexpr float maxPitch = 24.0f;
    static constexpr float minLevelDb = -24.0f, maxLevelDb = 12.0f;

    enum Mode { modeFree = 0, modeSync = 1 };
    enum Feel { straight = 0, triplet = 1, dotted = 2 };

    /** Longueurs de boucle en SYNC, de la plus longue (1/1) à la plus courte (1/4096), pointées et triolets compris. */
    static const juce::StringArray& getDivisionNames();
    static double getDivisionInBeats (int index);
    static int getDivisionFeel (int index);
    static int getNumDivisions()                    { return getDivisionNames().size(); }

    /** Pas du gate, de 1/1 à 1/64. */
    static const juce::StringArray& getGateDivisionNames();
    static double getGateDivisionInBeats (int index);
    static double getFeelFactor (int feel);

    /** Texte d'une transposition : "+3 st", ou "+3.25 st" quand elle ne tombe pas sur un demi-ton. */
    static juce::String pitchToText (float semitones);

    //==========================================================================
    PhractalAudioProcessor();
    ~PhractalAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "Phractal"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // Pour l'interface (lecture seule)
    const SliceEngine& getEngine() const noexcept           { return engine; }
    double getBpm() const noexcept                          { return bpmNow.load (std::memory_order_relaxed); }
    double getCurrentSampleRate() const noexcept            { return currentSampleRate; }

    /** Crêtes d'entrée et de sortie depuis la dernière lecture (l'interface les relit et les remet à zéro). */
    float takeInputPeak() noexcept                          { return inputPeak.exchange (0.0f, std::memory_order_relaxed); }
    float takeOutputPeak() noexcept                         { return outputPeak.exchange (0.0f, std::memory_order_relaxed); }

    /** Longueurs (en noires) entre lesquelles les slices tirent leur boucle en SYNC, selon les réglages. */
    static int collectDivisions (int longest, int shortest, bool random, bool allowTriplets, bool allowDotted, int* indices);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SliceEngine engine;
    OutputFilter filter, limiterStage;

    std::atomic<float>* sliceParam = nullptr;
    std::atomic<float>* stepsParam = nullptr;
    std::atomic<float>* thruParam = nullptr;
    std::atomic<float>* chanceParams[numSteps] = {};
    std::atomic<float>* lengthModeParam = nullptr;
    std::atomic<float>* maxDivisionParam = nullptr;
    std::atomic<float>* minDivisionParam = nullptr;
    std::atomic<float>* maxTimeParam = nullptr;
    std::atomic<float>* minTimeParam = nullptr;
    std::atomic<float>* randomParam = nullptr;
    std::atomic<float>* allowTripletParam = nullptr;
    std::atomic<float>* allowDottedParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* pitchOnParam = nullptr;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* pitchModeParam = nullptr;
    std::atomic<float>* gateParam = nullptr;
    std::atomic<float>* gateDivisionParam = nullptr;
    std::atomic<float>* gateFeelParam = nullptr;
    std::atomic<float>* gateStepsParam = nullptr;
    std::atomic<float>* gateStepParams[numSteps] = {};
    std::atomic<float>* gateLengthParam = nullptr;
    std::atomic<float>* gateSmoothParam = nullptr;
    std::atomic<float>* filterParam = nullptr;
    std::atomic<float>* filterTypeParam = nullptr;
    std::atomic<float>* filterCutoffParam = nullptr;
    std::atomic<float>* filterResonanceParam = nullptr;
    std::atomic<float>* filterSlopeParam = nullptr;
    std::atomic<float>* limiterParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* levelParam = nullptr;

    double currentSampleRate = 44100.0;
    double lastBpm = 120.0;
    std::atomic<double> bpmNow { 120.0 };

    // Dry / wet et niveau de sortie, lissés
    juce::AudioBuffer<float> dryBuffer;
    float mixNow = 1.0f, levelNow = 1.0f, smoothing = 1.0f;

    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhractalAudioProcessor)
};
