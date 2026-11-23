#pragma once

#include "DustyTableProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>
#include <vector>

class VinylLookAndFeel : public juce::LookAndFeel_V4 {
public:
    VinylLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
        float startAngle, float endAngle, juce::Slider&) override;
};

// Hand tools for working on the record surface.
enum class DiscTool { none, dust, nail, eraser };

class ToolButton : public juce::Button {
public:
    explicit ToolButton(DiscTool t);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    const DiscTool tool;
};

// Spinning record with its dust and scratches, and a tonearm that tracks the
// stylus radius. The dust, nail and eraser tools work directly on the disc.
class DiscView : public juce::Component {
public:
    explicit DiscView(DustyTableProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    // Called by the editor at the display rate.
    void tick();

    static void drawToolIcon(juce::Graphics&, DiscTool, juce::Rectangle<float> area);

private:
    friend struct DiscViewProbe; // Tools/UiCheck.cpp

private:
    struct Layout {
        int discIndex;
        dustytable::DiscGeometry geom;
        double phase, radius;
        juce::Point<float> c, pivot, stylusPt;
        float platterR, scale, discR, armLength, stylusAngle;
    };
    Layout layout() const;
    // Disc angle under a screen angle; the disc turns clockwise.
    static double discAngle(const Layout& l, float screenAngle);
    static float screenAngle(const Layout& l, double theta);
    juce::Path scratchPath(const dustytable::Scratch& s, const Layout& l) const;
    // The scratch the eraser would remove at pos: the one most recently under
    // the pointer within a short look-back (so a turning disc can be hit too).
    int eraserTarget(juce::Point<float> pos, const Layout& l) const;
    dustytable::DrawnScratch nailCut(const Layout& l, juce::Point<float> pos, double heldSeconds) const;
    bool onDisc(juce::Point<float> pos, const Layout& l) const;
    bool onGrooves(juce::Point<float> pos, const Layout& l) const;

    void setTool(DiscTool t);
    void changeDust(double delta);
    void beginDustGesture();
    void endDustGesture();
    void paintDust(juce::Graphics&, const Layout& l);

    DustyTableProcessor& processor;
    std::vector<std::unique_ptr<ToolButton>> toolButtons;
    DiscTool tool = DiscTool::none;
    std::array<juce::MouseCursor, 4> cursors;

    // Disc angle as drawn in the current frame; the tools work on what is on screen.
    double framePhase = 0.0;
    struct PhaseSample {
        double time = -1.0e9, phase = 0.0;
    };
    std::array<PhaseSample, 48> phaseHistory {}; // ring buffer, one sample per frame
    int phaseHistoryNext = 0;

    bool hovering = false;
    juce::Point<float> hoverPos;

    bool pressing = false, dustGesture = false;
    double dustValue = 0.0; // normalised Dust Density, accumulated while a tool is held
    double pressStart = 0.0, lastTick = 0.0;
    juce::Point<float> pressPos;
    uint64_t cutSeed = 1;

    // Mottled dust layer in disc coordinates, re-tinted when the amount changes.
    static constexpr int dustImageSize = 320;
    std::vector<float> mottle;
    juce::Image dustImage;
    float dustImageLevel = -1.0f;
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
    juce::TooltipWindow tooltips { this, 500 };

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Section> sections;

    juce::TextButton rpm33 { "33" }, rpm45 { "45" };
    juce::ComboBox discBox, stylusBox;
    juce::TextButton startButton { "START" }, stopButton { "STOP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> discAttachment, stylusAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DustyTableEditor)
};
