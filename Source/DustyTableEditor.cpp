#include "DustyTableEditor.h"

#include <cmath>

namespace {

namespace colours {
const juce::Colour background { 0xff17151a };
const juce::Colour panel { 0xff221f26 };
const juce::Colour accent { 0xffe8a33d };
const juce::Colour text { 0xffddd5c7 };
const juce::Colour dim { 0xff8a8278 };
const juce::Colour vinyl { 0xff0d0d0f };
const juce::Colour label { 0xff8e2c22 };
} // namespace colours

constexpr int discWidth = 540;
constexpr int panelWidth = 440;
constexpr int editorHeight = 580;
constexpr int cellWidth = 104;
constexpr int rowHeight = 117;

juce::Point<float> polar(juce::Point<float> c, float r, float angle)
{
    return { c.x + r * std::cos(angle), c.y + r * std::sin(angle) };
}

} // namespace

//==============================================================================
VinylLookAndFeel::VinylLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, colours::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, colours::dim);
    setColour(juce::ComboBox::backgroundColourId, colours::panel);
    setColour(juce::ComboBox::outlineColourId, colours::dim.withAlpha(0.4f));
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::ComboBox::arrowColourId, colours::accent);
    setColour(juce::PopupMenu::backgroundColourId, colours::panel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha(0.3f));
    setColour(juce::TextButton::buttonColourId, colours::panel);
    setColour(juce::TextButton::buttonOnColourId, colours::accent);
    setColour(juce::TextButton::textColourOffId, colours::text);
    setColour(juce::TextButton::textColourOnId, colours::background);
    setColour(juce::ToggleButton::textColourId, colours::text);
    setColour(juce::ToggleButton::tickColourId, colours::accent);
    setColour(juce::ToggleButton::tickDisabledColourId, colours::dim);
}

void VinylLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
    float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float track = 3.0f;

    juce::Path back;
    back.addCentredArc(centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour(colours::dim.withAlpha(0.3f));
    g.strokePath(back, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
    g.setColour(colours::accent);
    g.strokePath(value, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobRadius = radius - track * 3.0f;
    g.setColour(colours::panel.brighter(0.15f));
    g.fillEllipse(centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);
    // Concentric rings, like a small record.
    g.setColour(colours::background.withAlpha(0.6f));
    for (float rr = knobRadius * 0.45f; rr < knobRadius; rr += 3.0f)
        g.drawEllipse(centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f, 0.6f);

    // Slider angles are measured clockwise from 12 o'clock.
    const auto tip = polar(centre, knobRadius * 0.85f, angle - juce::MathConstants<float>::halfPi);
    const auto base = polar(centre, knobRadius * 0.3f, angle - juce::MathConstants<float>::halfPi);
    g.setColour(colours::text);
    g.drawLine({ base, tip }, 2.0f);
}

//==============================================================================
void DiscView::paint(juce::Graphics& g)
{
    using dustytable::twoPi;
    auto& ap = processor.apvts;
    const int discIndex = static_cast<int>(ap.getRawParameterValue("disc")->load());
    const int rpmIndex = static_cast<int>(ap.getRawParameterValue("rpm")->load());
    const auto geom = dustytable::discGeometry(discIndex);
    const double phase = processor.uiPhase.load();
    const double radius = processor.uiRadius.load();
    const double position = processor.uiPosition.load();

    const auto area = getLocalBounds().toFloat();
    const juce::Point<float> c { area.getCentreX() - 20.0f, area.getCentreY() + 10.0f };
    const float platterR = juce::jmin(area.getWidth(), area.getHeight()) * 0.42f;
    // Smaller discs are drawn to scale on the same 12" platter.
    const float scale = platterR / 0.151f;
    const float discR = static_cast<float>(geom.discRadius) * scale;

    // Platter and its mat.
    g.setColour(juce::Colour(0xff2a272d));
    g.fillEllipse(c.x - platterR - 6, c.y - platterR - 6, (platterR + 6) * 2, (platterR + 6) * 2);
    g.setColour(juce::Colour(0xff1c1a1e));
    g.fillEllipse(c.x - platterR, c.y - platterR, platterR * 2, platterR * 2);

    // Disc and grooves.
    g.setColour(colours::vinyl);
    g.fillEllipse(c.x - discR, c.y - discR, discR * 2, discR * 2);
    const float outer = static_cast<float>(geom.outerGroove) * scale;
    const float inner = static_cast<float>(geom.innerGroove) * scale;
    g.setColour(juce::Colours::white.withAlpha(0.045f));
    for (float rr = inner; rr <= outer; rr += 2.2f)
        g.drawEllipse(c.x - rr, c.y - rr, rr * 2, rr * 2, 0.6f);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawEllipse(c.x - outer, c.y - outer, outer * 2, outer * 2, 1.0f);
    g.drawEllipse(c.x - inner, c.y - inner, inner * 2, inner * 2, 1.0f);

    // Fixed light reflections: they stay put while the disc turns.
    for (float centreAngle : { -2.3f, 0.84f }) {
        juce::Path wedge;
        wedge.addPieSegment(c.x - outer, c.y - outer, outer * 2, outer * 2, centreAngle - 0.18f, centreAngle + 0.18f,
            inner / outer);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.10f), c,
            juce::Colours::white.withAlpha(0.0f), polar(c, outer, centreAngle - juce::MathConstants<float>::halfPi), true));
        g.fillPath(wedge);
    }

    // Tonearm: pivot outside the disc, stylus on the current groove radius.
    const juce::Point<float> pivot { c.x + platterR * 1.05f, c.y - platterR * 0.85f };
    const float armLength = platterR * 1.25f;
    const float rPx = static_cast<float>(radius) * scale;
    const auto toPivot = pivot - c;
    const float d = toPivot.getDistanceFromOrigin();
    const float a = (rPx * rPx - armLength * armLength + d * d) / (2.0f * d);
    const float h = std::sqrt(juce::jmax(0.0f, rPx * rPx - a * a));
    const auto basePt = c + toPivot * (a / d);
    const juce::Point<float> perp { -toPivot.y / d, toPivot.x / d };
    const auto p1 = basePt + perp * h, p2 = basePt - perp * h;
    const auto stylusPt = p1.y > p2.y ? p1 : p2;
    const float stylusAngle = std::atan2(stylusPt.y - c.y, stylusPt.x - c.x);

    // A point at disc angle theta is under the stylus when phase == theta; the
    // disc turns clockwise.
    auto screenAngle = [&](double theta) { return stylusAngle + static_cast<float>(phase - theta); };

    // Scratches.
    const int count = static_cast<int>(ap.getRawParameterValue("scratchCount")->load());
    const auto scratches = dustytable::makeScratches(static_cast<int>(ap.getRawParameterValue("scratchSeed")->load()),
        discIndex, ap.getRawParameterValue("scratchLength")->load() * 0.01,
        ap.getRawParameterValue("scratchDepth")->load(), ap.getRawParameterValue("scratchPileUp")->load());
    for (int i = 0; i < count; ++i) {
        const auto& s = scratches[static_cast<size_t>(i)];
        juce::Path p;
        const int steps = 12;
        for (int k = 0; k <= steps; ++k) {
            const double r = s.rStart + (s.rEnd - s.rStart) * k / steps;
            const auto pt = polar(c, static_cast<float>(r) * scale, screenAngle(s.angleAt(r)));
            if (k == 0) p.startNewSubPath(pt);
            else p.lineTo(pt);
        }
        const bool active = s.covers(radius);
        g.setColour(juce::Colours::white.withAlpha(active ? 0.55f : 0.25f));
        g.strokePath(p, juce::PathStrokeType(static_cast<float>(0.8 + s.width * 1e4)));
    }

    // Label, turning with the disc.
    const float labelR = static_cast<float>(geom.labelRadius) * scale;
    g.setColour(colours::label);
    g.fillEllipse(c.x - labelR, c.y - labelR, labelR * 2, labelR * 2);
    {
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::rotation(static_cast<float>(phase), c.x, c.y));
        g.setColour(colours::text.withAlpha(0.85f));
        g.setFont(juce::FontOptions(labelR * 0.2f, juce::Font::bold));
        g.drawText("DUSTY TABLE", juce::Rectangle<float>(c.x - labelR, c.y - labelR * 0.62f, labelR * 2, labelR * 0.3f),
            juce::Justification::centred);
        g.setFont(juce::FontOptions(labelR * 0.16f));
        g.drawText(rpmIndex == 1 ? "45 RPM" : "33 RPM",
            juce::Rectangle<float>(c.x - labelR, c.y + labelR * 0.32f, labelR * 2, labelR * 0.25f),
            juce::Justification::centred);
        g.setColour(colours::text.withAlpha(0.4f));
        g.drawLine(c.x + labelR * 0.25f, c.y, c.x + labelR * 0.85f, c.y, 1.0f);
    }
    g.setColour(juce::Colour(0xffb8b4ae));
    g.fillEllipse(c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);

    // Arm swings between the rest (pointing down, clear of the platter) and the groove.
    const float restAngle = juce::MathConstants<float>::halfPi - 0.12f;
    const float grooveAngle = std::atan2(stylusPt.y - pivot.y, stylusPt.x - pivot.x);
    const float transit = static_cast<float>(processor.uiArmTransit.load());
    const float armAngle = restAngle + (grooveAngle - restAngle) * transit;
    const juce::Point<float> dir { std::cos(armAngle), std::sin(armAngle) };
    const auto tipPt = pivot + dir * armLength;
    // Height above the record: on the groove ~0, on the rest 1.5 mm, lifted 5 mm.
    const float lift = juce::jlimit(0.0f, 1.0f, static_cast<float>((processor.uiArmHeight.load() + 0.5e-3) / 5.5e-3));
    const bool lifterRaised = processor.uiLifterRaised.load();

    // Cue lifter bar under the arm's path over the record.
    {
        juce::Path bar;
        const float barR = armLength * 0.35f;
        // Arc angles run clockwise from 12 o'clock; the arm lies at 1.6-2.3 rad (screen maths) over the record.
        bar.addCentredArc(pivot.x, pivot.y, barR, barR, 0.0f, juce::MathConstants<float>::halfPi + 1.6f,
            juce::MathConstants<float>::halfPi + 2.3f, true);
        g.setColour(lifterRaised ? colours::accent.withAlpha(0.8f) : colours::dim.withAlpha(0.5f));
        g.strokePath(bar, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // Arm rest with its clamp.
    {
        const auto restPt = pivot + juce::Point<float> { std::cos(restAngle), std::sin(restAngle) } * (armLength * 0.62f);
        g.setColour(juce::Colour(0xff35323a));
        g.fillEllipse(restPt.x - 9, restPt.y - 9, 18, 18);
        g.setColour(colours::dim);
        g.drawEllipse(restPt.x - 9, restPt.y - 9, 18, 18, 1.2f);
        const float clampAngle = processor.uiClamped.load() ? 0.0f : -1.1f;
        const auto hinge = restPt + juce::Point<float> { -10.0f, 0.0f };
        g.setColour(juce::Colour(0xffb8b4ae));
        g.drawLine({ hinge, hinge + juce::Point<float> { std::cos(clampAngle), std::sin(clampAngle) } * 20.0f }, 2.0f);
    }
    // Cam motor housing beside the pivot, lit while the motor runs.
    {
        const auto base = pivot + juce::Point<float> { -40.0f, 24.0f };
        g.setColour(juce::Colour(0xff35323a));
        g.fillRoundedRectangle(base.x - 10, base.y - 7, 20, 14, 3.0f);
        g.setColour(processor.uiMotorRunning.load() ? colours::accent : colours::dim.withAlpha(0.6f));
        g.fillEllipse(base.x - 3.0f, base.y - 3.0f, 6.0f, 6.0f);
    }

    // Arm shadow: the higher the arm, the further the shadow falls.
    const auto shadowOffset = juce::Point<float> { 3.0f, 4.0f } * (0.6f + 3.0f * lift);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawLine({ pivot + shadowOffset, tipPt - dir * 14.0f + shadowOffset }, 4.0f);

    // Arm tube, headshell, pivot and counterweight.
    const auto weight = pivot - dir * (platterR * 0.22f);
    g.setColour(juce::Colour(0xff4a464e));
    g.drawLine({ pivot, weight }, 12.0f);
    g.setColour(juce::Colour(0xffc9c4bc));
    g.drawLine({ pivot, tipPt - dir * 14.0f }, 4.0f);
    {
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::rotation(armAngle, tipPt.x, tipPt.y));
        g.setColour(juce::Colour(0xff2f2c33));
        g.fillRoundedRectangle(tipPt.x - 22.0f, tipPt.y - 7.0f, 26.0f, 14.0f, 3.0f);
        g.setColour(lift < 0.1f ? colours::accent : colours::dim);
        g.fillEllipse(tipPt.x - 2.5f, tipPt.y - 2.5f, 5.0f, 5.0f);
    }
    g.setColour(juce::Colour(0xff35323a));
    g.fillEllipse(pivot.x - 16, pivot.y - 16, 32, 32);
    g.setColour(colours::dim);
    g.drawEllipse(pivot.x - 16, pivot.y - 16, 32, 32, 1.5f);

    // Readout of the physical state.
    const double rpm = dustytable::rpmValue(rpmIndex);
    const double speed = twoPi * rpm / 60.0 * radius;
    const float side = ap.getRawParameterValue("side")->load();
    const int elapsed = static_cast<int>(position * side * 60.0);
    g.setColour(colours::dim);
    g.setFont(juce::FontOptions(13.0f));
    const juce::String info = juce::String(radius * 1000.0, 1) + " mm   "
                              + juce::String(speed * 100.0, 1) + " cm/s   "
                              + juce::String(elapsed / 60) + ":" + juce::String(elapsed % 60).paddedLeft('0', 2)
                              + " / " + juce::String(side, 1) + " min";
    g.drawText(info, getLocalBounds().removeFromBottom(28).reduced(16, 0), juce::Justification::centredLeft);
}

//==============================================================================
DustyTableEditor::DustyTableEditor(DustyTableProcessor& p)
    : AudioProcessorEditor(&p)
    , processor(p)
    , disc(p)
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(disc);

    auto section = [this](const juce::String& title, std::initializer_list<std::pair<const char*, const char*>> items) {
        Section s { title, {}, {} };
        for (const auto& [id, text] : items) s.knobs.push_back(addKnob(id, text));
        sections.push_back(std::move(s));
    };
    section("DECK", { { "vtf", "Tracking" }, { "position", "Position" }, { "side", "Side" }, { "cue", "Cue damping" } });
    section("SCRATCH", { { "scratchCount", "Count" }, { "scratchDepth", "Depth" }, { "scratchPileUp", "Pile-up" },
                           { "scratchLength", "Length" }, { "scratchSeed", "Layout" } });
    section("DUST", { { "dustDensity", "Density" }, { "dustSize", "Max size" } });
    section("STATIC ELECTRICITY", { { "static", "Charge" } });
    section("OUTPUT", { { "noise", "Noise" }, { "mech", "Mechanics" }, { "dry", "Dry" } });

    for (auto* b : { &rpm33, &rpm45 }) {
        b->setClickingTogglesState(false);
        addAndMakeVisible(*b);
    }
    rpm33.onClick = [this] { setRpm(0); };
    rpm45.onClick = [this] { setRpm(1); };
    rpm33.setConnectedEdges(juce::Button::ConnectedOnRight);
    rpm45.setConnectedEdges(juce::Button::ConnectedOnLeft);

    discBox.addItemList({ "12\"", "10\"", "7\"" }, 1);
    stylusBox.addItemList({ "Conical", "Elliptical" }, 1);
    addAndMakeVisible(discBox);
    addAndMakeVisible(stylusBox);
    addAndMakeVisible(autoButton);
    // Deck buttons: START lowers the arm onto the record, STOP returns it.
    startButton.onClick = [this] { setArm(true); };
    stopButton.onClick = [this] { setArm(false); };
    startButton.setConnectedEdges(juce::Button::ConnectedOnRight);
    stopButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
    addAndMakeVisible(startButton);
    addAndMakeVisible(stopButton);
    discAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, "disc", discBox);
    stylusAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, "stylus", stylusBox);
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "auto", autoButton);

    setSize(discWidth + panelWidth, editorHeight);
    timerCallback();
    startTimerHz(60);
}

DustyTableEditor::~DustyTableEditor() { setLookAndFeel(nullptr); }

DustyTableEditor::Knob* DustyTableEditor::addKnob(const juce::String& paramId, const juce::String& text)
{
    auto knob = std::make_unique<Knob>();
    knob->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, cellWidth - 8, 18);
    knob->slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob->slider.setColour(juce::Slider::textBoxTextColourId, colours::text);
    knob->label.setText(text, juce::dontSendNotification);
    knob->label.setJustificationType(juce::Justification::centred);
    knob->label.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(knob->slider);
    addAndMakeVisible(knob->label);
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, paramId,
        knob->slider);
    knobs.push_back(std::move(knob));
    return knobs.back().get();
}

void DustyTableEditor::setRpm(int index)
{
    if (auto* param = processor.apvts.getParameter("rpm")) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(index)));
        param->endChangeGesture();
    }
}

void DustyTableEditor::setArm(bool down)
{
    if (auto* param = processor.apvts.getParameter("arm")) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(down ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

void DustyTableEditor::timerCallback()
{
    const bool is45 = processor.apvts.getRawParameterValue("rpm")->load() > 0.5f;
    rpm33.setToggleState(!is45, juce::dontSendNotification);
    rpm45.setToggleState(is45, juce::dontSendNotification);
    const bool armDown = processor.apvts.getRawParameterValue("arm")->load() > 0.5f;
    startButton.setToggleState(armDown, juce::dontSendNotification);
    stopButton.setToggleState(!armDown, juce::dontSendNotification);
    disc.repaint();
}

void DustyTableEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    auto panelArea = getLocalBounds().removeFromRight(panelWidth);
    g.setColour(colours::panel);
    g.fillRect(panelArea);

    g.setColour(colours::text);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("DustyTable", panelArea.getX() + 14, 12, 200, 28, juce::Justification::centredLeft);
    g.setColour(colours::dim);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("physically modelled groove noise", panelArea.getX() + 14, 36, 300, 16,
        juce::Justification::centredLeft);

    for (const auto& s : sections) {
        g.setColour(colours::accent);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(s.title, s.bounds.getX() + 6, s.bounds.getY(), s.bounds.getWidth() - 12, 14,
            juce::Justification::centredLeft);
        g.setColour(colours::dim.withAlpha(0.3f));
        g.drawHorizontalLine(s.bounds.getY() + 15, static_cast<float>(s.bounds.getX() + 6),
            static_cast<float>(s.bounds.getRight() - 6));
    }
}

void DustyTableEditor::resized()
{
    auto area = getLocalBounds();
    disc.setBounds(area.removeFromLeft(discWidth));
    startButton.setBounds(discWidth - 150, editorHeight - 46, 67, 30);
    stopButton.setBounds(discWidth - 83, editorHeight - 46, 67, 30);

    auto panel = area.reduced(12, 0);
    panel.removeFromTop(60);
    auto selectors = panel.removeFromTop(28);
    rpm33.setBounds(selectors.removeFromLeft(50));
    rpm45.setBounds(selectors.removeFromLeft(50));
    selectors.removeFromLeft(8);
    discBox.setBounds(selectors.removeFromLeft(70));
    selectors.removeFromLeft(8);
    stylusBox.setBounds(selectors.removeFromLeft(104));
    selectors.removeFromLeft(8);
    autoButton.setBounds(selectors);
    panel.removeFromTop(12);

    // Rows: DECK | SCRATCH | DUST + STATIC ELECTRICITY | OUTPUT. Cells shrink when
    // a row holds more knobs than fit; the last section takes the rest of the row.
    const std::vector<std::vector<size_t>> rows { { 0 }, { 1 }, { 2, 3 }, { 4 } };
    for (const auto& row : rows) {
        auto rowArea = panel.removeFromTop(rowHeight);
        size_t knobsInRow = 0;
        for (size_t index : row) knobsInRow += sections[index].knobs.size();
        const int cell = juce::jmin(cellWidth, rowArea.getWidth() / static_cast<int>(knobsInRow));
        for (size_t index : row) {
            auto& s = sections[index];
            auto bounds = index == row.back() ? rowArea
                                               : rowArea.removeFromLeft(cell * static_cast<int>(s.knobs.size()));
            s.bounds = bounds;
            auto cells = bounds.withTrimmedTop(18);
            for (auto* k : s.knobs) {
                auto cellArea = cells.removeFromLeft(cell);
                k->label.setBounds(cellArea.removeFromTop(16));
                k->slider.setBounds(cellArea.reduced(4, 0).withTrimmedBottom(4));
            }
        }
    }
}
