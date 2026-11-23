#pragma once

#include "DustyTableProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

class VinylLookAndFeel : public juce::LookAndFeel_V4 {
public:
    VinylLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
        float startAngle, float endAngle, juce::Slider&) override;
};

// Spinning record with the scratch layout and a tonearm that tracks the
// stylus radius.
class DiscView : public juce::Component {
public:
    explicit DiscView(DustyTableProcessor& p) : processor(p) {}
    void paint(juce::Graphics&) override;

private:
    DustyTableProcessor& processor;
};

class DustyTableEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit DustyTableEditor(DustyTableProcessor&);
    ~DustyTableEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Section {
        juce::String title;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    Knob* addKnob(const juce::String& paramId, const juce::String& text);
    void setRpm(int index);
    void setArm(bool down);

    DustyTableProcessor& processor;
    VinylLookAndFeel lookAndFeel;
    DiscView disc;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Section> sections;

    juce::TextButton rpm33 { "33" }, rpm45 { "45" };
    juce::ComboBox discBox, stylusBox;
    juce::ToggleButton autoButton { "Auto advance" };
    juce::TextButton startButton { "START" }, stopButton { "STOP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> discAttachment, stylusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DustyTableEditor)
};
