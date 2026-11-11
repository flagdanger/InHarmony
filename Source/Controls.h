#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"

// a dial with its name above and its value below, attached to one parameter
class LabelledDial : public juce::Component
{
public:
    LabelledDial (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                  const juce::String& name, const juce::String& suffix)
    {
        nameLabel.setText (name, juce::dontSendNotification);
        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setFont (ui::font (17.0f, true));
        addAndMakeVisible (nameLabel);

        dial.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        dial.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
        dial.setTextValueSuffix (suffix);
        addAndMakeVisible (dial);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, dial);

        // double-click goes back to the parameter's default
        if (auto* parameter = state.getParameter (parameterId))
            dial.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }

    juce::Slider& getSlider() noexcept { return dial; }

    void setArcColour(juce::Colour colour) { dial.setColour(juce::Slider::rotarySliderFillColourId, colour); }

    void resized() override
    {
        auto area = getLocalBounds();
        nameLabel.setBounds (area.removeFromTop (22));
        dial.setBounds (area);
    }

private:
    juce::Label nameLabel;
    juce::Slider dial;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment; // after the slider, destroyed first
};

// a slider that only takes mouse clicks on one half, so two can share one bar
// a slider that only takes mouse clicks on one half of its width, so two can share one bar
class SideSlider : public juce::Slider
{
public:
    enum class Side { left, right };
    explicit SideSlider(Side sideToUse) : side(sideToUse) {}

    bool hitTest(int x, int) override
    {
        return side == Side::left ? x < getWidth() / 2 : x >= getWidth() / 2;
    }

private:
    Side side;
};

// gain fader and limiter ceiling sharing one meter bar, with a value box for each underneath
class FaderStrip : public juce::Component
{
public:

    FaderStrip(juce::AudioProcessorValueTreeState& state, const juce::String& gainId,
        const juce::String& ceilingId, const juce::String& name)
    {
        nameLabel.setText(name, juce::dontSendNotification);
        nameLabel.setJustificationType(juce::Justification::centred);
        nameLabel.setFont(ui::font(14.0f));
        addAndMakeVisible(nameLabel);

        // peak
        peakLabel.setJustificationType(juce::Justification::centred);
        peakLabel.setFont(ui::font(13.0f));
        peakLabel.setColour(juce::Label::textColourId, ui::subtle);
        peakLabel.setText("-inf", juce::dontSendNotification);
        addAndMakeVisible(peakLabel);

        // both sliders share the bar, ceiling is added first, so its underneath
        for (auto* slider : { &ceiling, &fader })
        {
            slider->setSliderStyle(juce::Slider::LinearVertical);
            slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            addAndMakeVisible(slider);
        }

        ceiling.getProperties().set("ceilingHandle", true);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, gainId, fader);
        ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, ceilingId, ceiling);
        fader.setDoubleClickReturnValue(true, 0.0);
        ceiling.setDoubleClickReturnValue(true, 0.0);

        setUpValueBox(gainCaption, gainBox, "gain", fader);
        setUpValueBox(limitCaption, limitBox, "limit", ceiling);
    }

    // where a dB value sits on this strip, in the parent's coordinates (the editor draws the dB scale with these)
    float getYForDb(float db) { return (float)(getY() + fader.getY()) + fader.getPositionOfValue(db); }
    float getBarCentreX() const { return (float)(getX() + fader.getBounds().getCentreX()); }

    // peak of the signal this fader controls (linear gain); the meter uses the fader's own db scale,
    // so the meter top lines up with the thumb at the same db value
    void setMeterLevel(float linearPeak)
    {
        const auto db = juce::Decibels::gainToDecibels(linearPeak, -100.0f);
        const auto proportion = db <= -60.0f ? 0.0
            : fader.valueToProportionOfLength(juce::jlimit(-60.0, 6.0, static_cast<double> (db)));

        if (std::abs(proportion - shownProportion) > 0.002)
        {
            shownProportion = proportion;
            repaint(barBounds().getSmallestIntegerContainer()); // only the bar area needs redrawing
        }

        // peak readout holds the loudest value for 2 seconds, then follows the level down
        if (db >= heldPeakDb)
        {
            heldPeakDb = db;
            holdTicksRemaining = peakHoldTicks;
        }
        else if (holdTicksRemaining > 0)
        {
            --holdTicksRemaining;
        }
        else
        {
            heldPeakDb = juce::jmax(db, heldPeakDb - 1.0f);
        }

        const auto text = heldPeakDb <= -60.0f ? juce::String("-inf") : juce::String(heldPeakDb, 1);

        if (text != peakLabel.getText())
        {
            peakLabel.setText(text, juce::dontSendNotification);
            // red when the readout is anything that rounds to 0.0 or above
            peakLabel.setColour(juce::Label::textColourId, heldPeakDb >= -0.05f ? ui::clip : ui::subtle);
        }
    }

    void paint(juce::Graphics& g) override
    {
        // bar, meter and outline sit underneath both handles
        const auto bar = barBounds();

        g.setColour(ui::panel);
        g.fillRect(bar);

        if (shownProportion > 0.0)
        {
            g.setColour(ui::meterFill);
            g.fillRect(bar.withTop(bar.getBottom() - bar.getHeight() * (float)shownProportion));
        }

        g.setColour(ui::ink);
        g.drawRect(bar, ui::outline);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        nameLabel.setBounds(area.removeFromTop(20));
        peakLabel.setBounds(area.removeFromTop(18));
        area.removeFromTop(4);

        // values with units ("-37.7 dB") are too wide to sit side by side, so the two boxes stack:
        // gain caption, gain value, limit caption, limit value
        limitBox.setBounds(area.removeFromBottom(17));
        limitCaption.setBounds(area.removeFromBottom(12));
        area.removeFromBottom(2);
        gainBox.setBounds(area.removeFromBottom(17));
        gainCaption.setBounds(area.removeFromBottom(12));
        area.removeFromBottom(6);

        // identical bounds, both handles share one pixel scale
        fader.setBounds(area);
        ceiling.setBounds(area);
    }

private:
    // caption above, editable number below, kept in sync with its slider both ways
    void setUpValueBox(juce::Label& caption, juce::Label& box, const juce::String& captionText, juce::Slider& slider)
    {
        caption.setText(captionText, juce::dontSendNotification);
        caption.setJustificationType(juce::Justification::centred);
        caption.setFont(ui::font(11.0f));
        caption.setColour(juce::Label::textColourId, ui::subtle);
        addAndMakeVisible(caption);

        box.setJustificationType(juce::Justification::centred);
        box.setFont(ui::font(12.0f));
        box.setColour(juce::Label::textColourId, ui::ink);
        box.setEditable(true); // click to type a value
        addAndMakeVisible(box);

        // slider -> box: show the parameter's own text for the value
        auto showValue = [&box, &slider] { box.setText(slider.getTextFromValue(slider.getValue()), juce::dontSendNotification); };
        slider.onValueChange = showValue;

        // box -> slider: typed text gets clamped by range
        box.onTextChange = [&box, &slider] { slider.setValue(slider.getValueFromText(box.getText())); };

        showValue();
    }

    // the bar spans exactly the thumb's travel, so the meter, thumb and "0" mark share one scale
    juce::Rectangle<float> barBounds()
    {
        constexpr float barWidth = 12.0f;
        const auto top = (float)fader.getY() + fader.getPositionOfValue(fader.getMaximum());
        const auto bottom = (float)fader.getY() + fader.getPositionOfValue(fader.getMinimum());

        return { (float)fader.getBounds().getCentreX() - barWidth * 0.5f, top, barWidth, bottom - top };
    }

    static constexpr int peakHoldTicks = 40; // 2s at the editor's 20 hz meter refresh

    juce::Label nameLabel, peakLabel;
    juce::Label gainCaption, limitCaption, gainBox, limitBox;
    SideSlider fader{ SideSlider::Side::left };
    SideSlider ceiling{ SideSlider::Side::right };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment, ceilingAttachment; // last, destroyed first

    double shownProportion = 0.0;
    float heldPeakDb = -100.0f;
    int holdTicksRemaining = 0;
};

// switch for a choice parameter, one option lit at a time
class ChoiceSwitch : public juce::Component
{
public:
    ChoiceSwitch(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
    {
        auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter(parameterId));
        jassert(parameter != nullptr); // parameterId must be an AudioParameterChoice

        options = parameter->choices;
        attachment = std::make_unique<juce::ParameterAttachment>(*parameter,
            [this](float index) { selected = juce::roundToInt(index); repaint(); });
        attachment->sendInitialUpdate();


        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setAccentColour(juce::Colour newColour) { accent = newColour; repaint(); }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(ui::outline * 0.5f);
        const auto segmentWidth = area.getWidth() / (float)options.size();

        for (int i = 0; i < options.size(); ++i)
        {
            const auto segment = juce::Rectangle<float>(area.getX() + (float)i * segmentWidth, area.getY(),
                segmentWidth, area.getHeight());

            const auto isSelected = i == selected;

            if (isSelected)
            {
                g.setColour(accent);
                g.fillRect(segment);
            }

            // selected text stays black (bold), matching the dial titles
            g.setColour(ui::ink);
            g.setFont(ui::font(13.0f, isSelected));
            g.drawText(options[i], segment, juce::Justification::centred);
        }

        // dividers drawn after the fills, centred exactly on each boundary at the outline's width,
        // so they cover the fill's edge instead of leaving a sliver of white beside it
        g.setColour(ui::ink);

        for (int i = 1; i < options.size(); ++i)
        {
            const auto boundaryX = area.getX() + (float)i * segmentWidth;
            g.fillRect(juce::Rectangle<float>(boundaryX - ui::outline * 0.5f, area.getY(), ui::outline, area.getHeight()));
        }

        g.setColour(ui::ink);
        g.drawRect(area, ui::outline);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto index = juce::jlimit(0, options.size() - 1, (int)(e.position.x / ((float)getWidth() / (float)options.size())));
        attachment->setValueAsCompleteGesture((float)index);
    }

private:
    juce::StringArray options;
    int selected = 0;
    juce::Colour accent = ui::ink;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};


// vertical on/off switch with its name above
class VerticalToggle : public juce::Component
{
public:
    VerticalToggle(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& nameToShow)
        : name(nameToShow)
    {
        attachment = std::make_unique<juce::ParameterAttachment>(*state.getParameter(parameterId),
            [this](float value) { on = juce::roundToInt(value) == 1; repaint(); });
        attachment->sendInitialUpdate();
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setOnColour(juce::Colour colour) { onColour = colour; repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        g.setColour(ui::ink);
        g.setFont(ui::font(17.0f));
        g.drawText(name, area.removeFromTop(22.0f), juce::Justification::centred);

        const auto track = trackBounds();
        const auto radius = track.getWidth() * 0.5f;

        g.setColour(on ? onColour : ui::panel);
        g.fillRoundedRectangle(track, radius);
        g.setColour(ui::ink);
        g.drawRoundedRectangle(track, radius, ui::outline);

        // knob up = on  down = off
        const auto knobSize = track.getWidth() - 8.0f;
        const auto knob = juce::Rectangle<float>(knobSize, knobSize)
            .withCentre({ track.getCentreX(), on ? track.getY() + 4.0f + knobSize * 0.5f
                                                 : track.getBottom() - 4.0f - knobSize * 0.5f });
        g.setColour(ui::panel);
        g.fillEllipse(knob);
        g.setColour(ui::ink);
        g.drawEllipse(knob, ui::outline);

        g.setColour(ui::subtle);
        g.setFont(ui::font(13.0f));
        g.drawText(on ? "On" : "Off", juce::Rectangle<float>(area.getX(), track.getBottom() + 4.0f, area.getWidth(), 18.0f),
            juce::Justification::centred);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains(e.getPosition()))
            attachment->setValueAsCompleteGesture(on ? 0.0f : 1.0f);
    }

private:
    juce::Rectangle<float> trackBounds() const
    {
        const auto area = getLocalBounds().toFloat().withTrimmedTop(26.0f).withTrimmedBottom(24.0f);
        return area.withSizeKeepingCentre(26.0f, juce::jmin(72.0f, area.getHeight()));
    }

    juce::String name;
    bool on = false;
    juce::Colour onColour{ ui::meterFill };
    std::unique_ptr<juce::ParameterAttachment> attachment; // last, destroyed first
};