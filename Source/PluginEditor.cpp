#include "PluginEditor.h"
#include "BinaryData.h"

#ifndef PHRACTAL_VERSION
 #define PHRACTAL_VERSION "dev"
#endif

namespace
{
    using LF = PhractalLookAndFeel;
    using Ids = PhractalAudioProcessor;
    using Pt = juce::Point<float>;

    // Façade : cinq modules côte à côte (SLICE, LOOP, PITCH, GATE, FILTER), tous bâtis sur la même grille,
    // puis la séquence sur toute la largeur.
    constexpr int numColumns = 5, columnWidth = 222, columnGap = 14, firstColumnX = 26;
    constexpr int editorWidth = PhractalFacade::width, editorHeight = PhractalFacade::height;
    static_assert (editorWidth == 2 * firstColumnX + numColumns * columnWidth + (numColumns - 1) * columnGap, "largeur de la façade");
    constexpr int panelInset = 12, panelInnerWidth = columnWidth - 2 * panelInset;
    constexpr int footerY = 766;

    constexpr int columnX (int column)                  { return firstColumnX + column * (columnWidth + columnGap); }

    juce::Rectangle<int> bayArea (int column)           { return { columnX (column), 84, columnWidth, 268 }; }
    juce::Rectangle<int> knobArea (int column)          { return { columnX (column) + 31, 100, 160, 160 }; }
    juce::Rectangle<int> readoutArea (int column)       { return { columnX (column) + 16, 276, columnWidth - 32, 34 }; }
    juce::Rectangle<int> switchArea (int column, int offset, int width)   { return { columnX (column) + offset, 318, width, 24 }; }
    juce::Rectangle<int> buttonArea (int column)        { return { columnX (column), 364, columnWidth, 52 }; }
    juce::Rectangle<int> panelArea (int column)         { return { columnX (column), 430, columnWidth, 166 }; }
    juce::Rectangle<int> displayArea (int column)       { return { columnX (column) + panelInset, 458, panelInnerWidth, 44 }; }

    // Séquence : un panneau sur toute la largeur, deux rangées de pas.
    const juce::Rectangle<int> bandArea { firstColumnX, 610, editorWidth - 2 * firstColumnX, 142 };
    const juce::Rectangle<int> chanceLaneArea { 132, 622, editorWidth - firstColumnX - 12 - 132, 66 };
    const juce::Rectangle<int> gateLaneArea { 132, 700, editorWidth - firstColumnX - 12 - 132, 40 };

    // Interrupteurs d'une baie : pleine largeur, ou mode à gauche et variante à droite.
    constexpr int switchOffset = 16, fullSwitchWidth = columnWidth - 2 * switchOffset;

    /** Petit potard numéro "slot" parmi "numSlots" répartis sur la largeur du panneau. */
    juce::Rectangle<int> miniKnobArea (int column, int slot, int numSlots)
    {
        const int centre = columnX (column) + panelInset + (panelInnerWidth * (2 * slot + 1)) / (2 * numSlots);
        return { centre - 20, 514, 40, 40 };
    }

    /** Dernier tiers d'un panneau, à droite de deux petits potards : une ou deux touches empilées. */
    juce::Rectangle<int> panelKeyArea (int column, int row, int numRows)
    {
        const int x = columnX (column) + panelInset + (2 * panelInnerWidth) / 3 + 2;
        const int width = columnX (column) + columnWidth - panelInset - x;

        if (numRows == 1)
            return { x, 522, width, 24 };

        return { x, 512 + row * 24, width, 20 };
    }

    juce::Path polyline (std::initializer_list<Pt> points, bool closed = false)
    {
        juce::Path path;
        bool first = true;

        for (const auto point : points)
        {
            if (first)
                path.startNewSubPath (point);
            else
                path.lineTo (point);

            first = false;
        }

        if (closed)
            path.closeSubPath();

        return path;
    }

    juce::Path circle (Pt centre, float radius)
    {
        juce::Path path;
        path.addEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
        return path;
    }

    /** "4.00 s" -> "4 s", "10.0 ms" -> "10 ms" : pour les repères de début et de fin des potards. */
    juce::String compactValue (const juce::String& text)
    {
        auto compact = text.replace (".00 ", " ").replace (".0 ", " ");

        if (compact.endsWith (".00"))
            compact = compact.dropLastCharacters (3);
        else if (compact.endsWith (".0"))
            compact = compact.dropLastCharacters (2);

        return compact;
    }

    float textWidth (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        return glyphs.getBoundingBox (0, -1, true).getWidth();
    }

    /** Halo lumineux autour d'une forme allumée. */
    void drawHalo (juce::Graphics& g, const juce::Path& outline, const juce::ColourGradient& gradient)
    {
        struct Layer { float width, alpha; };

        for (const auto layer : { Layer { 16.0f, 0.06f }, Layer { 10.0f, 0.09f }, Layer { 5.0f, 0.13f } })
        {
            g.setGradientFill (gradient);
            g.setOpacity (layer.alpha);
            g.strokePath (outline, juce::PathStrokeType (layer.width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setOpacity (1.0f);
    }

    /** Trace une courbe en dégradé d'accent, remplie en dessous jusqu'au bas de la zone. */
    void plotCurve (juce::Graphics& g, juce::Rectangle<float> plot, const juce::Path& curve, bool lit = true, float thickness = 2.0f)
    {
        auto area = curve;
        area.lineTo (plot.getBottomRight());
        area.lineTo (plot.getBottomLeft());
        area.closeSubPath();

        if (lit)
        {
            const auto gradient = LF::accentGradient (plot.getBottomLeft(), plot.getBottomRight());
            g.setGradientFill (gradient);
            g.setOpacity (0.16f);
            g.fillPath (area);
            g.setGradientFill (gradient);
            g.setOpacity (1.0f);
        }
        else
        {
            g.setColour (LF::standby().withAlpha (0.16f));
            g.fillPath (area);
            g.setColour (LF::standby());
        }

        g.strokePath (curve, juce::PathStrokeType (thickness, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }

    /** "300 ms", "9.6 ms", "0.31 ms", "1.25 s" */
    juce::String durationText (double seconds)
    {
        const double ms = seconds * 1000.0;

        if (ms >= 999.5)
            return juce::String (seconds, 2) + " s";

        if (ms >= 99.5)
            return juce::String (juce::roundToInt (ms)) + " ms";

        if (ms >= 9.95)
            return juce::String (ms, 1) + " ms";

        return juce::String (ms, 2) + " ms";
    }

    /** "405 Hz", "3.2 kHz" */
    juce::String hertzText (double hertz)
    {
        return hertz >= 1000.0 ? juce::String (hertz / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (hertz)) + " Hz";
    }

    /** Une boucle plus courte que ça s'entend comme une note : on affiche sa hauteur. */
    constexpr double toneThresholdSeconds = 0.012;
}

//==============================================================================
PhractalLookAndFeel::PhractalLookAndFeel()
{
    semiBoldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf,
                                                            (size_t) BinaryData::ChakraPetchSemiBold_ttfSize);
    boldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBold_ttf,
                                                        (size_t) BinaryData::ChakraPetchBold_ttfSize);

    setColour (juce::ResizableWindow::backgroundColourId, ground());

    setColour (juce::Label::textColourId, ink());
    setColour (juce::Label::textWhenEditingColourId, ink());
    setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::white);
    setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::textColourId, ink());
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::highlightColourId, cyan().withAlpha (0.45f));
    setColour (juce::TextEditor::highlightedTextColourId, ink());
    setColour (juce::CaretComponent::caretColourId, blue());
}

juce::ColourGradient PhractalLookAndFeel::accentGradient (juce::Point<float> from, juce::Point<float> to, bool holdBlue)
{
    juce::ColourGradient gradient (blue(), from, cyan(), to, false);

    if (holdBlue)
        gradient.addColour (0.62, juce::Colour (0xff1a8dff));

    return gradient;
}

juce::Colour PhractalLookAndFeel::accentAt (float proportion)
{
    return blue().interpolatedWith (cyan(), juce::jlimit (0.0f, 1.0f, proportion));
}

juce::Path PhractalLookAndFeel::chamferedRect (juce::Rectangle<float> area, float cut)
{
    const float c = juce::jmax (0.0f, juce::jmin (cut, area.getWidth() * 0.5f, area.getHeight() * 0.5f));
    const float x = area.getX(), y = area.getY(), r = area.getRight(), b = area.getBottom();

    juce::Path path;
    path.startNewSubPath (x + c, y);
    path.lineTo (r - c, y);
    path.lineTo (r, y + c);
    path.lineTo (r, b - c);
    path.lineTo (r - c, b);
    path.lineTo (x + c, b);
    path.lineTo (x, b - c);
    path.lineTo (x, y + c);
    path.closeSubPath();
    return path;
}

void PhractalLookAndFeel::strokeGlowing (juce::Graphics& g, const juce::Path& path, float thickness,
                                         const juce::ColourGradient& gradient, bool roundedEnds)
{
    struct Layer { float extra, alpha; };

    for (const auto layer : { Layer { 12.0f, 0.07f }, Layer { 8.0f, 0.10f }, Layer { 4.0f, 0.16f } })
    {
        g.setGradientFill (gradient);
        g.setOpacity (layer.alpha);
        g.strokePath (path, juce::PathStrokeType (thickness + layer.extra, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setGradientFill (gradient);
    g.setOpacity (1.0f);
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                              roundedEnds ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
}

void PhractalLookAndFeel::dropShadow (juce::Graphics& g, const juce::Path& path, float radius, float offsetY, float alpha)
{
    juce::DropShadow (shadow().withAlpha (alpha), juce::roundToInt (radius), { 0, juce::roundToInt (offsetY) }).drawForPath (g, path);
}

void PhractalLookAndFeel::innerShadow (juce::Graphics& g, const juce::Path& shape, float radius, float offsetY, float alpha)
{
    // L'ombre de tout ce qui entoure la forme, vue à travers la forme : sombre le long du bord haut, puis estompée.
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);

    juce::Path surround;
    surround.setUsingNonZeroWinding (false);
    surround.addRectangle (shape.getBounds().expanded (radius * 3.0f + std::abs (offsetY) + 4.0f));
    surround.addPath (shape);

    juce::DropShadow (shadow().withAlpha (alpha), juce::roundToInt (radius), { 0, juce::roundToInt (offsetY) }).drawForPath (g, surround);
}

void PhractalLookAndFeel::fillVertical (juce::Graphics& g, const juce::Path& path, juce::Rectangle<float> area,
                                        juce::Colour top, juce::Colour bottom)
{
    g.setGradientFill (juce::ColourGradient (top, area.getX(), area.getY(), bottom, area.getX(), area.getBottom(), false));
    g.fillPath (path);
}

void PhractalLookAndFeel::drawScreen (juce::Graphics& g, juce::Rectangle<float> area, float cut)
{
    const auto shape = chamferedRect (area, cut);

    // Lèvre claire sous l'écran, puis l'écran lui-même, en creux.
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.strokePath (shape, juce::PathStrokeType (1.2f), juce::AffineTransform::translation (0.0f, 1.2f));

    fillVertical (g, shape, area, juce::Colour (0xfff2f5f8), juce::Colours::white);
    innerShadow (g, shape, 4.0f, 2.0f, 0.30f);

    g.setColour (juce::Colour (0xffa7b3c2));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

juce::Font PhractalLookAndFeel::font (float pointHeight, bool bold, float tracking) const
{
    const juce::Font base (juce::FontOptions (bold ? boldFace : semiBoldFace).withPointHeight (pointHeight));
    return tracking > 0.0f ? base.withExtraKerningFactor (tracking) : base;
}

juce::Font PhractalLookAndFeel::getLabelFont (juce::Label&)
{
    return font (20.0f, true);
}

//==============================================================================
void PhractalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

    if (slider.getProperties().contains ("mini"))
        drawMiniKnob (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
    else
        drawMainKnob (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

void PhractalLookAndFeel::drawMainKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto centre = bounds.getCentre();
    const float scale = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 190.0f;
    const float arcRadius = 76.0f * scale, tickRadius = 89.0f * scale, capRadius = 44.0f * scale;
    const float bodyRadius = mainKnobBodyRadius (bounds);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool isStandby = (bool) slider.getProperties().getWithDefault ("standby", false);

    // Le dégradé court de gauche (bleu) à droite (cyan) : il suit la progression de l'arc.
    const float left = centre.x - arcRadius;
    const auto gradient = accentGradient ({ left, centre.y }, { centre.x + arcRadius, centre.y });
    const auto accentAtX = [left, arcRadius] (float px) { return accentAt ((px - left) / (2.0f * arcRadius)); };
    const juce::PathStrokeType arcStroke (6.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // Piste en creux : un bord sombre en haut, un bord clair en bas.
    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.strokePath (trackArc, arcStroke, juce::AffineTransform::translation (0.0f, 1.0f));
    g.setColour (juce::Colour (0xffafbac8));
    g.strokePath (trackArc, arcStroke, juce::AffineTransform::translation (0.0f, -0.7f));
    g.setColour (track());
    g.strokePath (trackArc, arcStroke);

    if (angle > startAngle + 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);

        if (isStandby)
        {
            g.setColour (standby());
            g.strokePath (valueArc, arcStroke);
        }
        else
        {
            strokeGlowing (g, valueArc, 6.0f * scale, gradient);
        }
    }

    // Crans pour les potards à pas (divisions du mode sync)
    const int numSteps = slider.getInterval() > 0.0 ? juce::roundToInt ((slider.getMaximum() - slider.getMinimum()) / slider.getInterval()) : 0;

    if (numSteps > 0 && numSteps <= 16)
    {
        for (int i = 0; i <= numSteps; ++i)
        {
            const float a = startAngle + ((float) i / (float) numSteps) * (endAngle - startAngle);
            const auto p = centre.getPointOnCircumference (tickRadius, a);
            const bool lit = a <= angle + 0.001f;

            g.setColour (! lit ? juce::Colour (0xffaeb9c8) : isStandby ? standby() : accentAtX (p.x));
            g.fillEllipse (juce::Rectangle<float> (4.8f * scale, 4.8f * scale).withCentre (p));
        }
    }

    // Corps du potard (son ombre portée est dessinée une fois pour toutes dans le fond)
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, body.getY(),
                                             juce::Colour (0xffd3dae4), centre.x, body.getBottom(), false));
    g.fillEllipse (body);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, body.getY(),
                                             juce::Colour (0xff9eaaba), centre.x, body.getBottom(), false));
    g.drawEllipse (body, 1.5f * scale);

    // Calotte : légèrement creusée, avec un reflet en haut
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe3e8ef), centre.x, cap.getY(),
                                             juce::Colour (0xfffafbfd), centre.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb4bfcc), centre.x, cap.getY(),
                                             juce::Colours::white, centre.x, cap.getBottom(), false));
    g.drawEllipse (cap, 1.2f * scale);

    // Repère
    const auto tip = centre.getPointOnCircumference (50.0f * scale, angle);
    juce::Path pointer;
    pointer.startNewSubPath (centre.getPointOnCircumference (24.0f * scale, angle));
    pointer.lineTo (tip);
    const juce::PathStrokeType pointerStroke (4.5f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (shadow().withAlpha (0.18f));
    g.strokePath (pointer, pointerStroke, juce::AffineTransform::translation (0.0f, 1.5f * scale));
    g.setColour (isStandby ? juce::Colour (0xff7c8ba6) : accentAtX (tip.x));
    g.strokePath (pointer, pointerStroke);

    // Valeurs de début et de fin de course
    const auto endText = [&slider] (double proportion)
    {
        return compactValue (slider.getTextFromValue (slider.proportionOfLengthToValue (proportion)));
    };

    g.setColour (dimInk());
    g.setFont (font (10.0f * scale, false));
    g.drawText (endText (0.0), juce::Rectangle<float> (64.0f, 14.0f).withCentre ({ centre.x - 65.0f * scale, centre.y + 82.0f * scale }),
                juce::Justification::centred, false);
    g.drawText (endText (1.0), juce::Rectangle<float> (64.0f, 14.0f).withCentre ({ centre.x + 65.0f * scale, centre.y + 82.0f * scale }),
                juce::Justification::centred, false);
}

void PhractalLookAndFeel::drawMiniKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float sliderPos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    const bool isStandby = (bool) slider.getProperties().getWithDefault ("standby", false);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float arcRadius = radius - 3.0f, bodyRadius = miniKnobBodyRadius (bounds);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const juce::PathStrokeType arcStroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.strokePath (trackArc, arcStroke, juce::AffineTransform::translation (0.0f, 0.8f));
    g.setColour (juce::Colour (0xffc2cbd7));
    g.strokePath (trackArc, arcStroke);

    if (angle > startAngle + 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        if (isStandby)
            g.setColour (standby());
        else
            g.setGradientFill (accentGradient ({ centre.x - arcRadius, centre.y }, { centre.x + arcRadius, centre.y }));

        g.strokePath (valueArc, arcStroke);
    }

    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, body.getY(),
                                             juce::Colour (0xffd5dce6), centre.x, body.getBottom(), false));
    g.fillEllipse (body);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, centre.x, body.getY(),
                                             juce::Colour (0xff9eaaba), centre.x, body.getBottom(), false));
    g.drawEllipse (body, 1.2f);

    juce::Path pointer;
    pointer.startNewSubPath (centre.getPointOnCircumference (bodyRadius * 0.3f, angle));
    pointer.lineTo (centre.getPointOnCircumference (bodyRadius - 2.0f, angle));
    g.setColour (isStandby ? juce::Colour (0xff7c8ba6) : blue());
    g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void PhractalLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                bool highlighted, bool down)
{
    const float cut = (float) (double) button.getProperties().getWithDefault ("cut", 8.0);
    const auto area = button.getLocalBounds().toFloat();
    const auto shape = chamferedRect (area, cut);
    const auto inner = chamferedRect (area.reduced (1.5f), cut - 1.0f);

    if (! button.isEnabled())
    {
        // Hors service (variante rythmique en mode free) : à plat, sans relief.
        g.setColour (juce::Colour (0xffcfd6df));
        g.fillPath (shape);
        g.setColour (juce::Colour (0xffe9edf2));
        g.fillPath (inner);
        return;
    }

    if (button.getToggleState())
    {
        // Allumé : le dégradé d'accent, bombé (reflet en haut, ombre en bas).
        g.setGradientFill (accentGradient (area.getTopLeft(), area.getTopRight(), true));
        g.fillPath (shape);

        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.30f), area.getX(), area.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), area.getX(), area.getCentreY(), false));
        g.fillPath (shape);
        g.setGradientFill (juce::ColourGradient (shadow().withAlpha (0.0f), area.getX(), area.getCentreY(),
                                                 shadow().withAlpha (0.22f), area.getX(), area.getBottom(), false));
        g.fillPath (shape);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.strokePath (inner, juce::PathStrokeType (1.0f));
    }
    else
    {
        // Éteint : une touche claire en relief, bord sombre, liseré blanc.
        g.setColour (juce::Colour (0xffa3afbe));
        g.fillPath (shape);
        fillVertical (g, chamferedRect (area.reduced (1.0f), cut - 0.7f), area, juce::Colours::white, juce::Colour (0xffe0e5ec));
        g.setColour (juce::Colours::white);
        g.strokePath (inner, juce::PathStrokeType (1.0f));
    }

    if (down)
    {
        g.setColour (shadow().withAlpha (0.14f));
        g.fillPath (shape);
    }
    else if (highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (button.getToggleState() ? 0.10f : 0.45f));
        g.fillPath (shape);
    }
}

void PhractalLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const bool on = button.getToggleState();
    const auto& properties = button.getProperties();
    const bool big = properties.contains ("big"), small = properties.contains ("small"), tiny = properties.contains ("tiny");
    const auto text = button.getButtonText();
    const auto textFont = font (big ? 18.0f : small ? 10.5f : tiny ? 9.0f : 13.0f, true, big ? 0.2f : tiny ? 0.08f : 0.12f);
    const auto textColour = ! button.isEnabled() ? juce::Colour (0xffb4bdc9) : on ? juce::Colours::white : juce::Colour (0xff2a3342);
    const auto area = button.getLocalBounds().toFloat().translated (0.0f, down ? 1.0f : 0.0f);   // la touche s'enfonce

    g.setFont (textFont);

    if (! big)
    {
        if (on)   // ombre du texte : il se détache du dégradé
        {
            g.setColour (juce::Colour (0xff0a3a8a).withAlpha (0.45f));
            g.drawText (text, area.translated (0.0f, 1.0f), juce::Justification::centred, false);
        }

        g.setColour (textColour);
        g.drawText (text, area, juce::Justification::centred, false);
        return;
    }

    // Grand bouton : deux barres obliques (témoin) puis le libellé, le tout centré.
    const float iconWidth = 14.0f, gap = 14.0f;
    const float labelWidth = textWidth (textFont, text);
    const float left = area.getCentreX() - (iconWidth + gap + labelWidth) * 0.5f;
    const float midY = area.getCentreY();

    juce::Path bars;
    bars.startNewSubPath (left + 1.0f, midY - 7.0f);
    bars.lineTo (left + 6.0f, midY + 7.0f);
    bars.startNewSubPath (left + 9.0f, midY - 7.0f);
    bars.lineTo (left + 14.0f, midY + 7.0f);

    const auto labelArea = juce::Rectangle<float> (left + iconWidth + gap, area.getY(), labelWidth + 12.0f, area.getHeight());

    if (on)
    {
        g.setColour (juce::Colour (0xff0a3a8a).withAlpha (0.45f));
        g.strokePath (bars, juce::PathStrokeType (3.0f), juce::AffineTransform::translation (0.0f, 1.5f));
        g.drawText (text, labelArea.translated (0.0f, 1.5f), juce::Justification::centredLeft, false);
    }

    g.setColour (on ? juce::Colours::white : juce::Colour (0xffa9b6c8));
    g.strokePath (bars, juce::PathStrokeType (3.0f));

    g.setColour (textColour);
    g.drawText (text, labelArea, juce::Justification::centredLeft, false);
}

//==============================================================================
StepLane::StepLane (PhractalAudioProcessor& p, Kind k) : audioProcessor (p), kind (k)
{
    setComponentID (kind == chances ? "chances" : "gatePattern");
    setRepaintsOnMouseActivity (false);
    auto& apvts = audioProcessor.apvts;

    for (int step = 0; step < Ids::numSteps; ++step)
    {
        auto* parameter = apvts.getParameter (kind == chances ? Ids::chanceId (step) : Ids::gateStepId (step));
        attachments[step] = std::make_unique<juce::ParameterAttachment> (*parameter, [this, step] (float value)
        {
            values[step] = value;
            repaint();
        });
    }

    countAttachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (kind == chances ? Ids::stepsId : Ids::gateStepsId),
                                                                   [this] (float value)
                                                                   {
                                                                       numShown = juce::jlimit (1, Ids::numSteps, juce::roundToInt (value));
                                                                       repaint();
                                                                   });

    powerAttachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (kind == chances ? Ids::sliceId : Ids::gateId),
                                                                   [this] (float value)
                                                                   {
                                                                       powered = value > 0.5f;
                                                                       repaint();
                                                                   });

    if (kind == gate)
        extraAttachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (Ids::gateLengthId), [this] (float value)
        {
            gateLength = 0.01f * value;
            repaint();
        });

    for (auto& attachment : attachments)
        attachment->sendInitialUpdate();

    countAttachment->sendInitialUpdate();
    powerAttachment->sendInitialUpdate();

    if (extraAttachment != nullptr)
        extraAttachment->sendInitialUpdate();

    startTimerHz (30);
}

StepLane::~StepLane()
{
    stopTimer();
    finishGestures();
}

void StepLane::refresh()
{
    const auto& engine = audioProcessor.getEngine();
    const int step = ! powered ? -1 : kind == chances ? engine.getCurrentStep() : engine.getGateStep();
    const auto mask = kind == chances ? engine.getSlicedMask() : 0u;

    if (step != lastStep || mask != lastMask)
    {
        lastStep = step;
        lastMask = mask;
        repaint();
    }
}

juce::Rectangle<float> StepLane::cellArea (int step) const
{
    const auto area = getLocalBounds().toFloat().reduced (6.0f, 5.0f);
    const float cell = area.getWidth() / (float) numShown;
    return { area.getX() + cell * (float) step, area.getY(), cell, area.getHeight() };
}

int StepLane::stepAt (float x) const
{
    const auto area = getLocalBounds().toFloat().reduced (6.0f, 5.0f);
    return juce::jlimit (0, numShown - 1, (int) std::floor ((x - area.getX()) / area.getWidth() * (float) numShown));
}

void StepLane::edit (const juce::MouseEvent& event, bool first)
{
    const int step = stepAt (event.position.x);
    const auto area = cellArea (step);

    if (kind == gate && first)
        paintValue = values[step] > 0.5f ? 0.0f : 1.0f;   // on peint l'inverse de la première case touchée

    // Chance : la hauteur du clic. Les 3 px du haut et du bas valent 100 % et 0 % : on y arrive sans viser au pixel.
    const auto band = area.reduced (0.0f, 3.0f);
    const float value = kind == chances
        ? std::round (juce::jlimit (0.0f, 100.0f, 100.0f * (band.getBottom() - event.position.y) / band.getHeight()))
        : paintValue;

    // Tous les pas entre le précédent et celui-ci : un geste rapide ne laisse pas de trou.
    const int from = (first || editingStep < 0) ? step : juce::jmin (editingStep, step);
    const int to = (first || editingStep < 0) ? step : juce::jmax (editingStep, step);

    for (int s = from; s <= to; ++s)
    {
        if (! touched[s])
        {
            touched[s] = true;
            attachments[s]->beginGesture();
        }

        attachments[s]->setValueAsPartOfGesture (value);
    }

    editingStep = step;
}

void StepLane::finishGestures()
{
    for (int s = 0; s < Ids::numSteps; ++s)
    {
        if (touched[s])
        {
            touched[s] = false;
            attachments[s]->endGesture();
        }
    }

    editingStep = -1;
}

void StepLane::mouseDown (const juce::MouseEvent& event)    { edit (event, true); }
void StepLane::mouseDrag (const juce::MouseEvent& event)    { edit (event, false); }
void StepLane::mouseUp (const juce::MouseEvent&)            { finishGestures(); }

void StepLane::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (kind == chances)   // double-clic : la chance revient à 50 %
        attachments[stepAt (event.position.x)]->setValueAsCompleteGesture (50.0f);
}

void StepLane::paint (juce::Graphics& g)
{
    // L'écran en creux est dessiné par le fond ; ici, les pas.
    const auto whole = getLocalBounds().toFloat().reduced (6.0f, 5.0f);
    const auto gradient = LF::accentGradient (whole.getTopLeft(), whole.getTopRight());
    const bool groupsOfFour = numShown % 4 == 0 && numShown > 4;

    for (int step = 0; step < numShown; ++step)
    {
        const auto cell = cellArea (step);

        // Séparations : plus marquées entre les groupes de quatre pas.
        if (step > 0)
        {
            g.setColour (groupsOfFour && step % 4 == 0 ? juce::Colour (0xffa9b4c2) : juce::Colour (0xffdde2e9));
            g.fillRect (juce::Rectangle<float> (cell.getX() - 0.5f, cell.getY(), 1.0f, cell.getHeight()));
        }

        if (step == lastStep)   // le pas en cours
        {
            g.setColour (LF::cyan().withAlpha (0.13f));
            g.fillRect (cell);
        }

        const auto inner = cell.reduced (juce::jmin (3.0f, cell.getWidth() * 0.12f), 0.0f);

        if (kind == chances)
        {
            const float level = juce::jlimit (0.0f, 1.0f, 0.01f * values[step]);
            const auto bar = inner.withTop (inner.getBottom() - level * inner.getHeight());

            if (level > 0.0f)
            {
                if (powered)
                    g.setGradientFill (gradient);
                else
                    g.setColour (LF::standby().withAlpha (0.75f));

                g.fillRect (bar.withTop (juce::jmin (bar.getY(), inner.getBottom() - 1.5f)));
            }

            // Ce pas est devenu une slice au dernier passage : un repère en haut.
            if (powered && ((lastMask >> step) & 1u) != 0)
            {
                g.setColour (LF::ink());
                g.fillRect (juce::Rectangle<float> (inner.getWidth() * 0.6f, 2.5f).withCentre ({ inner.getCentreX(), inner.getY() + 1.5f }));
            }
        }
        else
        {
            const bool on = values[step] > 0.5f;
            const auto box = inner.reduced (0.0f, 2.0f);
            const auto open = box.withWidth (juce::jmax (3.0f, box.getWidth() * juce::jlimit (0.0f, 1.0f, gateLength)));

            if (on)
            {
                if (powered)
                    g.setGradientFill (gradient);
                else
                    g.setColour (LF::standby().withAlpha (0.75f));

                g.fillRect (open);
            }
            else
            {
                g.setColour (juce::Colour (0xffc7d0db));
                g.drawRect (box, 1.0f);
            }
        }
    }
}

//==============================================================================
PhractalFacade::PhractalFacade (PhractalAudioProcessor& p)
    : audioProcessor (p),
      chanceLane (p, StepLane::chances),
      gateLane (p, StepLane::gate)
{
    setLookAndFeel (&lookAndFeel);
    setSize (width, height);

    auto& apvts = audioProcessor.apvts;

    // Grands potards : un par module (deux superposés pour la longueur : divisions ou durées).
    configureMainKnob (stepsKnob, Ids::stepsId, 0);
    configureMainKnob (maxDivisionKnob, Ids::maxDivisionId, 1);
    configureMainKnob (maxTimeKnob, Ids::maxTimeId, 1);
    configureMainKnob (pitchKnob, Ids::pitchId, 2);
    configureMainKnob (gateRateKnob, Ids::gateDivisionId, 3);
    configureMainKnob (cutoffKnob, Ids::filterCutoffId, 4);

    for (auto* knob : { &stepsKnob, &pitchKnob, &gateRateKnob, &cutoffKnob })
        knob->setVisible (true);

    // Petits potards des panneaux du bas
    configureMiniKnob (attackKnob, Ids::attackId, "ATT", miniKnobArea (0, 0, 4));
    configureMiniKnob (decayKnob, Ids::decayId, "DEC", miniKnobArea (0, 1, 4));
    configureMiniKnob (sustainKnob, Ids::sustainId, "SUS", miniKnobArea (0, 2, 4));
    configureMiniKnob (releaseKnob, Ids::releaseId, "REL", miniKnobArea (0, 3, 4));
    configureMiniKnob (minDivisionKnob, Ids::minDivisionId, "MIN", miniKnobArea (1, 0, 3));
    configureMiniKnob (minTimeKnob, Ids::minTimeId, "MIN", miniKnobArea (1, 0, 3));
    configureMiniKnob (mixKnob, Ids::mixId, "MIX", miniKnobArea (2, 0, 3));
    configureMiniKnob (levelKnob, Ids::levelId, "LEVEL", miniKnobArea (2, 1, 3));
    configureMiniKnob (gateStepsKnob, Ids::gateStepsId, "STEPS", miniKnobArea (3, 0, 3));
    configureMiniKnob (gateLengthKnob, Ids::gateLengthId, "LENGTH", miniKnobArea (3, 1, 3));
    configureMiniKnob (gateSmoothKnob, Ids::gateSmoothId, "SMOOTH", miniKnobArea (3, 2, 3));
    configureMiniKnob (resonanceKnob, Ids::filterResonanceId, "RESO", miniKnobArea (4, 1, 3));

    for (int column = 0; column < numColumns; ++column)
        configureValueLabel (valueLabels[column], column);

    // Grands boutons : des bascules. Les lumières de chaque module suivent son état.
    configureBigToggle (sliceButton, Ids::sliceId, 0, [this] (bool on) { sliceOn = on; setStandby (stepsKnob, ! on); });
    configureBigToggle (randomButton, Ids::randomId, 1, [this] (bool on) { randomOn = on; modesChanged(); });
    configureBigToggle (pitchButton, Ids::pitchOnId, 2, [this] (bool on) { pitchOn = on; setStandby (pitchKnob, ! on); });
    configureBigToggle (gateButton, Ids::gateId, 3, [this] (bool on)
    {
        gateOn = on;
        setStandby (gateRateKnob, ! on);

        for (auto* knob : { &gateStepsKnob, &gateLengthKnob, &gateSmoothKnob })
            setStandby (*knob, ! on);
    });
    configureBigToggle (filterButton, Ids::filterId, 4, [this] (bool on)
    {
        filterOn = on;
        setStandby (cutoffKnob, ! on);
        setStandby (resonanceKnob, ! on);
    });

    // Interrupteurs des baies
    configureSwitch (thruSwitch, Ids::thruId, { "SILENT", "INPUT", "RANDOM" }, switchArea (0, 62, columnWidth - 62 - switchOffset), true,
                     [this] (int) { repaint(); });
    configureSwitch (lengthModeSwitch, Ids::lengthModeId, { "FREE", "SYNC" }, switchArea (1, switchOffset, fullSwitchWidth), false,
                     [this] (int choice) { lengthMode = choice; modesChanged(); });
    configureSwitch (pitchModeSwitch, Ids::pitchModeId, { "SEMI", "FREE" }, switchArea (2, switchOffset, fullSwitchWidth), false,
                     [this] (int choice) { pitchInSemitones = choice == 0; updateValueLabels(); repaint(); });
    configureSwitch (gateFeelSwitch, Ids::gateFeelId, { "STRAIGHT", "TRIPLET", "DOTTED" }, switchArea (3, switchOffset, fullSwitchWidth), true,
                     [this] (int choice) { gateFeel = choice; updateValueLabels(); repaint(); });
    configureSwitch (filterTypeSwitch, Ids::filterTypeId, { "HP", "BP", "LP" }, switchArea (4, switchOffset, 112), false,
                     [this] (int choice) { filterType = choice; repaint(); });
    configureSwitch (filterSlopeSwitch, Ids::filterSlopeId, { "12 dB", "24 dB" }, switchArea (4, 132, 74), true,
                     [this] (int choice) { steepFilter = choice == 1; repaint(); });

    // Panneau RANGE : triolets et pointées dans le tirage
    configureSmallToggle (tripletButton, Ids::allowTripletId, panelKeyArea (1, 0, 2), [this] (bool on) { tripletsAllowed = on; repaint(); });
    configureSmallToggle (dottedButton, Ids::allowDottedId, panelKeyArea (1, 1, 2), [this] (bool on) { dottedAllowed = on; repaint(); });

    // Panneau OUTPUT
    configureSmallToggle (limiterButton, Ids::limiterId, panelKeyArea (2, 0, 1));

    // Pitch : par demi-tons (le potard se pose sur le demi-ton le plus proche quand on le lâche) ou libre.
    pitchAttachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (Ids::pitchId), [] (float) {});

    pitchKnob.onDragEnd = [this]
    {
        const auto value = (float) pitchKnob.getValue();

        if (pitchInSemitones && std::abs (value - std::round (value)) > 0.001f)
            pitchAttachment->setValueAsCompleteGesture (std::round (value));
    };

    // Séquence
    addAndMakeVisible (chanceLane);
    chanceLane.setBounds (chanceLaneArea);
    addAndMakeVisible (gateLane);
    gateLane.setBounds (gateLaneArea);

    // Ce que les afficheurs dessinent dépend aussi de ces réglages.
    for (const char* id : { Ids::attackId, Ids::decayId, Ids::sustainId, Ids::releaseId, Ids::gateStepsId, Ids::gateLengthId, Ids::gateSmoothId })
        stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (id), [this] (float) { repaint(); }));

    for (int step = 0; step < Ids::numSteps; ++step)
        stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (Ids::gateStepId (step)),
                                                                              [this] (float) { repaint (displayArea (3)); }));

    // Tout est en place : on aligne l'interface sur les paramètres.
    for (auto& watcher : stateWatchers)
        watcher->sendInitialUpdate();

    for (auto* selector : { &thruSwitch, &lengthModeSwitch, &pitchModeSwitch, &gateFeelSwitch, &filterTypeSwitch, &filterSlopeSwitch })
        selector->attachment->sendInitialUpdate();

    modesChanged();
    startTimerHz (30);
}

PhractalFacade::~PhractalFacade()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PhractalFacade::configureMainKnob (juce::Slider& knob, const char* parameterId, int column)
{
    addChildComponent (knob);
    knob.setComponentID (parameterId);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setWantsKeyboardFocus (false);
    knob.setBounds (knobArea (column));
    knob.onValueChange = [this] { updateValueLabels(); repaint(); };

    sliderAttachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, parameterId, knob));
}

void PhractalFacade::configureMiniKnob (juce::Slider& knob, const char* parameterId, const char* name, juce::Rectangle<int> area)
{
    addAndMakeVisible (knob);
    knob.setName (name);
    knob.setComponentID (parameterId);
    knob.getProperties().set ("mini", true);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setWantsKeyboardFocus (false);
    knob.setBounds (area);
    knob.onValueChange = [this] { repaint(); };   // afficheurs et valeurs

    sliderAttachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, parameterId, knob));
}

void PhractalFacade::configureValueLabel (juce::Label& label, int column)
{
    addAndMakeVisible (label);
    label.setComponentID ("value/" + juce::String (column));
    label.setJustificationType (juce::Justification::centredRight);
    label.setMinimumHorizontalScale (0.7f);
    label.setBounds (readoutArea (column).withTrimmedLeft (74).withTrimmedRight (9));

    // Saisie au clavier : la valeur tapée est appliquée au potard affiché dans ce module.
    label.onTextChange = [this, &label, column]
    {
        auto& knob = activeKnob (column);
        knob.setValue (knob.getValueFromText (label.getText()), juce::sendNotificationSync);
        updateValueLabels();
    };
}

void PhractalFacade::configureBigToggle (juce::TextButton& button, const char* parameterId, int column, std::function<void (bool)> onChange)
{
    addAndMakeVisible (button);
    button.setComponentID (parameterId);
    button.setWantsKeyboardFocus (false);
    button.getProperties().set ("cut", 16.0);
    button.getProperties().set ("big", true);
    button.setBounds (buttonArea (column));
    button.setClickingTogglesState (true);

    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (audioProcessor.apvts, parameterId, button));

    stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*audioProcessor.apvts.getParameter (parameterId),
                                                                          [this, onChange] (float value)
                                                                          {
                                                                              onChange (value > 0.5f);
                                                                              repaint();
                                                                          }));
}

void PhractalFacade::configureSmallToggle (juce::TextButton& button, const char* parameterId, juce::Rectangle<int> area, std::function<void (bool)> onChange)
{
    addAndMakeVisible (button);
    button.setComponentID (parameterId);
    button.setClickingTogglesState (true);
    button.setWantsKeyboardFocus (false);
    button.getProperties().set ("cut", 5.0);
    button.getProperties().set ("tiny", true);
    button.setBounds (area);

    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (audioProcessor.apvts, parameterId, button));

    if (onChange)
        stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*audioProcessor.apvts.getParameter (parameterId),
                                                                              [onChange] (float value) { onChange (value > 0.5f); }));
}

void PhractalFacade::configureSwitch (ChoiceSwitch& selector, const char* parameterId, const juce::StringArray& texts,
                                      juce::Rectangle<int> area, bool tiny, std::function<void (int)> onChange)
{
    const int count = texts.size();

    for (int i = 0; i < count; ++i)
    {
        auto* button = selector.buttons.add (new juce::TextButton (texts[i]));
        addAndMakeVisible (*button);

        // Identifiants "paramètre/0", "paramètre/1"… : pour retrouver chaque position (tests, accessibilité).
        button->setComponentID (juce::String (parameterId) + "/" + juce::String (i));
        button->setClickingTogglesState (false);
        button->setWantsKeyboardFocus (false);
        button->getProperties().set ("cut", tiny ? 5.0 : 6.0);
        button->getProperties().set (tiny ? "tiny" : "small", true);

        const int left = area.getX() + (area.getWidth() * i) / count;
        const int right = area.getX() + (area.getWidth() * (i + 1)) / count;
        button->setBounds (left, area.getY(), right - left, area.getHeight());
        button->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < count - 1 ? juce::Button::ConnectedOnRight : 0));
    }

    auto* buttons = &selector.buttons;

    selector.attachment = std::make_unique<juce::ParameterAttachment> (*audioProcessor.apvts.getParameter (parameterId),
                                                                       [buttons, onChange] (float value)
                                                                       {
                                                                           const int chosen = juce::roundToInt (value);

                                                                           for (int i = 0; i < buttons->size(); ++i)
                                                                               buttons->getUnchecked (i)->setToggleState (i == chosen, juce::dontSendNotification);

                                                                           onChange (chosen);
                                                                       });

    auto* attachment = selector.attachment.get();

    for (int i = 0; i < count; ++i)
        selector.buttons.getUnchecked (i)->onClick = [attachment, i] { attachment->setValueAsCompleteGesture ((float) i); };
}

juce::Slider& PhractalFacade::activeKnob (int column)
{
    switch (column)
    {
        case 0:   return stepsKnob;
        case 1:   return lengthMode == Ids::modeSync ? maxDivisionKnob : maxTimeKnob;
        case 2:   return pitchKnob;
        case 3:   return gateRateKnob;
        default:  return cutoffKnob;
    }
}

void PhractalFacade::setStandby (juce::Slider& knob, bool standby)
{
    // Réglage sans effet pour l'instant : le potard passe en veille (il reste réglable) et sa valeur s'estompe.
    knob.getProperties().set ("standby", standby);

    for (int column = 0; column < numColumns; ++column)
        if (knob.getBounds() == knobArea (column))
            valueLabels[column].setColour (juce::Label::textColourId, standby ? LF::dimInk() : LF::ink());

    knob.repaint();
}

void PhractalFacade::modesChanged()
{
    const bool synced = lengthMode == Ids::modeSync;

    maxDivisionKnob.setVisible (synced);
    maxTimeKnob.setVisible (! synced);
    minDivisionKnob.setVisible (synced);
    minTimeKnob.setVisible (! synced);

    // Sans tirage, la boucle fait toujours MAX : MIN, triolets et pointées ne servent pas.
    setStandby (minDivisionKnob, ! randomOn);
    setStandby (minTimeKnob, ! randomOn);
    tripletButton.setEnabled (synced && randomOn);
    dottedButton.setEnabled (synced && randomOn);

    // Les durées, les hauteurs et les fréquences se tapent au clavier ; les divisions et les nombres de pas se choisissent au potard.
    valueLabels[0].setEditable (false, false, false);
    valueLabels[1].setEditable (! synced, false, false);
    valueLabels[2].setEditable (true, false, false);
    valueLabels[3].setEditable (false, false, false);
    valueLabels[4].setEditable (true, false, false);

    updateValueLabels();
    repaint();
}

void PhractalFacade::updateValueLabels()
{
    for (int column = 0; column < numColumns; ++column)
    {
        auto& knob = activeKnob (column);
        double value = knob.getValue();

        // En mode demi-tons, le pitch affiché est celui qu'on entend : arrondi au demi-ton.
        if (column == 2 && pitchInSemitones)
            value = std::round (value);

        auto text = knob.getTextFromValue (value);

        if (column == 3)   // "1/16 T" = triolet, "1/16 D" = pointée
            text += gateFeel == Ids::triplet ? " T" : gateFeel == Ids::dotted ? " D" : "";

        valueLabels[column].setText (text, juce::dontSendNotification);
    }
}

void PhractalFacade::timerCallback()
{
    // Crêtes d'entrée et de sortie : montée immédiate, descente de 30 dB par seconde.
    const auto toLevel = [] (float peak) { return juce::jmax (0.0f, juce::Decibels::gainToDecibels (peak, -60.0f) + 60.0f); };
    const float in = toLevel (audioProcessor.takeInputPeak()), out = toLevel (audioProcessor.takeOutputPeak());
    const float newInput = juce::jmax (in, inputLevel - 1.0f), newOutput = juce::jmax (out, outputLevel - 1.0f);

    if (std::abs (newInput - inputLevel) > 0.01f || std::abs (newOutput - outputLevel) > 0.01f)
    {
        inputLevel = newInput;
        outputLevel = newOutput;
        repaint (displayArea (2));
    }

    // Dernière longueur tirée et tempo : l'afficheur RANGE les montre.
    const double length = audioProcessor.getEngine().getLastLength(), bpm = audioProcessor.getBpm();

    if (std::abs (length - shownLength) > 1.0e-9 || std::abs (bpm - shownBpm) > 1.0e-6)
    {
        shownLength = length;
        shownBpm = bpm;
        repaint (displayArea (1));
    }
}

//==============================================================================
void PhractalFacade::paint (juce::Graphics& g)
{
    // Le fond (plaques, ombres, textes fixes) est coûteux à dessiner : on le garde en image,
    // à la résolution réelle de l'écran, et on ne redessine à chaque fois que ce qui bouge.
    const float scale = juce::jlimit (0.5f, 4.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());

    if (! background.isValid() || std::abs (scale - backgroundScale) > 0.01f)
    {
        backgroundScale = scale;
        background = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) editorWidth * scale),
                                  juce::roundToInt ((float) editorHeight * scale), true);

        juce::Graphics layer (background);
        layer.addTransform (juce::AffineTransform::scale (scale));
        paintBackground (layer);
    }

    g.drawImage (background, getLocalBounds().toFloat());

    paintLights (g);
    paintModules (g);
}

void PhractalFacade::paintBackground (juce::Graphics& g)
{
    const auto full = juce::Rectangle<float> (0.0f, 0.0f, (float) editorWidth, (float) editorHeight);
    const float w = (float) editorWidth, mid = w * 0.5f;

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2e7ee), 0.0f, 0.0f, juce::Colour (0xffcbd3dd), 0.0f, (float) editorHeight, false));
    g.fillRect (full);

    // Plaque principale : posée sur le fond, avec un liseré clair le long du bord.
    const auto plateArea = full.reduced (10.0f);
    const auto plate = LF::chamferedRect (plateArea, 18.0f);
    LF::dropShadow (g, plate, 12.0f, 5.0f, 0.38f);
    LF::fillVertical (g, plate, plateArea, juce::Colour (0xfffcfdfe), juce::Colour (0xffeceff3));
    g.setColour (juce::Colour (0xffa9b4c2));
    g.strokePath (plate, juce::PathStrokeType (1.3f));
    g.setColour (juce::Colours::white);
    g.strokePath (LF::chamferedRect (plateArea.reduced (1.8f), 17.0f), juce::PathStrokeType (1.2f));
    g.setColour (juce::Colour (0xffdde2e9));
    g.strokePath (LF::chamferedRect (plateArea.reduced (6.0f), 15.0f), juce::PathStrokeType (1.0f));

    // Joints de l'en-tête et du pied : une rainure (trait sombre, reflet clair dessous)
    const auto headerSeam = polyline ({ { 16.0f, 64.0f }, { mid - 80.0f, 64.0f }, { mid - 68.0f, 76.0f },
                                        { mid + 68.0f, 76.0f }, { mid + 80.0f, 64.0f }, { w - 16.0f, 64.0f } });
    const auto footerSeam = polyline ({ { 16.0f, (float) footerY }, { w - 16.0f, (float) footerY } });

    for (const auto* seam : { &headerSeam, &footerSeam })
    {
        g.setColour (juce::Colours::white);
        g.strokePath (*seam, juce::PathStrokeType (1.2f), juce::AffineTransform::translation (0.0f, 1.2f));
        g.setColour (juce::Colour (0xffb9c3d0));
        g.strokePath (*seam, juce::PathStrokeType (1.2f));
    }

    static const char* const panelTitles[numColumns] = { "ENVELOPE", "RANGE", "OUTPUT", "GATE SHAPE", "RESPONSE" };

    const auto drawPanel = [&g] (juce::Rectangle<float> panel)
    {
        const auto panelShape = LF::chamferedRect (panel, 14.0f);
        LF::dropShadow (g, panelShape, 8.0f, 4.0f, 0.22f);
        LF::fillVertical (g, panelShape, panel, juce::Colour (0xfffbfcfd), juce::Colour (0xffe8ecf1));
        g.setColour (juce::Colour (0xffb1bcc9));
        g.strokePath (panelShape, juce::PathStrokeType (1.1f));
        g.setColour (juce::Colours::white);
        g.strokePath (LF::chamferedRect (panel.reduced (1.5f), 13.0f), juce::PathStrokeType (1.1f));
    };

    for (int column = 0; column < numColumns; ++column)
    {
        // Baie du potard : en creux dans la plaque.
        const auto bay = bayArea (column).toFloat();
        const auto bayShape = LF::chamferedRect (bay, 22.0f);
        g.setColour (juce::Colours::white);
        g.strokePath (bayShape, juce::PathStrokeType (1.4f), juce::AffineTransform::translation (0.0f, 1.4f));
        LF::fillVertical (g, bayShape, bay, juce::Colour (0xffdde2e9), juce::Colour (0xffebeef3));
        LF::innerShadow (g, bayShape, 10.0f, 4.0f, 0.34f);
        g.setColour (juce::Colour (0xffa9b4c2));
        g.strokePath (bayShape, juce::PathStrokeType (1.2f));

        // Ombre portée du potard : une large et douce, une courte au contact.
        const auto knob = knobArea (column).toFloat();
        const auto body = circle (knob.getCentre(), LF::mainKnobBodyRadius (knob));
        LF::dropShadow (g, body, 13.0f, 9.0f, 0.30f);
        LF::dropShadow (g, body, 3.0f, 2.0f, 0.30f);

        // Afficheur sous le potard
        LF::drawScreen (g, readoutArea (column).toFloat(), 8.0f);

        // Panneau du bas : posé sur la plaque.
        drawPanel (panelArea (column).toFloat());

        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (9.0f, false, 0.12f));
        g.drawText (panelTitles[column], panelArea (column).withTrimmedLeft (panelInset).withHeight (28), juce::Justification::centredLeft, false);

        LF::drawScreen (g, displayArea (column).toFloat(), 6.0f);
    }

    // Séquence : un panneau sur toute la largeur, deux écrans.
    drawPanel (bandArea.toFloat());
    LF::drawScreen (g, chanceLaneArea.toFloat(), 7.0f);
    LF::drawScreen (g, gateLaneArea.toFloat(), 6.0f);

    // Libellé THRU devant son interrupteur
    g.setColour (LF::dimInk());
    g.setFont (lookAndFeel.font (9.0f, false, 0.12f));
    g.drawText ("THRU", switchArea (0, switchOffset, 44), juce::Justification::centredLeft, false);

    // Ombres portées des boutons et des petits potards, noms des petits potards
    for (auto* child : getChildren())
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            const bool big = button->getProperties().contains ("big");
            const float cut = (float) (double) button->getProperties().getWithDefault ("cut", 8.0);
            const auto shape = LF::chamferedRect (button->getBounds().toFloat(), cut);

            if (big)
                LF::dropShadow (g, shape, 9.0f, 5.0f, 0.30f);

            LF::dropShadow (g, shape, big ? 3.0f : 2.0f, big ? 2.0f : 1.6f, big ? 0.30f : 0.34f);
        }
        else if (auto* knob = dynamic_cast<juce::Slider*> (child))
        {
            if (! knob->getProperties().contains ("mini"))
                continue;

            if (knob == &minTimeKnob)   // même place que MIN en SYNC : une seule ombre, un seul nom
                continue;

            const auto bounds = knob->getBounds().toFloat();
            LF::dropShadow (g, circle (bounds.getCentre(), LF::miniKnobBodyRadius (bounds)), 4.0f, 3.0f, 0.34f);

            g.setColour (LF::dimInk());
            g.setFont (lookAndFeel.font (9.0f, false, 0.1f));
            g.drawText (knob->getName(), juce::Rectangle<int> (66, 12).withCentre ({ knob->getBounds().getCentreX(), knob->getBottom() + 8 }),
                        juce::Justification::centred, false);
        }
    }

    // En-tête : nom du plugin, signature, et numéro de version à droite
    const auto titleFont = lookAndFeel.font (20.0f, true, 0.24f);
    const auto titleArea = juce::Rectangle<float> (36.0f, 16.0f, 420.0f, 48.0f);

    g.setColour (LF::ink());
    g.setFont (titleFont);
    g.drawText ("PHRACTAL", titleArea, juce::Justification::centredLeft, false);

    // La signature est posée sur la même ligne de base que le nom (d'où le décalage vers le bas).
    g.setColour (LF::dimInk());
    g.setFont (lookAndFeel.font (11.0f, false, 0.26f));
    g.drawText ("BY PHRAGMA", titleArea.withTrimmedLeft (textWidth (titleFont, "PHRACTAL") + 18.0f).translated (0.0f, 1.5f),
                juce::Justification::centredLeft, false);

    // Numéro de version : pour vérifier d'un coup d'œil quelle version le DAW a chargée
    g.setFont (lookAndFeel.font (11.0f, false, 0.2f));
    g.drawText ("V" PHRACTAL_VERSION, juce::Rectangle<float> ((float) editorWidth - 336.0f, 16.0f, 300.0f, 48.0f),
                juce::Justification::centredRight, false);
}

void PhractalFacade::paintLights (juce::Graphics& g)
{
    const bool moduleOn[numColumns] = { sliceOn, randomOn, pitchOn, gateOn, filterOn };
    const float pipY = (float) footerY + 11.0f;

    // Trait éteint : une rainure (reflet clair dessous).
    const auto strokeUnlit = [&g] (const juce::Path& path, const juce::PathStrokeType& stroke)
    {
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.strokePath (path, stroke, juce::AffineTransform::translation (0.0f, 1.2f));
        g.setColour (LF::unlit());
        g.strokePath (path, stroke);
    };

    for (int column = 0; column < numColumns; ++column)
    {
        const float x = (float) columnX (column);
        const float right = x + (float) columnWidth;

        // Néons d'angle de la baie, et voyants du pied
        const Pt leftStart { x + 13.0f, 150.0f }, leftEnd { x + 72.0f, 97.0f };
        const Pt rightStart { right - 13.0f, 150.0f }, rightEnd { right - 72.0f, 97.0f };
        const auto leftStrip = polyline ({ leftStart, { x + 13.0f, 112.0f }, { x + 28.0f, 97.0f }, leftEnd });
        const auto rightStrip = polyline ({ rightStart, { right - 13.0f, 112.0f }, { right - 28.0f, 97.0f }, rightEnd });

        juce::Path pips;
        pips.startNewSubPath (x + 12.0f, pipY);
        pips.lineTo (x + 24.0f, pipY);
        pips.startNewSubPath (x + 32.0f, pipY);
        pips.lineTo (x + 44.0f, pipY);

        const juce::PathStrokeType pipStroke (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const juce::PathStrokeType stripStroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        if (moduleOn[column])
        {
            LF::strokeGlowing (g, leftStrip, 3.0f, LF::accentGradient (leftStart, leftEnd));
            LF::strokeGlowing (g, rightStrip, 3.0f, LF::accentGradient (rightStart, rightEnd));
            LF::strokeGlowing (g, pips, 2.5f, LF::accentGradient ({ x + 12.0f, pipY }, { x + 44.0f, pipY }));

            const auto button = buttonArea (column).toFloat();
            drawHalo (g, LF::chamferedRect (button, 16.0f), LF::accentGradient (button.getTopLeft(), button.getTopRight(), true));
        }
        else
        {
            strokeUnlit (leftStrip, stripStroke);
            strokeUnlit (rightStrip, stripStroke);
            strokeUnlit (pips, pipStroke);
        }
    }

    // Barres obliques du haut : allumées quand les slices tournent
    const float mid = (float) editorWidth * 0.5f;
    juce::Path leftBars, rightBars;

    for (const float offset : { 46.0f, 34.0f, 22.0f })
    {
        leftBars.startNewSubPath (mid - offset, 30.0f);
        leftBars.lineTo (mid - offset + 9.0f, 58.0f);
        rightBars.startNewSubPath (mid + offset, 30.0f);
        rightBars.lineTo (mid + offset - 9.0f, 58.0f);
    }

    if (sliceOn)
    {
        LF::strokeGlowing (g, leftBars, 4.0f, LF::accentGradient ({ mid - 46.0f, 44.0f }, { mid - 13.0f, 44.0f }), false);
        LF::strokeGlowing (g, rightBars, 4.0f, LF::accentGradient ({ mid + 46.0f, 44.0f }, { mid + 13.0f, 44.0f }), false);
    }
    else
    {
        strokeUnlit (leftBars, juce::PathStrokeType (4.0f));
        strokeUnlit (rightBars, juce::PathStrokeType (4.0f));
    }
}

void PhractalFacade::paintRange (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Haut : la plage, en notes (ou en ms) et en durées au tempo actuel ; une boucle très courte est donnée en Hz.
    // Bas : toutes les longueurs possibles, celles du tirage allumées, la dernière tirée marquée.
    const double secondsPerBeat = 60.0 / juce::jmax (1.0, shownBpm);
    const bool synced = lengthMode == Ids::modeSync;
    const auto textArea = area.removeFromTop (14.0f);
    const auto axis = area.reduced (2.0f, 0.0f).withTrimmedTop (4.0f);

    const auto describe = [] (double seconds)
    {
        return seconds < toneThresholdSeconds ? hertzText (1.0 / seconds) : durationText (seconds);
    };

    double longestSeconds = 0.0, shortestSeconds = 0.0;
    juce::String span;

    if (synced)
    {
        const int longest = juce::roundToInt (maxDivisionKnob.getValue()), shortest = juce::roundToInt (minDivisionKnob.getValue());
        const auto& names = Ids::getDivisionNames();
        longestSeconds = Ids::getDivisionInBeats (longest) * secondsPerBeat;
        shortestSeconds = Ids::getDivisionInBeats (randomOn ? shortest : longest) * secondsPerBeat;
        span = randomOn && shortest != longest ? names[longest] + " - " + names[shortest] : names[longest];

        int indices[64];
        const int count = Ids::collectDivisions (longest, shortest, randomOn, tripletsAllowed, dottedAllowed, indices);
        const int numDivisions = Ids::getNumDivisions();

        for (int i = 0; i < numDivisions; ++i)
        {
            const float x = axis.getX() + axis.getWidth() * (float) i / (float) (numDivisions - 1);
            const bool straight = Ids::getDivisionFeel (i) == Ids::straight;
            const bool chosen = std::find (indices, indices + count, i) != indices + count;
            const float tick = straight ? axis.getHeight() * 0.7f : axis.getHeight() * 0.4f;
            const auto mark = juce::Rectangle<float> (chosen ? 2.6f : 1.2f, tick).withCentre ({ x, axis.getBottom() - tick * 0.5f });

            g.setColour (chosen ? (sliceOn ? LF::accentAt ((float) i / (float) (numDivisions - 1)) : LF::standby()) : juce::Colour (0xffc7d0db));
            g.fillRect (mark);
        }
    }
    else
    {
        const double longest = maxTimeKnob.getValue(), shortest = randomOn ? minTimeKnob.getValue() : longest;
        longestSeconds = 0.001 * longest;
        shortestSeconds = 0.001 * shortest;
        span = randomOn ? compactValue (minTimeKnob.getTextFromValue (shortest)) + " - " + compactValue (maxTimeKnob.getTextFromValue (longest))
                        : compactValue (maxTimeKnob.getTextFromValue (longest));

        // Axe logarithmique de 1 s (gauche) à 0,1 ms (droite), comme le potard.
        const auto xFor = [axis] (double ms)
        {
            const double position = std::log (1000.0 / juce::jlimit (0.1, 1000.0, ms)) / std::log (10000.0);
            return axis.getX() + (float) position * axis.getWidth();
        };

        for (const double decade : { 1000.0, 100.0, 10.0, 1.0, 0.1 })
        {
            g.setColour (juce::Colour (0xffc7d0db));
            g.fillRect (juce::Rectangle<float> (1.2f, axis.getHeight() * 0.7f).withCentre ({ xFor (decade), axis.getBottom() - axis.getHeight() * 0.35f }));
        }

        const float from = xFor (juce::jmax (longest, shortest)), to = xFor (juce::jmin (longest, shortest));
        const auto bar = juce::Rectangle<float> (from, axis.getBottom() - axis.getHeight() * 0.5f, juce::jmax (2.6f, to - from), axis.getHeight() * 0.3f);

        if (sliceOn)
            g.setGradientFill (LF::accentGradient (bar.getTopLeft(), bar.getTopRight()));
        else
            g.setColour (LF::standby());

        g.fillRect (bar);
    }

    // Dernière longueur tirée
    if (shownLength > 0.0 && sliceOn)
    {
        const double seconds = shownLength / juce::jmax (1.0, audioProcessor.getCurrentSampleRate());
        double position;

        if (synced)
        {
            const double beats = seconds / secondsPerBeat;
            position = std::log (4.0 / juce::jmax (1.0e-6, beats)) / std::log (4096.0);   // 1/1 à gauche, 1/4096 à droite
        }
        else
        {
            position = std::log (1.0 / juce::jlimit (1.0e-4, 1.0, seconds)) / std::log (10000.0);
        }

        const float x = axis.getX() + (float) juce::jlimit (0.0, 1.0, position) * axis.getWidth();
        juce::Path marker;
        marker.addTriangle (x - 3.5f, axis.getY() - 3.0f, x + 3.5f, axis.getY() - 3.0f, x, axis.getY() + 2.5f);
        g.setColour (LF::ink());
        g.fillPath (marker);
    }

    g.setFont (lookAndFeel.font (10.5f, true));
    g.setColour (sliceOn ? LF::ink() : LF::dimInk());
    g.drawText (span, textArea, juce::Justification::centredLeft, false);

    const auto durations = juce::approximatelyEqual (shortestSeconds, longestSeconds)
        ? describe (longestSeconds)
        : describe (shortestSeconds) + " - " + describe (longestSeconds);

    g.setFont (lookAndFeel.font (9.5f, false));
    g.setColour (LF::dimInk());
    g.drawText (durations, textArea, juce::Justification::centredRight, false);
}

void PhractalFacade::paintMeters (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Deux barres, entrée et sortie, de -60 à 0 dBFS ; le trait du bout marque 0 dB.
    const float rowHeight = area.getHeight() / 2.0f;

    for (int row = 0; row < 2; ++row)
    {
        const auto line = area.withHeight (rowHeight).translated (0.0f, rowHeight * (float) row);
        const auto label = line.withWidth (28.0f);
        const auto track = line.withTrimmedLeft (32.0f).withSizeKeepingCentre (line.getWidth() - 32.0f, 6.0f);
        const float level = juce::jlimit (0.0f, 1.0f, (row == 0 ? inputLevel : outputLevel) / 60.0f);

        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (9.0f, false, 0.1f));
        g.drawText (row == 0 ? "IN" : "OUT", label, juce::Justification::centredLeft, false);

        g.setColour (juce::Colour (0xffdde2e9));
        g.fillRect (track);

        if (level > 0.0f)
        {
            g.setGradientFill (LF::accentGradient (track.getTopLeft(), track.getTopRight()));
            g.fillRect (track.withWidth (track.getWidth() * level));
        }

        g.setColour (LF::dimInk().withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (1.0f, 10.0f).withCentre ({ track.getRight(), track.getCentreY() }));
    }
}

void PhractalFacade::paintModules (juce::Graphics& g)
{
    // --- Libellés des afficheurs ---
    {
        static const char* const names[numColumns] = { "STEPS", "MAX", "PITCH", "RATE", "CUTOFF" };

        g.setColour (LF::dimInk());
        g.setFont (lookAndFeel.font (10.0f, false, 0.12f));

        for (int column = 0; column < numColumns; ++column)
            g.drawText (names[column], readoutArea (column).withTrimmedLeft (13), juce::Justification::centredLeft, false);
    }

    // --- Enveloppe d'une slice, sur la durée d'un pas ---
    {
        const auto plot = displayArea (0).toFloat().reduced (9.0f, 7.0f);
        const double a = attackKnob.getValue() * 0.01, d = decayKnob.getValue() * 0.01;
        const double s = sustainKnob.getValue() * 0.01, r = releaseKnob.getValue() * 0.01;
        const int numPoints = 120;
        juce::Path curve;

        for (int i = 0; i <= numPoints; ++i)
        {
            const double phase = (double) i / numPoints;
            const Pt point { plot.getX() + (float) phase * plot.getWidth(),
                             plot.getBottom() - (float) SliceEngine::envelopeAt (phase, a, d, s, r, 0.02) * plot.getHeight() };

            if (i == 0)
                curve.startNewSubPath (point);
            else
                curve.lineTo (point);
        }

        plotCurve (g, plot, curve, sliceOn);
    }

    // --- Plage des longueurs ---
    paintRange (g, displayArea (1).toFloat().reduced (9.0f, 5.0f));

    // --- Niveaux ---
    paintMeters (g, displayArea (2).toFloat().reduced (10.0f, 6.0f));

    // --- Gate : le niveau sur tout le motif, calculé comme le moteur (ouverture, longueur, lissage) ---
    {
        const auto plot = displayArea (3).toFloat().reduced (9.0f, 7.0f);
        const int numGateSteps = juce::roundToInt (gateStepsKnob.getValue());
        const double length = gateLengthKnob.getValue() * 0.01, smooth = gateSmoothKnob.getValue() * 0.01;
        const int numPoints = 220;
        const double perPoint = (double) numGateSteps / numPoints;              // en pas
        const double slope = perPoint / juce::jmax (0.004, smooth * 0.5);      // une rampe de lissage x 0,5 pas
        double gain = 0.0;
        juce::Path curve;

        // Un premier tour pour partir de l'état où le motif boucle.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i <= numPoints; ++i)
            {
                const double position = juce::jmin ((double) numGateSteps - 1.0e-9, (double) i * perPoint);
                const int step = (int) position;
                const bool on = audioProcessor.apvts.getRawParameterValue (Ids::gateStepId (step))->load() > 0.5f;
                const double target = on && position - step < length ? 1.0 : 0.0;
                gain = target > gain ? juce::jmin (target, gain + slope) : juce::jmax (target, gain - slope);

                if (pass == 1)
                {
                    const Pt point { plot.getX() + (float) i / (float) numPoints * plot.getWidth(), plot.getBottom() - (float) gain * plot.getHeight() };

                    if (i == 0)
                        curve.startNewSubPath (point);
                    else
                        curve.lineTo (point);
                }
            }
        }

        plotCurve (g, plot, curve, gateOn, 1.6f);
    }

    // --- Filtre : courbe de réponse, de 20 Hz à 20 kHz ---
    {
        const auto plot = displayArea (4).toFloat().reduced (9.0f, 8.0f);
        const double cutoff = cutoffKnob.getValue();
        const double quality = OutputFilter::qualityFor (resonanceKnob.getValue() * 0.01);
        const int numPoints = 110;
        juce::Path curve;

        for (int i = 0; i <= numPoints; ++i)
        {
            const double position = (double) i / numPoints;
            const double frequency = OutputFilter::minFrequency * std::pow (OutputFilter::maxFrequency / OutputFilter::minFrequency, position);
            const double decibels = 20.0 * std::log10 (juce::jmax (1.0e-5, OutputFilter::magnitudeAt (frequency, cutoff, quality, filterType, steepFilter)));
            const double level = juce::jlimit (0.0, 1.0, (decibels + 36.0) / 56.0);   // de -36 dB (bas) à +20 dB (haut)
            const Pt point { plot.getX() + (float) position * plot.getWidth(), plot.getBottom() - (float) level * plot.getHeight() };

            if (i == 0)
                curve.startNewSubPath (point);
            else
                curve.lineTo (point);
        }

        plotCurve (g, plot, curve, filterOn);
    }

    // --- Séquence : libellés à gauche des deux rangées ---
    {
        const auto labelColumn = [] (juce::Rectangle<int> lane) { return juce::Rectangle<int> (firstColumnX + 14, lane.getY(), 88, lane.getHeight()); };
        const int steps = juce::roundToInt (stepsKnob.getValue());
        const int gateSteps = juce::roundToInt (gateStepsKnob.getValue());

        for (int row = 0; row < 2; ++row)
        {
            // Titre et sous-titre, centrés ensemble sur la hauteur de la rangée.
            const auto area = labelColumn (row == 0 ? chanceLaneArea : gateLaneArea).toFloat();
            const auto title = juce::Rectangle<float> (area.getX(), area.getCentreY() - 15.0f, area.getWidth(), 16.0f);

            g.setColour (LF::ink());
            g.setFont (lookAndFeel.font (12.0f, true, 0.14f));
            g.drawText (row == 0 ? "CHANCE" : "GATE", title, juce::Justification::centredLeft, false);

            g.setColour (LF::dimInk());
            g.setFont (lookAndFeel.font (9.0f, false, 0.08f));
            g.drawText (row == 0 ? juce::String (steps) + (steps > 1 ? " STEPS / BAR" : " STEP / BAR")
                                 : juce::String (gateSteps) + " x " + valueLabels[3].getText(),
                        title.translated (0.0f, 16.0f).withHeight (13.0f), juce::Justification::centredLeft, false);
        }
    }

    // Valeurs des petits potards (leurs noms sont dans le fond)
    g.setFont (lookAndFeel.font (10.5f, true));

    for (auto* knob : { &attackKnob, &decayKnob, &sustainKnob, &releaseKnob, lengthMode == Ids::modeSync ? &minDivisionKnob : &minTimeKnob,
                        &mixKnob, &levelKnob, &gateStepsKnob, &gateLengthKnob, &gateSmoothKnob, &resonanceKnob })
    {
        const bool standby = (bool) knob->getProperties().getWithDefault ("standby", false);
        g.setColour (standby ? LF::dimInk() : LF::ink());
        g.drawText (compactValue (knob->getTextFromValue (knob->getValue())),
                    juce::Rectangle<int> (60, 12).withCentre ({ knob->getBounds().getCentreX(), knob->getBottom() + 20 }),
                    juce::Justification::centred, false);
    }
}

//==============================================================================
PhractalAudioProcessorEditor::PhractalAudioProcessorEditor (PhractalAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), facade (p)
{
    addAndMakeVisible (facade);

    // Taille gardée avec le projet (lue avant de régler les limites : les régler redimensionne déjà la fenêtre).
    const int savedWidth = (int) audioProcessor.apvts.state.getProperty ("editorWidth", PhractalFacade::width);

    // Fenêtre redimensionnable de 60 % à 150 %, proportions gardées.
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (PhractalFacade::width * 0.6), juce::roundToInt (PhractalFacade::height * 0.6),
                     juce::roundToInt (PhractalFacade::width * 1.5), juce::roundToInt (PhractalFacade::height * 1.5));
    getConstrainer()->setFixedAspectRatio ((double) PhractalFacade::width / (double) PhractalFacade::height);

    const int shownWidth = juce::jlimit (juce::roundToInt (PhractalFacade::width * 0.6), juce::roundToInt (PhractalFacade::width * 1.5), savedWidth);
    setSize (shownWidth, juce::roundToInt ((double) shownWidth * PhractalFacade::height / PhractalFacade::width));
    sized = true;
    resized();
}

PhractalAudioProcessorEditor::~PhractalAudioProcessorEditor() = default;

void PhractalAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) PhractalFacade::width;
    facade.setTransform (juce::AffineTransform::scale (scale));

    if (sized)
        audioProcessor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}
