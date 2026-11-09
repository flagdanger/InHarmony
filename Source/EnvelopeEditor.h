#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"
#include "ShapedEnvelope.h"

#include <array>

// draggable ADSR graph
class EnvelopeEditor : public juce::Component
{
public:
    explicit EnvelopeEditor(juce::AudioProcessorValueTreeState& state)
    {
        const char* ids[] = { "attack", "decay", "sustain", "release", "attackCurve", "decayCurve", "releaseCurve" };

        for (size_t i = 0; i < 7; ++i)
        {
            parameters[i] = state.getParameter(ids[i]);

            // keeps the drawing in sync when the value changes from anywhere (host, preset, typing)
            attachments[i] = std::make_unique<juce::ParameterAttachment>(*parameters[i],
                [this, i](float newValue) { values[i] = newValue; repaint(); });
            attachments[i]->sendInitialUpdate();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto points = handlePositions();
        const auto plot = plotArea();

        // baseline
        g.setColour(ui::divider);
        g.drawHorizontalLine(juce::roundToInt(plot.getBottom()), plot.getX(), plot.getRight());

        // envelope shape: three curves and then flat sustain slope
        const auto parts = segments();
        juce::Path shape;
        shape.startNewSubPath(plot.getBottomLeft());
        addCurvedSegment(shape, parts[0]);   // attack
        addCurvedSegment(shape, parts[1]);   // decay
        shape.lineTo(points[2]);             // sustain
        addCurvedSegment(shape, parts[2]);   // release


        juce::Path fill(shape);
        fill.closeSubPath();
        g.setColour(ui::envelopeTheme.withAlpha(0.12f));
        g.fillPath(fill);

        g.setColour(ui::envelopeTheme);
        g.strokePath(shape, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // handles
        for (int i = 0; i < (int)points.size(); ++i)
        {
            const auto dot = juce::Rectangle<float>(10.0f, 10.0f).withCentre(points[(size_t)i]);
            g.setColour(i == hovered || i == dragging ? ui::envelopeTheme : ui::panel);
            g.fillEllipse(dot);
            g.setColour(ui::envelopeTheme);
            g.drawEllipse(dot, 2.0f);
        }

        // curve handles
        for (int s = 0; s < 3; ++s)
        {
            if (!canBend(parts[(size_t)s]))
                continue;

            const auto handleIndex = 4 + s;
            const auto dot = juce::Rectangle<float>(8.0f, 8.0f).withCentre(curveHandlePosition(parts[(size_t)s]));
            const auto active = handleIndex == hovered || handleIndex == dragging;

            g.setColour(ui::envelopeTheme.withAlpha(active ? 1.0f : 0.35f));
            g.fillEllipse(dot);
        }

        // values underneath
        auto row = getLocalBounds().removeFromBottom(18).toFloat();
        const auto quarter = row.getWidth() / 4.0f;
        const char* names[] = { "Attack: ", "Decay: ", "Sustain: ", "Release: " };
        const char* units[] = { "", "", " %", "" }; // times format their own unit

        g.setFont(ui::font(12.0f));
        g.setColour(ui::subtle);

        for (size_t i = 0; i < 4; ++i)
            g.drawText(names[i] + parameters[i]->getCurrentValueAsText() + units[i],
                row.removeFromLeft(quarter), juce::Justification::centred);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto newHovered = handleAt(e.position);

        if (newHovered != hovered)
        {
            hovered = newHovered;
            setMouseCursor(hovered >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hovered = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging = handleAt(e.position);

        // a gesture tells the host one continuous edit is happening
        // important for automation recording and undo
        for (auto index : parametersFor(dragging))
            attachments[(size_t)index]->beginGesture();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;

        const auto plot = plotArea();
        const auto points = handlePositions();

        // the time handles move along their own segment, which starts at the previous point
        auto setTime = [&](int parameterIndex, float segmentStartX)
            {
                const auto proportion = juce::jlimit(0.0f, 1.0f, (e.position.x - segmentStartX) / segmentWidth());
                attachments[(size_t)parameterIndex]->setValueAsPartOfGesture(parameters[(size_t)parameterIndex]->convertFrom0to1(proportion));
            };

        auto setSustain = [&]
            {
                const auto proportion = juce::jlimit(0.0f, 1.0f, (plot.getBottom() - e.position.y) / plot.getHeight());
                attachments[2]->setValueAsPartOfGesture(parameters[2]->convertFrom0to1(proportion));
            };

        switch (dragging)
        {
        case 0: setTime(0, plot.getX()); break;                 // peak: attack
        case 1: setTime(1, points[0].x); setSustain(); break;   // knee: decay and sustain
        case 2: setSustain(); break;                            // end of sustain line: sustain
        case 3: setTime(3, points[2].x); break;                 // last point: release
        case 4: case 5: case 6:
        {
            // how far along the segment's height the mouse is, at the midpoint
            const auto segment = segments()[(size_t)(dragging - 4)];
            const auto s = juce::jlimit(0.05f, 0.95f, (e.position.y - segment.from.y) / (segment.to.y - segment.from.y));

            // shape(0.5) = 1 / (e^(k/2) + 1), solved for k, then curve = k / 6
            const auto curve = juce::jlimit(-1.0f, 1.0f, 2.0f * std::log(1.0f / s - 1.0f) / 6.0f);
            attachments[(size_t)dragging]->setValueAsPartOfGesture(curve * 100.0f);
            break;
        }
        default: break;
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        for (auto index : parametersFor(dragging))
            attachments[(size_t)index]->endGesture();

        dragging = -1;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        for (auto index : parametersFor(handleAt(e.position)))
        {
            auto* parameter = parameters[(size_t)index];
            attachments[(size_t)index]->setValueAsCompleteGesture(parameter->convertFrom0to1(parameter->getDefaultValue()));
        }
    }

private:
    // drawing area
    juce::Rectangle<float> plotArea() const
    {
        // leave room for value row under
        return getLocalBounds().toFloat().withTrimmedBottom(22.0f).reduced(8.0f, 8.0f);
    }

    // attack, decay and release each get the same slot width
    float segmentWidth() const { return plotArea().getWidth() * 0.28f; }

    // the sustain hold is a fixed width flat line
    float sustainHoldWidth() const { return plotArea().getWidth() * 0.16f; }

    // peak, knee, end of sustain, end of release
    std::array<juce::Point<float>, 4> handlePositions() const
    {
        const auto plot = plotArea();

        // dial-style proportions (0 - 1 of each parameter's range), so short times get more room, like the dials
        auto proportion = [this](size_t i) { return parameters[i]->convertTo0to1(values[i]); };

        const auto sustainY = plot.getBottom() - plot.getHeight() * proportion(2);
        const auto peak = juce::Point<float>(plot.getX() + segmentWidth() * proportion(0), plot.getY());
        const auto knee = juce::Point<float>(peak.x + segmentWidth() * proportion(1), sustainY);
        const auto sustainEnd = juce::Point<float>(knee.x + sustainHoldWidth(), sustainY);
        const auto end = juce::Point<float>(sustainEnd.x + segmentWidth() * proportion(3), plot.getBottom());

        return { peak, knee, sustainEnd, end };
    }

    // for the three sloped segments
    struct Segment { juce::Point<float> from, to; size_t curveIndex; };

    std::array<Segment, 3> segments() const
    {
        const auto points = handlePositions();
        return { Segment{ plotArea().getBottomLeft(), points[0], 4 },
                 Segment{ points[0], points[1], 5 },
                 Segment{ points[2], points[3], 6 } };
    }

    float curveOf(const Segment& segment) const { return values[segment.curveIndex] * 0.01f; }

    // a segment drawn with its bend, using the same shape function as the sound
    void addCurvedSegment(juce::Path& path, const Segment& segment) const
    {
        constexpr int steps = 24;

        for (int i = 1; i <= steps; ++i)
        {
            const auto t = (float)i / (float)steps;
            path.lineTo(segment.from.x + (segment.to.x - segment.from.x) * t,
                segment.from.y + (segment.to.y - segment.from.y) * ShapedEnvelope::shape(t, curveOf(segment)));
        }
    }

    // the curve handle sits on the curve, halfway along the segment
    juce::Point<float> curveHandlePosition(const Segment& segment) const
    {
        return { segment.from.x + (segment.to.x - segment.from.x) * 0.5f,
                 segment.from.y + (segment.to.y - segment.from.y) * ShapedEnvelope::shape(0.5f, curveOf(segment)) };
    }

    // too short or too flat to bend (e.g. decay with sustain at 100%): no curve handle
    static bool canBend(const Segment& segment)
    {
        return std::abs(segment.to.x - segment.from.x) >= 14.0f && std::abs(segment.to.y - segment.from.y) >= 4.0f;
    }

    // which handle is under the mouse (within 10 px), or -1
    int handleAt(juce::Point<float> position) const
    {
        const auto points = handlePositions();
        const auto parts = segments();

        int closest = -1;
        float closestDistance = 10.0f;

        auto consider = [&](int index, juce::Point<float> point)
            {
                const auto distance = point.getDistanceFrom(position);

                if (distance < closestDistance)
                {
                    closestDistance = distance;
                    closest = index;
                }
            };

        for (int i = 0; i < 4; ++i)
            consider(i, points[(size_t)i]);

        for (int s = 0; s < 3; ++s)
            if (canBend(parts[(size_t)s]))
                consider(4 + s, curveHandlePosition(parts[(size_t)s]));

        return closest;
    }

    // which parameters each handle edits
    static std::vector<int> parametersFor(int handle)
    {
        switch (handle)
        {
        case 0: return { 0 };
        case 1: return { 1, 2 };
        case 2: return { 2 };
        case 3: return { 3 };
        case 4: return { 4 };
        case 5: return { 5 };
        case 6: return { 6 };
        default: return {};
        }
    }

    std::array<juce::RangedAudioParameter*, 7> parameters{};
    std::array<float, 7> values{};
    std::array<std::unique_ptr<juce::ParameterAttachment>, 7> attachments; // last, so destroyed first

    int hovered = -1;
    int dragging = -1;
};