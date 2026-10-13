#pragma once

#include "PluginProcessor.h"

/**
    Design clair, celui de Phragments et Phibres : plaques à pans coupés, gris
    clair et blanc, accent en dégradé bleu -> cyan. Le relief vient d'ombres
    portées (ce qui dépasse) et d'ombres intérieures (ce qui est en creux).
    Police Chakra Petch embarquée.
*/
class PhractalLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PhractalLookAndFeel();

    // Palette
    static juce::Colour ground()    { return juce::Colour (0xffd9dfe7); }
    static juce::Colour track()     { return juce::Colour (0xffcdd5df); }
    static juce::Colour ink()       { return juce::Colour (0xff101622); }
    static juce::Colour dimInk()    { return juce::Colour (0xff5b6676); }
    static juce::Colour blue()      { return juce::Colour (0xff1785ff); }
    static juce::Colour cyan()      { return juce::Colour (0xff1de4f5); }
    static juce::Colour standby()   { return juce::Colour (0xff93a3bf); }
    static juce::Colour unlit()     { return juce::Colour (0xffc3ccd8); }
    static juce::Colour shadow()    { return juce::Colour (0xff1b2b4b); }

    /** Dégradé d'accent, du bleu au cyan. holdBlue garde le bleu sur les deux premiers tiers (texte blanc lisible). */
    static juce::ColourGradient accentGradient (juce::Point<float> from, juce::Point<float> to, bool holdBlue = false);
    static juce::Colour accentAt (float proportion);

    static juce::Path chamferedRect (juce::Rectangle<float> area, float cut);

    /** Trait en dégradé avec un halo lumineux autour. */
    static void strokeGlowing (juce::Graphics&, const juce::Path&, float thickness,
                               const juce::ColourGradient&, bool roundedEnds = true);

    /** Ombre portée sous une forme : elle la décolle du fond. Coûteuse : à réserver au fond, dessiné une seule fois. */
    static void dropShadow (juce::Graphics&, const juce::Path&, float radius, float offsetY, float alpha);

    /** Ombre intérieure en haut d'une forme : elle la creuse. Même remarque. */
    static void innerShadow (juce::Graphics&, const juce::Path&, float radius, float offsetY, float alpha);

    /** Remplissage en dégradé vertical. */
    static void fillVertical (juce::Graphics&, const juce::Path&, juce::Rectangle<float> area, juce::Colour top, juce::Colour bottom);

    /** Écran blanc en creux (afficheurs). */
    static void drawScreen (juce::Graphics&, juce::Rectangle<float> area, float cut);

    juce::Font font (float pointHeight, bool bold, float tracking = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;

    /** Rayon du corps d'un grand / petit potard pour une zone donnée (le fond dessine son ombre). */
    static float mainKnobBodyRadius (juce::Rectangle<float> bounds)   { return 58.0f * juce::jmin (bounds.getWidth(), bounds.getHeight()) / 190.0f; }
    static float miniKnobBodyRadius (juce::Rectangle<float> bounds)   { return juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 8.5f; }

private:
    void drawMainKnob (juce::Graphics&, juce::Rectangle<float> bounds, float sliderPos,
                       float startAngle, float endAngle, juce::Slider&);
    void drawMiniKnob (juce::Graphics&, juce::Rectangle<float> bounds, float sliderPos,
                       float startAngle, float endAngle, juce::Slider&);

    juce::Typeface::Ptr semiBoldFace, boldFace;
};

//==============================================================================
/**
    La façade à taille fixe : cinq modules (SLICE, LOOP, PITCH, GATE, FILTER)
    côte à côte, chacun avec son grand potard, son afficheur, ses interrupteurs,
    son grand bouton et un panneau de réglages. L'éditeur la met à l'échelle.
*/
class PhractalFacade final : public juce::Component,
                             private juce::Timer
{
public:
    static constexpr int width = 1218, height = 642;

    explicit PhractalFacade (PhractalAudioProcessor&);
    ~PhractalFacade() override;

    void paint (juce::Graphics&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    /** Interrupteur à plusieurs positions relié à un paramètre à choix. */
    struct ChoiceSwitch
    {
        juce::OwnedArray<juce::TextButton> buttons;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    void configureMainKnob (juce::Slider&, const char* parameterId, int column);
    void configureMiniKnob (juce::Slider&, const char* parameterId, const char* name, juce::Rectangle<int> area);
    void configureValueLabel (juce::Label&, int column);
    void configureBigToggle (juce::TextButton&, const char* parameterId, int column, std::function<void (bool)> onChange);
    void configureSmallToggle (juce::TextButton&, const char* parameterId, juce::Rectangle<int> area, std::function<void (bool)> onChange = {});
    void configureSwitch (ChoiceSwitch&, const char* parameterId, const juce::StringArray& texts,
                          juce::Rectangle<int> area, bool tiny, std::function<void (int)> onChange);

    /** Le potard affiché dans un module (selon son mode). */
    juce::Slider& activeKnob (int column);
    void setStandby (juce::Slider&, bool standby);
    void modesChanged();
    void updateValueLabels();

    void timerCallback() override;

    void paintBackground (juce::Graphics&);
    void paintLights (juce::Graphics&);
    void paintModules (juce::Graphics&);
    void paintRange (juce::Graphics&, juce::Rectangle<float> area);
    void paintMeters (juce::Graphics&, juce::Rectangle<float> area);

    PhractalAudioProcessor& audioProcessor;
    PhractalLookAndFeel lookAndFeel;

    // Fond (plaques, ombres, textes fixes) : dessiné une fois, à la résolution de l'écran.
    juce::Image background;
    float backgroundScale = 0.0f;

    bool sliceOn = false, syncOn = true, pitchOn = false, gateOn = false, filterOn = false;
    bool pitchInSemitones = true, steepFilter = true;
    int filterType = 0, gateFeel = 0, thruMode = 1;
    double shownLength = -1.0, shownBpm = -1.0;
    float inputLevel = 0.0f, outputLevel = 0.0f;   // affichés, en dB au-dessus de -60

    juce::Slider chanceKnob, maxDivisionKnob, maxTimeKnob, pitchKnob, gateRateKnob, cutoffKnob;
    juce::Slider stepsKnob, durationKnob, attackKnob, releaseKnob, minDivisionKnob, minTimeKnob;
    juce::Slider mixKnob, levelKnob, gateLengthKnob, gateSmoothKnob, resonanceKnob;
    juce::Label valueLabels[5];
    juce::TextButton sliceButton { "SLICE" }, syncButton { "SYNC" }, pitchButton { "PITCH" }, gateButton { "GATE" }, filterButton { "FILTER" };
    juce::TextButton limiterButton { "LIMITER" };
    ChoiceSwitch thruSwitch, pitchModeSwitch, gateFeelSwitch, filterTypeSwitch, filterSlopeSwitch;

    // Déclarés après les contrôles : détruits avant eux.
    std::vector<std::unique_ptr<APVTS::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> stateWatchers;
    std::unique_ptr<juce::ParameterAttachment> pitchAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhractalFacade)
};

//==============================================================================
/** L'éditeur : la façade, mise à l'échelle de la fenêtre (redimensionnable, proportions gardées). */
class PhractalAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PhractalAudioProcessorEditor (PhractalAudioProcessor&);
    ~PhractalAudioProcessorEditor() override;

    void resized() override;

    PhractalFacade& getFacade() noexcept                { return facade; }

private:
    PhractalAudioProcessor& audioProcessor;
    PhractalFacade facade;
    bool sized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhractalAudioProcessorEditor)
};
