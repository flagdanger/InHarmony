#pragma once

#include <vector>
#include <cstddef>

class BreathExtract
{
public:
	void prepare(double newSampleRate, std::size_t outputDelaySamples) noexcept;
	void reset() noexcept;

	float processSample(float input, float pitchHz, float confidence) noexcept;

private:
	float readDelayed(double delaySamples) const noexcept;

	double sampleRate = 44100.0;
	double highpassHz = 1500.0;

	std::vector<float> history;
	std::size_t historyPosition = 0;

	std::vector<float> delayLine;
	std::size_t delayPosition = 0;

	float voicedAmount = 0.0f;
	float voicedCoefficient = 0.0f;
	double lastPeriod = 0.0f;

	float b0 = 0.0f;
	float b1 = 0.0f;
	float b2 = 0.0f;

	float a1 = 0.0f;
	float a2 = 0.0f;

	float z1 = 0.0f;
	float z2 = 0.0f;
};