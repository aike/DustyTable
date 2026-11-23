#pragma once

#include "dsp/VinylEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

class DustyTableProcessor : public juce::AudioProcessor, private juce::AsyncUpdater {
public:
    DustyTableProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    dustytable::EngineParams readParams() const;

    juce::AudioProcessorValueTreeState apvts;

    // Disc state for the editor, written by the audio thread.
    std::atomic<double> uiPhase { 0.0 };
    std::atomic<double> uiPhaseStamp { 0.0 }; // Time::getMillisecondCounterHiRes() when uiPhase was written
    std::atomic<double> uiPlatterSpeed { 1.0 };
    std::atomic<double> uiPosition { 0.0 };
    std::atomic<double> uiRadius { 0.146 };
    std::atomic<double> uiArmTransit { 1.0 };
    std::atomic<double> uiArmHeight { 0.0 };
    std::atomic<bool> uiLifterRaised { false };
    std::atomic<bool> uiMotorRunning { false };
    std::atomic<bool> uiClamped { false };

    // Scratches cut by hand on the editor's disc. Any thread may call these;
    // the audio thread picks up changes at the start of the next block.
    dustytable::ScratchList getScratches() const;
    void setScratches(const dustytable::ScratchList& list);

private:
    float param(const char* id) const { return apvts.getRawParameterValue(id)->load(); }

    dustytable::VinylEngine engine;
    juce::AudioBuffer<float> noise;
    double internalTime = 0.0;
    juce::SmoothedValue<float> noiseGain, mechanicsGain, dryGain;
    std::atomic<bool> armImmediatePending { false };
    std::atomic<double> savedStylus { -1.0 }; // stylus position from a loaded session, -1 = none
    void handleAsyncUpdate() override;

    mutable juce::SpinLock scratchLock;
    dustytable::ScratchList scratchList; // guarded by scratchLock
    int scratchVersion = 0;              // guarded by scratchLock
    int scratchVersionApplied = -1;      // audio thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DustyTableProcessor)
};
