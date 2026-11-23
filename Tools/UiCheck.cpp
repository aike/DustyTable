// Offscreen check of the editor's record tools: creates the processor and the
// editor, sends mouse events straight to the disc view and saves snapshots.
// A helper thread stands in for the audio thread and turns the disc, so the
// tools are checked on a spinning and on a standing disc.
// Usage: DustyTableUiCheck <output directory>

#include "DustyTableEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cstdio>
#include <thread>

struct DiscViewProbe {
    static int eraserTarget(const DiscView& v, juce::Point<float> pos) { return v.eraserTarget(pos, v.layout()); }
};

namespace {

double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

struct Check {
    DustyTableProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    DiscView* disc = nullptr;
    juce::File dir;
    juce::Point<float> downPos;
    juce::Time downTime;

    // Stand-in for the audio thread: reports the disc angle every 512 samples.
    std::atomic<bool> spinning { false }, quit { false };
    std::thread audio;

    explicit Check(const juce::File& d)
        : dir(d)
    {
        editor.reset(processor.createEditor());
        for (auto* c : editor->getChildren())
            if (auto* v = dynamic_cast<DiscView*>(c)) disc = v;
        jassert(disc != nullptr);
        audio = std::thread([this] {
            const double omega = dustytable::twoPi * (100.0 / 3.0) / 60.0;
            double phase = 0.0, last = nowSeconds();
            while (!quit) {
                const double t = nowSeconds();
                if (spinning) phase += omega * (t - last);
                last = t;
                processor.uiPhase.store(phase - dustytable::twoPi * std::floor(phase / dustytable::twoPi));
                processor.uiPlatterSpeed.store(spinning ? 1.0 : 0.0);
                processor.uiPhaseStamp.store(t * 1000.0);
                std::this_thread::sleep_for(std::chrono::microseconds(10667));
            }
        });
    }
    ~Check()
    {
        quit = true;
        audio.join();
        editor.reset();
    }

    juce::MouseEvent event(juce::Point<float> pos, bool buttonDown) const
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto mods = buttonDown ? juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys();
        return juce::MouseEvent(source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, disc, disc, juce::Time::getCurrentTime(),
            downPos, downTime, 1, false);
    }

    void selectTool(int index)
    {
        int i = 0;
        for (auto* c : disc->getChildren())
            if (auto* b = dynamic_cast<ToolButton*>(c))
                if (i++ == index && b->onClick) b->onClick();
    }

    // Runs the display for `seconds` at 60 Hz like the editor's timer.
    void frames(double seconds)
    {
        const double start = nowSeconds();
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            disc->tick();
        } while (nowSeconds() - start < seconds);
    }

    void press(juce::Point<float> pos, double seconds, juce::Point<float> releasePos)
    {
        disc->mouseMove(event(pos, false));
        downPos = pos;
        downTime = juce::Time::getCurrentTime();
        disc->mouseDown(event(pos, true));
        frames(seconds);
        disc->mouseUp(event(releasePos, false));
    }
    void press(juce::Point<float> pos, double seconds) { press(pos, seconds, pos); }

    int scratches() const { return processor.getScratches().count; }

    void report(const char* step, std::initializer_list<juce::Point<float>> marks = {})
    {
        const float dust = processor.apvts.getRawParameterValue("dustDensity")->load();
        std::printf("%-34s dust %5.1f /m   scratches %d\n", step, dust, scratches());
        auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        {
            // Mark where the pointer was (disc view coordinates).
            juce::Graphics g(image);
            g.setColour(juce::Colours::cyan);
            for (auto p : marks) {
                const auto q = p + disc->getPosition().toFloat();
                g.drawEllipse(q.x - 6.0f, q.y - 6.0f, 12.0f, 12.0f, 1.5f);
            }
        }
        juce::FileOutputStream out(dir.getChildFile(juce::String(step) + ".png"));
        out.setPosition(0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream(image, out);
    }

    // Hovers the eraser at pos until a scratch has turned past the pointer and
    // is offered for erasing, then waits like a person would before clicking.
    bool eraseWhenOffered(juce::Point<float> pos, double reaction, double timeout)
    {
        disc->mouseMove(event(pos, false));
        const double start = nowSeconds();
        while (nowSeconds() - start < timeout) {
            frames(0.0);
            if (DiscViewProbe::eraserTarget(*disc, pos) >= 0) {
                std::printf("    offered after %.2f s\n", nowSeconds() - start);
                frames(reaction);
                const int before = scratches();
                press(pos, 0.08);
                return scratches() == before - 1;
            }
        }
        return false;
    }
};

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : ".");
    dir.createDirectory();
    int failures = 0;
    auto expect = [&](bool ok, const char* what) {
        std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    {
        Check c(dir);
        auto* dust = c.processor.apvts.getParameter("dustDensity");
        c.frames(0.1);
        c.report("01_initial");

        // ---- Standing disc ----
        dust->setValueNotifyingHost(0.0f);
        c.selectTool(0);
        c.press({ 150.0f, 420.0f }, 1.0);
        c.press({ 300.0f, 200.0f }, 2.5);
        c.report("02_still_dust_3_5s");
        expect(dust->getValue() > 0.6f && dust->getValue() < 0.8f, "dust grows with pressing time");

        c.selectTool(1);
        const juce::Point<float> a { 400.0f, 300.0f }, b { 200.0f, 430.0f };
        c.press(a, 1.0, { 420.0f, 420.0f });
        c.press(b, 3.0);
        c.report("03_still_nail", { { 420.0f, 420.0f }, b });
        expect(c.scratches() == 2, "two cuts on a standing disc");
        c.press(b, 0.5, { 20.0f, 560.0f });
        expect(c.scratches() == 2, "released off the record: no cut");

        c.selectTool(2);
        c.disc->mouseMove(c.event({ 420.0f, 420.0f }, false));
        c.frames(0.1);
        c.report("04_still_eraser_hover", { { 420.0f, 420.0f } });
        c.press({ 420.0f, 420.0f }, 0.05);
        expect(c.scratches() == 1, "eraser removes the scratch under the pointer (standing)");
        const float before = dust->getValue();
        c.press({ 150.0f, 150.0f }, 0.05);
        expect(dust->getValue() < before - 0.04f, "eraser click on bare surface wipes dust");
        c.press({ 150.0f, 150.0f }, 1.0);
        c.report("05_still_erased");

        // ---- Spinning disc ----
        c.spinning = true;
        c.frames(0.5);
        c.selectTool(0);
        const float dustBefore = dust->getValue();
        c.press({ 300.0f, 200.0f }, 1.0);
        expect(dust->getValue() > dustBefore + 0.15f, "dust tool works on a spinning disc");

        c.selectTool(1);
        const juce::Point<float> p { 120.0f, 230.0f };
        c.press(p, 1.0);
        c.report("06_spin_nail_just_released", { p });
        expect(c.scratches() == 2, "cut on a spinning disc");
        c.frames(0.3);
        c.report("07_spin_nail_turned_away", { p });

        // The new scratch comes round under the same point once per revolution.
        c.selectTool(2);
        expect(c.eraseWhenOffered(p, 0.25, 2.5), "eraser removes a passing scratch, clicked 0.25 s after it passed");
        c.report("08_spin_erased", { p });

        // Hover the remaining (old) scratch's radius while spinning and screenshot the highlight.
        c.selectTool(1);
        c.press({ 360.0f, 420.0f }, 1.5);
        c.selectTool(2);
        c.disc->mouseMove(c.event({ 360.0f, 420.0f }, false));
        for (int k = 0; k < 200 && DiscViewProbe::eraserTarget(*c.disc, { 360.0f, 420.0f }) < 0; ++k) c.frames(0.0);
        c.frames(0.1);
        c.report("09_spin_eraser_highlight", { { 360.0f, 420.0f } });

        // ---- Disc coming to rest ----
        c.spinning = false;
        c.frames(0.5);
        c.selectTool(1);
        c.press({ 250.0f, 120.0f }, 0.8);
        expect(c.scratches() == 3, "stopped again: cut");
        c.selectTool(2);
        c.press({ 250.0f, 120.0f }, 0.05);
        expect(c.scratches() == 2, "stopped again: erase at the same point");
        c.report("10_stopped_again");

        juce::MemoryBlock state;
        c.processor.getStateInformation(state);
        DustyTableProcessor other;
        other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        expect(other.getScratches().count == c.scratches(), "scratches survive a state round trip");
    }

    // A session saved before hand-cut scratches: Count 3, Length 80 %, Layout 5.
    {
        const char* legacy = R"(<?xml version="1.0"?><PhysVinyl><PARAM id="scratchCount" value="3"/>)"
                             R"(<PARAM id="scratchLength" value="80"/><PARAM id="scratchSeed" value="5"/>)"
                             R"(<PARAM id="dustDensity" value="12"/></PhysVinyl>)";
        juce::MemoryBlock legacyState;
        DustyTableProcessor::copyXmlToBinary(*juce::parseXML(legacy), legacyState);
        DustyTableProcessor other;
        other.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
        expect(other.getScratches().count == 3 && !other.apvts.state.getChildWithProperty("id", "scratchCount").isValid(),
            "old session: 3 generated scratches taken over, old parameters dropped");
    }

    // ---- See-through arm at the Drop point ----
    {
        Check c(dir);
        juce::Slider* drop = nullptr;
        for (auto* child : c.editor->getChildren())
            if (auto* sl = dynamic_cast<juce::Slider*>(child); sl != nullptr && sl->onDragStart) drop = sl;
        expect(drop != nullptr, "Drop point knob found");
        if (drop != nullptr) {
            c.frames(0.2);
            expect(c.disc->getDropGhostAlpha() == 0.0f, "no see-through arm at rest");
            drop->onDragStart();
            drop->setValue(70.0, juce::sendNotificationSync);
            c.frames(0.3);
            const float dragging = c.disc->getDropGhostAlpha();
            c.frames(1.2); // still holding the knob without moving it
            const float held = c.disc->getDropGhostAlpha();
            c.report("21_drop_point_ghost");
            drop->onDragEnd();
            c.frames(0.6);
            std::printf("    ghost alpha: dragging %.2f, held 1.5 s %.2f, 0.6 s after release %.2f\n", dragging, held,
                c.disc->getDropGhostAlpha());
            expect(dragging > 0.99f && held > 0.99f, "see-through arm shown while the Drop point knob is dragged");
            expect(c.disc->getDropGhostAlpha() == 0.0f, "see-through arm gone once the knob is released");
            // Set without dragging (automation, mouse wheel): shown, then gone when the value stays put.
            auto* param = c.processor.apvts.getParameter("position");
            param->setValueNotifyingHost(param->convertTo0to1(30.0f));
            c.frames(0.3);
            const float shown = c.disc->getDropGhostAlpha();
            c.frames(1.3);
            expect(shown > 0.99f && c.disc->getDropGhostAlpha() == 0.0f,
                "a Drop point change from automation shows the see-through arm briefly");
        }
    }

    // ---- Stylus switch ----
    {
        Check c(dir);
        StylusSwitch* sw = nullptr;
        for (auto* child : c.editor->getChildren())
            if (auto* s = dynamic_cast<StylusSwitch*>(child)) sw = s;
        expect(sw != nullptr, "stylus switch is on the panel");
        if (sw != nullptr) {
            auto* stylus = c.processor.apvts.getRawParameterValue("stylus");
            auto click = [&](float x) {
                const juce::Point<float> pos { x, sw->getHeight() * 0.5f };
                auto source = juce::Desktop::getInstance().getMainMouseSource();
                sw->mouseDown(juce::MouseEvent(source, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, sw, sw, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(),
                    1, false));
            };
            expect(stylus->load() < 0.5f && sw->getSelectedIndex() == 0, "conical selected by default");
            click(sw->getWidth() * 0.75f);
            expect(stylus->load() > 0.5f && sw->getSelectedIndex() == 1, "clicking the right side selects elliptical");
            c.report("20_stylus_elliptical");
            click(sw->getWidth() * 0.25f);
            expect(stylus->load() < 0.5f && sw->getSelectedIndex() == 0, "clicking the left side selects conical");
            auto* param = c.processor.apvts.getParameter("stylus");
            param->setValueNotifyingHost(1.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            expect(sw->getSelectedIndex() == 1, "switch follows the parameter (automation)");
        }
    }

    // ---- Stylus travel (processor with audio blocks) ----
    auto setParam = [](DustyTableProcessor& p, const char* id, float value) {
        auto* param = p.apvts.getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(value));
    };
    auto run = [](DustyTableProcessor& p, double seconds) {
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < static_cast<int>(seconds * 48000.0 / 512.0); ++i) {
            buffer.clear();
            p.processBlock(buffer, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // async Start/Stop update
    };
    {
        DustyTableProcessor p;
        setParam(p, "side", 1.0f);
        setParam(p, "position", 98.0f);
        p.prepareToPlay(48000.0, 512);
        run(p, 0.6);
        const double early = p.uiPosition.load();
        run(p, 1.0);
        std::printf("    end of side: stylus %.4f after 0.6 s, arm param %.0f after 1.6 s\n", early,
            p.apvts.getRawParameterValue("arm")->load());
        expect(early > 0.98 && early < 0.995, "stylus moves inward from the drop point");
        expect(p.apvts.getRawParameterValue("arm")->load() < 0.5f, "end of side switches Start/Stop to Stop");
        run(p, 8.0); // arm returns, cam parks, platter coasts down (about 7 s in all)
        expect(p.uiArmTransit.load() <= 0.0 && p.uiPlatterSpeed.load() <= 0.0, "arm back on its rest, platter stopped");
    }
    {
        DustyTableProcessor p;
        p.prepareToPlay(48000.0, 512);
        run(p, 3.0);
        juce::MemoryBlock state;
        p.getStateInformation(state);
        DustyTableProcessor q;
        q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        q.prepareToPlay(48000.0, 512);
        run(q, 0.02);
        std::printf("    saved mid-side: stylus %.5f -> %.5f after reload\n", p.uiPosition.load(), q.uiPosition.load());
        expect(std::abs(q.uiPosition.load() - p.uiPosition.load()) < 1e-4 && p.uiPosition.load() > 0.002,
            "a session saved mid-side resumes at the saved stylus position");
    }

    // ---- Cue in place (UP/DOWN) ----
    {
        DustyTableProcessor p;
        setParam(p, "position", 20.0f);
        p.prepareToPlay(48000.0, 512);
        run(p, 0.5);
        const double before = p.uiPosition.load();
        p.requestCueToggle();
        run(p, 3.0);
        std::printf("    cue up: arm height %.2f mm, stylus %.4f\n", p.uiArmHeight.load() * 1e3, p.uiPosition.load());
        expect(p.uiCueUp.load() && p.uiArmHeight.load() > 3e-3, "UP/DOWN raises the arm in place");
        expect(std::abs(p.uiPosition.load() - before) < 0.002, "the arm stays where it was when raised");
        setParam(p, "position", 60.0f);
        run(p, 3.0);
        const double moving = p.uiPosition.load();
        run(p, 12.0);
        std::printf("    drop point 60 %%: stylus %.4f after 3 s, %.4f after 15 s\n", moving, p.uiPosition.load());
        expect(moving > 0.25 && moving < 0.55, "the raised arm moves slowly towards the drop point");
        expect(std::abs(p.uiPosition.load() - 0.6) < 0.003, "the raised arm stops at the drop point");

        juce::MemoryBlock state;
        p.getStateInformation(state);
        DustyTableProcessor q;
        q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        q.prepareToPlay(48000.0, 512);
        run(q, 0.1);
        expect(q.uiCueUp.load() && q.uiArmHeight.load() > 3e-3 && std::abs(q.uiPosition.load() - 0.6) < 0.003,
            "a session saved with the arm cued up reopens with the arm held up at its position");

        p.requestCueToggle();
        run(p, 4.0);
        const double landed = p.uiPosition.load();
        run(p, 1.0);
        std::printf("    cue down: arm height %.3f mm, stylus %.5f -> %.5f\n", p.uiArmHeight.load() * 1e3, landed,
            p.uiPosition.load());
        expect(!p.uiCueUp.load() && p.uiArmHeight.load() < 0.0, "UP/DOWN lowers the arm onto the record");
        expect(p.uiPosition.load() > landed && std::abs(landed - 0.6) < 0.003, "play continues from where it was lowered");

        // Moved while playing: nothing happens until the arm is raised, then it moves.
        const double playing = p.uiPosition.load();
        setParam(p, "position", 40.0f);
        run(p, 1.0);
        // A 20-minute side moves the stylus about 0.0008 per second.
        expect(p.uiPosition.load() > playing && p.uiPosition.load() - playing < 0.0015,
            "moving the drop point while playing does not move the arm");
        const double beforeRaise = p.uiPosition.load();
        p.requestCueToggle();
        run(p, 1.2);
        const double raised = p.uiPosition.load();
        run(p, 14.0);
        std::printf("    raised after moving the drop point to 40 %%: stylus %.4f -> %.4f\n", raised, p.uiPosition.load());
        expect(std::abs(raised - beforeRaise) < 0.001 && std::abs(p.uiPosition.load() - 0.4) < 0.003,
            "after raising, the arm moves to the new drop point");

        // STOP while cued up returns the arm; UP/DOWN does nothing while it rests.
        setParam(p, "arm", 0.0f);
        run(p, 8.0);
        expect(!p.uiCueUp.load() && p.uiArmTransit.load() <= 0.0, "STOP while cued up returns the arm to its rest");
        p.requestCueToggle();
        run(p, 2.0);
        expect(!p.uiCueUp.load() && p.uiArmTransit.load() <= 0.0, "UP/DOWN does nothing while the arm is on its rest");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
