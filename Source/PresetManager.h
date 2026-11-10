#pragma once

#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <algorithm>


// 3 kinds, init (default values, factory presets, and user
// excluding correction key info
class PresetManager : private juce::AudioProcessorParameter::Listener
{
public:
	enum class Kind { init, factory, user };

    struct Preset
    {
        juce::String name;
        Kind kind = Kind::init;
        juce::File file;            // user
        juce::String resourceName;  // factory
    };


    explicit PresetManager(juce::AudioProcessorValueTreeState& stateToUse) : state(stateToUse)
    {
        for (auto* parameter : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (!isExcluded(ranged->paramID))
                {
                    presetParameters.add(ranged);
                    ranged->addListener(this);
                }

        refresh();
    }

    ~PresetManager() override
    {
        for (auto* parameter : presetParameters)
            parameter->removeListener(this);
    }

    static juce::File getUserFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("In Harmony").getChildFile("Presets");
    }


    // rebuilds the list: Init, factory (A-Z), user (A-Z)
    void refresh()
    {
        presets.clear();
        presets.push_back({ "Init", Kind::init,{},{} });

        std::vector<Preset> factory, user;

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const juce::String original(BinaryData::originalFilenames[i]);

            if (original.endsWithIgnoreCase(".xml"))
                factory.push_back({ original.upToLastOccurrenceOf(".", false, false), Kind::factory,{}, BinaryData::namedResourceList[i] });
        }


        for (const auto& file : getUserFolder().findChildFiles(juce::File::findFiles, false, "*.xml"))
            user.push_back({ file.getFileNameWithoutExtension(), Kind::user, file,{} });


        auto byName = [](const Preset& a, const Preset& b) { return a.name.compareNatural(b.name) < 0; };
        std::sort(factory.begin(), factory.end(), byName);
        std::sort(user.begin(), user.end(), byName);

        presets.insert(presets.end(), factory.begin(), factory.end());
        presets.insert(presets.end(), user.begin(), user.end());
    }

    const std::vector<Preset>& getPresets() const noexcept { return presets; }

    int getCurrentIndex() const
    {
        const auto name = getCurrentName();

        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == name)
                return (int)i;

        return -1;
    }

    juce::String getCurrentName() const
    {
        const juce::ScopedLock lock(nameLock);
        return currentName;
    }

    bool isModified() const noexcept 
    {
        return modified.load(); 
    }

    juce::String getDisplayName() const
    {
        const auto name = getCurrentName();
        return (name.isEmpty() ? juce::String("Untitled") : name) + (isModified() ? " *" : "");
    }

    bool currentIsUser() const
    {
        const auto index = getCurrentIndex();
        return index >= 0 && presets[(size_t)index].kind == Kind::user;
    }

    bool userPresetExists(const juce::String& name) const 
    { 
        return getUserFolder().getChildFile(name + ".xml").existsAsFile(); 
    }


    void loadPreset(int index)
    {
        if (!juce::isPositiveAndBelow(index, (int)presets.size()))
            return;

        const auto preset = presets[(size_t)index];
        const auto xml = readPreset(preset);

        // missing or unreadable file
        if (preset.kind != Kind::init && xml == nullptr)
            return;


        loading = true;
        for (auto* parameter : presetParameters)
        {
            // anything the preset doesn't mention (init, or presets older than a parameter) goes to its default
            auto target = parameter->getDefaultValue();

            if (xml != nullptr) {
                if (auto* saved = xml->getChildByAttribute("id", parameter->paramID))
                    target = parameter->convertTo0to1((float)saved->getDoubleAttribute("value"));
            }

            if (std::abs(parameter->getValue() - target) > 1.0e-6f)
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(target);
                parameter->endChangeGesture();
            }

        }

        loading = false;
        setCurrent(preset.name, false);
    }


    // go through the whole list and wrap at the ends
    void loadNeighbour(int step)
    {
        refresh();
        const auto count = (int)presets.size();
        const auto current = getCurrentIndex();
        loadPreset(current < 0 ? 0 : (current + step + count) % count);
    }

    bool savePreset(const juce::String& name)
    {
        const auto folder = getUserFolder();

        if (folder.createDirectory().failed())
            return false;

        juce::XmlElement xml("InHarmonyPreset");
        xml.setAttribute("version", 1);

        // real values ms hz etc so presets stay readable
        for (auto* parameter : presetParameters)
        {
            auto* element = xml.createNewChildElement("Param");
            element->setAttribute("id", parameter->paramID);
            element->setAttribute("value", parameter->convertFrom0to1(parameter->getValue()));
        }

        if (!xml.writeTo(folder.getChildFile(name + ".xml")))
            return false;

        refresh();

        setCurrent(name, false);
        return true;
    }

    // delete presets by name, if currently loaded leave parameters alone, but unsave it
    void deleteUserPreset(const juce::String& name)
    {
        getUserFolder().getChildFile(name + ".xml").deleteFile();
        refresh();

        if (name == getCurrentName())
            setCurrent({}, true);
    }

    void openUserFolder()
    {
        const auto folder = getUserFolder();
        folder.createDirectory();
        folder.startAsProcess();
    }

    // after the host restores a session
    void restore(const juce::String& name, bool wasModified) { setCurrent(name, wasModified); }

private:


    static bool isExcluded(const juce::String& id)
    {
        return id == "gate" || id == "correctKey" || id == "correctScale" || id.startsWith("note");
    }

    void setCurrent(const juce::String& name, bool isModifiedNow)
    {
        {
            const juce::ScopedLock lock(nameLock);
            currentName = name;
        }
        modified = isModifiedNow;
    }


    std::unique_ptr<juce::XmlElement> readPreset(const Preset& preset) const
    {

        if (preset.kind == Kind::factory)
        {
            int size = 0;
            if (const auto* data = BinaryData::getNamedResource(preset.resourceName.toRawUTF8(), size))
                return juce::parseXML(juce::String::fromUTF8(data, size));

            return nullptr;
        }

        if (preset.kind == Kind::user)
            return juce::parseXML(preset.file);


        return nullptr;
    }

    // any change to parameters marks the preset as modified
    void parameterValueChanged(int, float) override
    {
        if (!loading)
            modified = true;
    }

    void parameterGestureChanged(int, bool) override {}


    juce::AudioProcessorValueTreeState& state;
    juce::Array<juce::RangedAudioParameter*> presetParameters;
    std::vector<Preset> presets;

    juce::CriticalSection nameLock;
    juce::String currentName{ "Init" };
    std::atomic<bool> modified{ false };
    std::atomic<bool> loading{ false };
};
