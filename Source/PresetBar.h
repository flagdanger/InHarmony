#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"
#include "HarmonyLookAndFeel.h"
#include "PresetManager.h"

// box drawn over editor for presets
class PresetDialog : public juce::Component
{
public:


    PresetDialog()
    {
        titleLabel.setFont(ui::font(18.0f));
        messageLabel.setFont(ui::font(15.0f));
        messageLabel.setColour(juce::Label::textColourId, ui::subtle);

        nameEditor.setFont(ui::font(16.0f));
        nameEditor.onReturnKey = [this] { finish(true); };
        nameEditor.onEscapeKey = [this] { finish(false); };

        okButton.onClick = [this] { finish(true); };
        cancelButton.setButtonText("Cancel");
        cancelButton.onClick = [this] { finish(false); };

        for (auto* component : std::initializer_list<juce::Component*>{ &titleLabel, &messageLabel, &nameEditor, &okButton, &cancelButton })
            addAndMakeVisible(component);

        setWantsKeyboardFocus(true);
    }

    void askForName(const juce::String& title, const juce::String& initialName, const juce::String& okText,
        std::function<void(const juce::String&)> onOk)
    {
        textCallback = std::move(onOk);
        confirmCallback = nullptr;
        open(title, "Name", okText, true);

        nameEditor.setText(initialName, false);
        nameEditor.selectAll();
        nameEditor.grabKeyboardFocus();
    }


    void confirm(const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void()> onOk)
    {
        confirmCallback = std::move(onOk);
        textCallback = nullptr;
        open(title, message, okText, false);
        grabKeyboardFocus();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(ui::paper.withAlpha(0.75f)); // fades the plugin behind

        const auto box = boxBounds().toFloat().reduced(ui::outline * 0.5f);
        g.setColour(ui::panel);
        g.fillRoundedRectangle(box, 6.0f);
        g.setColour(ui::ink);
        g.drawRoundedRectangle(box, 6.0f, ui::outline);
    }


    void resized() override
    {
        auto area = boxBounds().reduced(16, 12);

        titleLabel.setBounds(area.removeFromTop(26));
        area.removeFromTop(4);
        messageLabel.setBounds(area.removeFromTop(22));

        if (showNameField)
        {
            area.removeFromTop(4);
            nameEditor.setBounds(area.removeFromTop(28));
        }

        auto buttons = area.removeFromBottom(28);
        okButton.setBounds(buttons.removeFromRight(90));
        buttons.removeFromRight(8);
        cancelButton.setBounds(buttons.removeFromRight(90));
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) { finish(false); return true; }
        if (key == juce::KeyPress::returnKey) { finish(true); return true; }
        return false;
    }

    // clicks outside the box don't touch the main plugin
    void mouseDown(const juce::MouseEvent&) override {} 

private:

    juce::Rectangle<int> boxBounds() const 
    { 
        return getLocalBounds().withSizeKeepingCentre(340, showNameField ? 176 : 140); 
    }


    void open(const juce::String& title, const juce::String& message, const juce::String& okText, bool withNameField)
    {
        showNameField = withNameField;
        titleLabel.setText(title, juce::dontSendNotification);
        messageLabel.setText(message, juce::dontSendNotification);
        okButton.setButtonText(okText);
        nameEditor.setVisible(withNameField);

        setVisible(true);
        toFront(false);
        resized();
        repaint();
    }

    // callbacks are taken out first, so they can open the dialog again like "Replace?" after naming
    void finish(bool accepted)
    {
        auto onText = std::move(textCallback);
        auto onConfirm = std::move(confirmCallback);
        textCallback = nullptr;
        confirmCallback = nullptr;
        const auto text = nameEditor.getText();

        setVisible(false);

        if (!accepted)
            return;

        if (onText)
            onText(text);
        else if (onConfirm)
            onConfirm();
    }


    juce::Label titleLabel, messageLabel;
    juce::TextEditor nameEditor;
    juce::TextButton okButton, cancelButton;
    bool showNameField = false;

    std::function<void(const juce::String&)> textCallback;
    std::function<void()> confirmCallback;

};


// one preset row in the menu 
// a tick when it's loaded, and an X on (only :/) user presets to delete them
class PresetMenuItem : public juce::PopupMenu::CustomComponent
{
public:
    PresetMenuItem(const juce::String& nameToShow, bool isCurrent, std::function<void()> onDeleteClicked)
        : juce::PopupMenu::CustomComponent(false), // clicks are handled below, so the X doesn't also load the preset
        name(shorten(nameToShow)), current(isCurrent), onDelete(std::move(onDeleteClicked))
    {
    }


    void getIdealSize(int& width, int& height) override
    {
        width = juce::roundToInt(juce::GlyphArrangement::getStringWidth(ui::font(15.0f), name)) + 70;
        height = 26;
    }


    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        if (isItemHighlighted())
        {
            g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle(area.reduced(HarmonyLookAndFeel::menuItemInset - 2.0f, 1.0f), 4.0f);
        }

        g.setColour(ui::ink);

        const auto tickArea = area.removeFromLeft(24.0f);
        if (current)
        {
            auto tick = getLookAndFeel().getTickShape(1.0f);
            g.fillPath(tick, tick.getTransformToScaleToFit(tickArea.reduced(7.0f), true));
        }

        if (onDelete)
        {
            const auto cross = deleteArea().toFloat().reduced(8.0f);
            g.setColour(hoveringDelete ? ui::ink : ui::subtle.withAlpha(0.6f));
            juce::Path path;
            path.addLineSegment({ cross.getTopLeft(), cross.getBottomRight() }, 0.0f);
            path.addLineSegment({ cross.getTopRight(), cross.getBottomLeft() }, 0.0f);
            g.strokePath(path, juce::PathStrokeType(hoveringDelete ? 2.0f : ui::outline, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            area.removeFromRight((float)deleteArea().getWidth());
        }

        g.setColour(ui::ink);
        g.setFont(ui::font(15.0f));
        g.drawText(name, area.withTrimmedRight(6.0f), juce::Justification::centredLeft, true);
    }

    void mouseMove(const juce::MouseEvent& e) override { setHoveringDelete(onDelete != nullptr && deleteArea().contains(e.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHoveringDelete(false); }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (onDelete != nullptr && deleteArea().contains(e.getPosition()))
        {
            auto callback = onDelete; // copied, since closing the menu deletes this row
            juce::PopupMenu::dismissAllActiveMenus();
            callback();
        }
        else
        {
            triggerMenuItem();
        }
    }


private:

    // long names stop at 30 characters, so they don't stretch the menu
    static juce::String shorten(const juce::String& fullName)
    {
        constexpr int maxCharacters = 30;
        return fullName.length() > maxCharacters ? fullName.substring(0, maxCharacters).trimEnd() + "..." : fullName;
    }

    juce::Rectangle<int> deleteArea() const
    {
        return getLocalBounds().withTrimmedRight((int)HarmonyLookAndFeel::menuItemInset - 2).removeFromRight(getHeight());
    }

    void setHoveringDelete(bool shouldHover)
    {
        if (hoveringDelete != shouldHover)
        {
            hoveringDelete = shouldHover;
            repaint();
        }
    }

    juce::String name;
    bool current = false;
    bool hoveringDelete = false;
    std::function<void()> onDelete;
};


// header preset controls: < [name *] > Save
class PresetBar : public juce::Component, private juce::Timer
{
public:

    PresetBar(PresetManager& managerToUse, PresetDialog& dialogToUse)
        : manager(managerToUse), dialog(dialogToUse)
    {
        previousButton.setButtonText("<");
        nextButton.setButtonText(">");
        saveButton.setButtonText("Save");

        previousButton.onClick = [this] { manager.loadNeighbour(-1); timerCallback(); };
        nextButton.onClick = [this] { manager.loadNeighbour(1); timerCallback(); };
        saveButton.onClick = [this] { askToSave(); };

        addAndMakeVisible(previousButton);
        addAndMakeVisible(nextButton);
        addAndMakeVisible(saveButton);

        shownName = manager.getDisplayName();
        startTimerHz(10); // picks up the * when anything changes
    }

    ~PresetBar() override { stopTimer(); }


    void paint(juce::Graphics& g) override
    {
        const auto box = boxArea.toFloat().reduced(ui::outline * 0.5f);

        g.setColour(ui::panel);
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(ui::ink);
        g.drawRoundedRectangle(box, 4.0f, ui::outline);

        g.setFont(ui::font(15.0f));
        g.drawText(shownName, box.reduced(10.0f, 0.0f).withTrimmedRight(16.0f), juce::Justification::centredLeft, true);

        // same arrow as the dropdowns
        const auto arrow = juce::Rectangle<float>(box.getRight() - 20.0f, box.getY(), 12.0f, box.getHeight()).withSizeKeepingCentre(9.0f, 5.0f);
        juce::Path path;
        path.startNewSubPath(arrow.getX(), arrow.getY());
        path.lineTo(arrow.getCentreX(), arrow.getBottom());
        path.lineTo(arrow.getRight(), arrow.getY());
        g.strokePath(path, juce::PathStrokeType(ui::outline, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }


    void resized() override
    {
        auto area = getLocalBounds();

        saveButton.setBounds(area.removeFromRight(64));
        area.removeFromRight(8);
        nextButton.setBounds(area.removeFromRight(28));
        area.removeFromRight(4);
        previousButton.setBounds(area.removeFromLeft(28));
        area.removeFromLeft(4);
        boxArea = area;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (boxArea.contains(e.getPosition()))
            showMenu();
    }


private:

    static constexpr int noneId = 100000, openFolderId = 100001;

    void timerCallback() override
    {
        const auto name = manager.getDisplayName();

        if (name != shownName)
        {
            shownName = name;
            repaint(boxArea);
        }
    }


    void showMenu()
    {
        manager.refresh();
        const auto& presets = manager.getPresets();
        const auto current = manager.getCurrentIndex();

        juce::PopupMenu menu;
        menu.setLookAndFeel(&getLookAndFeel());

        bool factoryHeader = false, userHeader = false;

        for (size_t i = 0; i < presets.size(); ++i)
        {
            const auto& preset = presets[i];
            const auto isUser = preset.kind == PresetManager::Kind::user;

            if (preset.kind == PresetManager::Kind::factory && !factoryHeader)
            {
                menu.addSectionHeader("Factory");
                factoryHeader = true;
            }

            if (isUser && !userHeader)
            {
                menu.addSectionHeader("My Presets");
                userHeader = true;
            }

            //only user presets get an X, open the delete screen once preset menu closes
            std::function<void()> onDelete;
            if (isUser)
                onDelete = [safeThis = juce::Component::SafePointer<PresetBar>(this), name = preset.name]
                {
                    juce::MessageManager::callAsync([safeThis, name]
                        {
                            if (safeThis != nullptr)
                                safeThis->askToDelete(name);
                        });
                };

            menu.addCustomItem((int)i + 1, std::make_unique<PresetMenuItem>(preset.name, (int)i == current, std::move(onDelete)));

        }

        if (!userHeader)
        {
            menu.addSectionHeader("My Presets");
            menu.addItem(noneId, "(none saved yet)", false);
        }

        menu.addSeparator();
        menu.addCustomItem(openFolderId, std::make_unique<PresetMenuItem>("Open Presets Folder", false, nullptr));


        menu.showMenuAsync(juce::PopupMenu::Options()
            .withTargetComponent(this)
            .withTargetScreenArea(localAreaToGlobal(boxArea).translated(0, 3))
            .withMinimumWidth(boxArea.getWidth()),
            [safeThis = juce::Component::SafePointer<PresetBar>(this)](int result)
            {
                if (safeThis != nullptr && result != 0)
                    safeThis->handleMenuResult(result);
            });

    }


    void handleMenuResult(int result)
    {
        if (result == openFolderId)
            manager.openUserFolder();
        else if (result != noneId)
            manager.loadPreset(result - 1);

        timerCallback();
    }

    void askToSave()
    {
        const auto suggestion = manager.currentIsUser() ? manager.getCurrentName() : juce::String();

        dialog.askForName("Save Preset", suggestion, "Save", [this](const juce::String& typed)
            {
                const auto name = juce::File::createLegalFileName(typed.trim());

                if (name.isEmpty())
                    return;

                if (manager.userPresetExists(name))
                    dialog.confirm("Replace \"" + name + "\"?", "A preset with this name already exists.", "Replace",
                        [this, name] { save(name); });
                else
                    save(name);
            });
    }


    void save(const juce::String& name)
    {
        if (!manager.savePreset(name))
            dialog.confirm("Couldn't save", "The presets folder couldn't be written to.", "OK", [] {});

        timerCallback();
    }

    void askToDelete(const juce::String& name)
    {
        dialog.confirm("Delete \"" + name + "\"?", "This can't be undone.", "Delete",
            [this, name] { manager.deleteUserPreset(name); timerCallback(); });
    }


    PresetManager& manager;
    PresetDialog& dialog;
    juce::TextButton previousButton, nextButton, saveButton;
    juce::Rectangle<int> boxArea;
    juce::String shownName;
};