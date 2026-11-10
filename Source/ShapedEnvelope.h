#pragma once

#include <cmath>

// replacement ASDR for curved slopes
// curve 0 = straight, > 0 = starts slowly and speeds up, < 0 = starts fast and eases in
class ShapedEnvelope
{
public:
    struct Parameters
    {
        float attackSeconds = 0.01f;
        float decaySeconds = 0.2f;
        float sustainLevel = 1.0f;
        float releaseSeconds = 0.1f;

        float attackCurve = 0.0f;  // -1 - 1
        float decayCurve = 0.0f;
        float releaseCurve = 0.0f;
    };

    float getCurrentLevel() const noexcept { return level; }

    // progress t (0 - 1) through a segment, bent by curve
    // share with the editor so drawing matches sound
    static float shape(float t, float curve) noexcept
    {
        const auto k = curve * 6.0f;

        if (std::abs(k) < 1.0e-3f)
            return t;

        // shape(t) = ( e^(k * t) - 1) / (e^k - 1)
        return (std::exp(k * t) - 1.0f) / (std::exp(k) - 1.0f);
    }

    void setSampleRate(double newSampleRate) noexcept { sampleRate = newSampleRate; }
    void setParameters(const Parameters& newParameters) noexcept { parameters = newParameters; }

    void noteOn() noexcept { startStage(Stage::attack); }

    void noteOff() noexcept
    {
        if (stage != Stage::idle)
            startStage(Stage::release); // releases from wherever the level is, even during attack
    }

    void reset() noexcept
    {
        stage = Stage::idle;
        level = 0.0f;
    }

    bool isActive() const noexcept { return stage != Stage::idle; }

    float getNextSample() noexcept
    {
        switch (stage)
        {
        case Stage::idle:
            return 0.0f;

        case Stage::attack: // from wherever it was up to full
            if (advance(parameters.attackSeconds)) {
                level = 1.0f;
                startStage(Stage::decay);
            }
            else {
                level = startLevel + (1.0f - startLevel) * shape(progress, parameters.attackCurve);
            }
            break;

        case Stage::decay: // full down to the sustain level
            if (advance(parameters.decaySeconds)) {
                level = parameters.sustainLevel;
                stage = Stage::sustain;
            }
            else {
                level = 1.0f - (1.0f - parameters.sustainLevel) * shape(progress, parameters.decayCurve);
            }
            break;

        case Stage::sustain: // follows the sustain setting while the key is held
            level = parameters.sustainLevel;
            break;

        case Stage::release: // from wherever it was down to silence
            if (advance(parameters.releaseSeconds)) {
                level = 0.0f;
                stage = Stage::idle;
            }
            else {
                level = startLevel * (1.0f - shape(progress, parameters.releaseCurve));
            }
            break;
        }

        return level;
    }

private:
    enum class Stage { idle, attack, decay, sustain, release };

    void startStage(Stage newStage) noexcept
    {
        stage = newStage;
        startLevel = level;
        progress = 0.0f;
    }

    // steps through the current stage, returns true once finished
    bool advance(float seconds) noexcept
    {
        if (seconds <= 0.0f)
            return true;

        progress += 1.0f / (seconds * static_cast<float>(sampleRate));
        return progress >= 1.0f;
    }

    Parameters parameters;
    double sampleRate = 44100.0;

    Stage stage = Stage::idle;
    float level = 0.0f;
    float startLevel = 0.0f;
    float progress = 0.0f;
};