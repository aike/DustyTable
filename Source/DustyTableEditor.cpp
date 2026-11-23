#include "DustyTableEditor.h"

#include <algorithm>
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
namespace {

constexpr int toolSize = 38;
constexpr double dustPerSecond = 0.2;    // normalised Dust Density per second of pressing (empty to full in 5 s)
constexpr double eraseClick = 0.05;      // dust wiped by a single eraser click
constexpr double eraseHoldDelay = 0.3;   // the eraser keeps wiping when held longer than this
constexpr double maxCutFraction = 2.0 / 3.0; // longest cut, as a fraction of the recorded band
constexpr double minCutLength = 0.002;   // shortest cut, metres
constexpr double fullCutSeconds = 2.0;   // holding the nail this long gives the longest cut
constexpr float eraserReach = 12.0f;     // pixels
constexpr double eraserLookBack = 0.4;   // s: a scratch that has just turned past the pointer still counts
constexpr double ghostHoldSeconds = 0.8;    // Drop point set by wheel or automation: settled after this long unchanged
constexpr double ghostFadeInSeconds = 0.08;
constexpr double ghostFadeOutSeconds = 0.35;

double wrapPi(double a) { return a - dustytable::twoPi * std::floor((a + dustytable::pi) / dustytable::twoPi); }

double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

juce::String toolTip(DiscTool t)
{
    switch (t) {
    case DiscTool::dust: return "Dust: hold the mouse on the record to add dust";
    case DiscTool::nail: return "Nail: hold and release on the record to cut a scratch (the longer, the longer the cut)";
    case DiscTool::eraser: return "Eraser: click the highlighted scratch to remove it, or bare surface to wipe off dust";
    default: return {};
    }
}

} // namespace

ToolButton::ToolButton(DiscTool t)
    : juce::Button(toolTip(t))
    , tool(t)
{
    setTooltip(toolTip(t));
}

void ToolButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    const bool on = getToggleState();
    g.setColour(on ? colours::accent.withAlpha(0.28f) : colours::panel.withAlpha(highlighted ? 0.95f : 0.8f));
    g.fillRoundedRectangle(b, 6.0f);
    g.setColour(on ? colours::accent : colours::dim.withAlpha(highlighted ? 0.8f : 0.45f));
    g.drawRoundedRectangle(b, 6.0f, on ? 1.6f : 1.0f);
    DiscView::drawToolIcon(g, tool, b.reduced(down ? 5.0f : 4.0f));
}

// Icons are drawn on a 32 x 32 grid; the cursors use them at that size.
void DiscView::drawToolIcon(juce::Graphics& g, DiscTool t, juce::Rectangle<float> area)
{
    const float s = juce::jmin(area.getWidth(), area.getHeight()) / 32.0f;
    const auto o = area.getCentre() - juce::Point<float> { 16.0f, 16.0f } * s;
    auto P = [&](float x, float y) { return o + juce::Point<float> { x, y } * s; };
    const juce::Colour outline = juce::Colours::black.withAlpha(0.8f);

    if (t == DiscTool::dust) {
        struct Speck {
            float x, y, r;
        };
        const Speck specks[] = { { 16, 16, 5.0f }, { 9.5f, 11.5f, 3.0f }, { 21.5f, 9.5f, 3.2f }, { 22, 21, 3.6f },
            { 10.5f, 21.5f, 2.8f }, { 16, 26, 2.0f }, { 6.5f, 16, 1.6f }, { 26, 14.5f, 1.6f }, { 17.5f, 5.5f, 1.5f } };
        for (const auto& k : specks) {
            const auto p = P(k.x, k.y);
            const float r = (k.r + 1.2f) * s;
            g.setColour(outline);
            g.fillEllipse(p.x - r, p.y - r, 2 * r, 2 * r);
        }
        for (const auto& k : specks) {
            const auto p = P(k.x, k.y);
            g.setColour(juce::Colour(0xffb9b2a6).withAlpha(0.45f));
            const float halo = (k.r + 0.6f) * s;
            g.fillEllipse(p.x - halo, p.y - halo, 2 * halo, 2 * halo);
            g.setColour(juce::Colour(0xffd8d1c4));
            const float r = k.r * 0.8f * s;
            g.fillEllipse(p.x - r, p.y - r, 2 * r, 2 * r);
        }
        // A fibre.
        juce::Path fibre;
        fibre.startNewSubPath(P(4, 26));
        fibre.cubicTo(P(8, 22), P(9, 29), P(14, 27));
        g.setColour(outline);
        g.strokePath(fibre, juce::PathStrokeType(2.6f * s));
        g.setColour(juce::Colour(0xffd8d1c4));
        g.strokePath(fibre, juce::PathStrokeType(1.0f * s));
    } else if (t == DiscTool::nail) {
        // Point at the bottom left, head at the top right.
        juce::Path shank;
        shank.startNewSubPath(P(24.5f, 7.5f));
        shank.lineTo(P(6.5f, 25.5f));
        juce::Path tip;
        tip.startNewSubPath(P(8.3f, 26.3f));
        tip.lineTo(P(2.0f, 30.0f));
        tip.lineTo(P(5.7f, 23.7f));
        tip.closeSubPath();
        g.setColour(outline);
        g.strokePath(shank, juce::PathStrokeType(5.4f * s, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        g.strokePath(tip, juce::PathStrokeType(2.2f * s));
        g.drawLine({ P(21.0f, 4.0f), P(28.0f, 11.0f) }, 6.6f * s);
        g.setColour(juce::Colour(0xffb5bac2));
        g.strokePath(shank, juce::PathStrokeType(3.0f * s, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        g.fillPath(tip);
        g.setColour(juce::Colour(0xffe4e7ec));
        g.drawLine({ P(23.0f, 7.5f), P(8.0f, 22.5f) }, 0.9f * s);
        g.setColour(juce::Colour(0xffa2a7b0));
        g.drawLine({ P(21.5f, 4.5f), P(27.5f, 10.5f) }, 4.0f * s);
    } else if (t == DiscTool::eraser) {
        // Rubber at the bottom left, sleeve above it.
        const auto a = P(3.0f, 24.0f), b = P(8.0f, 29.0f);
        const juce::Point<float> u { 15.6f * s, -15.6f * s };
        auto quad = [](juce::Point<float> p0, juce::Point<float> p1, juce::Point<float> p2, juce::Point<float> p3) {
            juce::Path p;
            p.startNewSubPath(p0);
            p.lineTo(p1);
            p.lineTo(p2);
            p.lineTo(p3);
            p.closeSubPath();
            return p;
        };
        const auto body = quad(a, b, b + u, a + u);
        const auto rubber = quad(a, b, b + u * 0.42f, a + u * 0.42f);
        g.setColour(outline);
        g.strokePath(body, juce::PathStrokeType(2.4f * s, juce::PathStrokeType::curved));
        g.setColour(juce::Colour(0xff5b8dd6));
        g.fillPath(body);
        g.setColour(juce::Colour(0xffe9a0ae));
        g.fillPath(rubber);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawLine({ a + u * 0.45f + (b - a) * 0.2f, a + u * 0.95f + (b - a) * 0.2f }, 1.0f * s);
    }
}

DiscView::DiscView(DustyTableProcessor& p)
    : processor(p)
{
    for (auto t : { DiscTool::dust, DiscTool::nail, DiscTool::eraser }) {
        auto b = std::make_unique<ToolButton>(t);
        b->setClickingTogglesState(false);
        b->onClick = [this, t] { setTool(t); };
        addAndMakeVisible(*b);
        toolButtons.push_back(std::move(b));
    }
    struct Hot {
        DiscTool tool;
        int x, y;
    };
    for (const auto& h : { Hot { DiscTool::dust, 16, 16 }, Hot { DiscTool::nail, 2, 30 }, Hot { DiscTool::eraser, 5, 27 } }) {
        juce::Image image(juce::Image::ARGB, 32, 32, true);
        {
            juce::Graphics g(image);
            drawToolIcon(g, h.tool, { 0.0f, 0.0f, 32.0f, 32.0f });
        }
        cursors[static_cast<size_t>(h.tool)] = juce::MouseCursor(image, h.x, h.y);
    }
}

void DiscView::resized()
{
    int y = 12;
    for (auto& b : toolButtons) {
        b->setBounds(12, y, toolSize, toolSize);
        y += toolSize + 6;
    }
}

void DiscView::setTool(DiscTool t)
{
    if (pressing) return;
    tool = tool == t ? DiscTool::none : t;
    for (auto& b : toolButtons) b->setToggleState(b->tool == tool, juce::dontSendNotification);
    setMouseCursor(cursors[static_cast<size_t>(tool)]);
}

DiscView::Layout DiscView::layout() const
{
    auto& ap = processor.apvts;
    Layout l {};
    l.discIndex = static_cast<int>(ap.getRawParameterValue("disc")->load());
    l.geom = dustytable::discGeometry(l.discIndex);
    l.phase = framePhase;
    l.radius = processor.uiRadius.load();

    const auto area = getLocalBounds().toFloat();
    l.c = { area.getCentreX() - 20.0f, area.getCentreY() + 10.0f };
    l.platterR = juce::jmin(area.getWidth(), area.getHeight()) * 0.42f;
    // Smaller discs are drawn to scale on the same 12" platter.
    l.scale = l.platterR / 0.151f;
    l.discR = static_cast<float>(l.geom.discRadius) * l.scale;

    // Tonearm: pivot outside the disc, stylus on the current groove radius.
    l.pivot = { l.c.x + l.platterR * 1.05f, l.c.y - l.platterR * 0.85f };
    l.armLength = l.platterR * 1.25f;
    l.stylusPt = stylusPointAt(l, l.radius);
    l.stylusAngle = std::atan2(l.stylusPt.y - l.c.y, l.stylusPt.x - l.c.x);
    return l;
}

// Where the arm, swung from its pivot, crosses the groove circle of radius r.
juce::Point<float> DiscView::stylusPointAt(const Layout& l, double r)
{
    const float rPx = static_cast<float>(r) * l.scale;
    const auto toPivot = l.pivot - l.c;
    const float d = toPivot.getDistanceFromOrigin();
    const float a = (rPx * rPx - l.armLength * l.armLength + d * d) / (2.0f * d);
    const float h = std::sqrt(juce::jmax(0.0f, rPx * rPx - a * a));
    const auto basePt = l.c + toPivot * (a / d);
    const juce::Point<float> perp { -toPivot.y / d, toPivot.x / d };
    const auto p1 = basePt + perp * h, p2 = basePt - perp * h;
    return p1.y > p2.y ? p1 : p2;
}

void DiscView::setDropPointDragging(bool dragging)
{
    dropDragging = dragging;
    // Letting go of the knob settles the value: start fading at once.
    if (!dragging) lastDropChange = -1.0e9;
}

// A point at disc angle theta is under the stylus when phase == theta; the disc
// turns clockwise.
float DiscView::screenAngle(const Layout& l, double theta) { return l.stylusAngle + static_cast<float>(l.phase - theta); }

double DiscView::discAngle(const Layout& l, float angle)
{
    const double theta = l.stylusAngle + l.phase - angle;
    return theta - dustytable::twoPi * std::floor(theta / dustytable::twoPi);
}

bool DiscView::onDisc(juce::Point<float> pos, const Layout& l) const { return pos.getDistanceFrom(l.c) <= l.discR; }

bool DiscView::onGrooves(juce::Point<float> pos, const Layout& l) const
{
    const float d = pos.getDistanceFrom(l.c);
    return d <= l.discR && d >= static_cast<float>(l.geom.labelRadius) * l.scale;
}

juce::Path DiscView::scratchPath(const dustytable::Scratch& s, const Layout& l) const
{
    juce::Path p;
    const int steps = 24;
    for (int k = 0; k <= steps; ++k) {
        const double r = s.rStart + (s.rEnd - s.rStart) * k / steps;
        const auto pt = polar(l.c, static_cast<float>(r) * l.scale, screenAngle(l, s.angleAt(r)));
        if (k == 0) p.startNewSubPath(pt);
        else p.lineTo(pt);
    }
    return p;
}

int DiscView::eraserTarget(juce::Point<float> pos, const Layout& l) const
{
    const double t = now();
    const double r = pos.getDistanceFrom(l.c) / l.scale;
    const double pointerAngle = std::atan2(pos.y - l.c.y, pos.x - l.c.x);
    const double reach = eraserReach / l.scale; // metres

    // Disc angles under the pointer over the look-back, oldest first.
    std::vector<PhaseSample> samples;
    for (size_t k = 0; k < phaseHistory.size(); ++k) {
        const auto& s = phaseHistory[(static_cast<size_t>(phaseHistoryNext) + k) % phaseHistory.size()];
        if (s.time >= t - eraserLookBack) samples.push_back(s);
    }
    samples.push_back({ t, l.phase });

    const auto list = processor.getScratches();
    int best = -1;
    double bestTime = -1.0e9, bestDistance = 1.0e9;
    for (int i = 0; i < list.count; ++i) {
        const auto s = dustytable::buildScratch(list.items[static_cast<size_t>(i)], l.discIndex, 10.0, 1.0);
        if (r < s.rStart - reach || r > s.rEnd + reach) continue;
        const double rc = juce::jlimit(s.rStart, s.rEnd, r);
        const double radial = std::abs(r - rc);
        const double scratchAngle = s.angleAt(rc);
        auto offset = [&](double phase) { return wrapPi(scratchAngle - (l.stylusAngle + phase - pointerAngle)); };
        // Latest moment the scratch was within reach of the pointer, also
        // counting a pass between two frames.
        double hitTime = -1.0e9, hitDistance = 1.0e9;
        for (size_t k = 0; k < samples.size(); ++k) {
            const double a = offset(samples[k].phase);
            double arc = std::abs(a) * rc;
            if (k > 0) {
                const double b = offset(samples[k - 1].phase);
                if ((a < 0.0) != (b < 0.0) && std::abs(a) + std::abs(b) < dustytable::pi) arc = 0.0;
            }
            const double d = std::sqrt(arc * arc + radial * radial);
            if (d <= reach && samples[k].time >= hitTime) {
                hitTime = samples[k].time;
                hitDistance = d;
            }
        }
        if (hitTime > bestTime + 1e-9 || (std::abs(hitTime - bestTime) <= 1e-9 && hitDistance < bestDistance)) {
            if (hitTime < -1.0e8) continue;
            best = i;
            bestTime = hitTime;
            bestDistance = hitDistance;
        }
    }
    return best;
}

dustytable::DrawnScratch DiscView::nailCut(const Layout& l, juce::Point<float> pos, double heldSeconds) const
{
    const double band = l.geom.outerGroove - l.geom.innerGroove;
    const double shortest = std::min(maxCutFraction, minCutLength / band);
    const double fraction = shortest + (maxCutFraction - shortest) * juce::jlimit(0.0, 1.0, heldSeconds / fullCutSeconds);
    const double r = pos.getDistanceFrom(l.c) / l.scale;
    const double theta = discAngle(l, std::atan2(pos.y - l.c.y, pos.x - l.c.x));
    return dustytable::cutScratch(l.discIndex, r, theta, fraction, cutSeed);
}

void DiscView::beginDustGesture()
{
    if (dustGesture) return;
    if (auto* param = processor.apvts.getParameter("dustDensity")) {
        param->beginChangeGesture();
        dustValue = param->getValue();
        dustGesture = true;
    }
}

void DiscView::endDustGesture()
{
    if (!dustGesture) return;
    if (auto* param = processor.apvts.getParameter("dustDensity")) param->endChangeGesture();
    dustGesture = false;
}

void DiscView::changeDust(double delta)
{
    auto* param = processor.apvts.getParameter("dustDensity");
    if (param == nullptr || !dustGesture) return;
    // Accumulate here: the parameter snaps to its step, which would swallow
    // the small per-frame increments near zero.
    dustValue = juce::jlimit(0.0, 1.0, dustValue + delta);
    if (std::abs(param->getValue() - static_cast<float>(dustValue)) > 1e-6f)
        param->setValueNotifyingHost(static_cast<float>(dustValue));
}

void DiscView::mouseDown(const juce::MouseEvent& e)
{
    if (tool == DiscTool::none) return;
    const auto l = layout();
    pressPos = e.position;
    pressStart = lastTick = now();
    pressing = false;
    switch (tool) {
    case DiscTool::dust:
        if (onDisc(pressPos, l)) {
            pressing = true;
            beginDustGesture();
        }
        break;
    case DiscTool::nail:
        if (onGrooves(pressPos, l)) {
            pressing = true;
            cutSeed = static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64());
        }
        break;
    case DiscTool::eraser:
        if (const int index = eraserTarget(pressPos, l); index >= 0) {
            auto list = processor.getScratches();
            list.remove(index);
            processor.setScratches(list);
        } else if (onDisc(pressPos, l)) {
            pressing = true;
            beginDustGesture();
            changeDust(-eraseClick);
        }
        break;
    default: break;
    }
}

void DiscView::mouseDrag(const juce::MouseEvent& e)
{
    pressPos = hoverPos = e.position;
    hovering = true;
}

void DiscView::mouseMove(const juce::MouseEvent& e)
{
    hoverPos = e.position;
    hovering = true;
}

void DiscView::mouseExit(const juce::MouseEvent&) { hovering = false; }

void DiscView::mouseUp(const juce::MouseEvent& e)
{
    pressPos = e.position;
    if (pressing && tool == DiscTool::nail) {
        const auto l = layout();
        // Released off the record: no cut.
        if (onGrooves(pressPos, l)) {
            auto list = processor.getScratches();
            list.add(nailCut(l, pressPos, now() - pressStart));
            processor.setScratches(list);
        }
    }
    endDustGesture();
    pressing = false;
}

void DiscView::tick()
{
    const double t = now();
    const double dt = juce::jlimit(0.0, 0.1, t - lastTick);
    lastTick = t;

    // The audio thread reports the angle once per block; carry it on to this
    // frame so that the disc turns smoothly and the tools hit what is drawn.
    const int rpmIndex = static_cast<int>(processor.apvts.getRawParameterValue("rpm")->load());
    const double omega = dustytable::twoPi * dustytable::rpmValue(rpmIndex) / 60.0;
    const double age = juce::jlimit(0.0, 0.1, t - processor.uiPhaseStamp.load() * 0.001);
    framePhase = processor.uiPhase.load() + omega * processor.uiPlatterSpeed.load() * age;
    framePhase -= dustytable::twoPi * std::floor(framePhase / dustytable::twoPi);
    phaseHistory[static_cast<size_t>(phaseHistoryNext)] = { t, framePhase };
    phaseHistoryNext = (phaseHistoryNext + 1) % static_cast<int>(phaseHistory.size());

    // See-through arm at the Drop point: shown while the value is being changed.
    const float drop = processor.apvts.getRawParameterValue("position")->load();
    if (lastDropPoint >= 0.0f && drop != lastDropPoint) lastDropChange = t;
    lastDropPoint = drop;
    const bool showGhost = dropDragging || t - lastDropChange < ghostHoldSeconds;
    const float fade = static_cast<float>(dt / (showGhost ? ghostFadeInSeconds : ghostFadeOutSeconds));
    ghostAlpha = juce::jlimit(0.0f, 1.0f, ghostAlpha + (showGhost ? fade : -fade));

    if (!pressing) return;
    const auto l = layout();
    if (!onDisc(pressPos, l)) return;
    if (tool == DiscTool::dust) changeDust(dustPerSecond * dt);
    else if (tool == DiscTool::eraser && t - pressStart > eraseHoldDelay) changeDust(-dustPerSecond * dt);
}

// Dust settles over the whole surface, a little unevenly: a fixed mottled
// pattern that fills in as the amount grows.
void DiscView::paintDust(juce::Graphics& g, const Layout& l)
{
    const float density = processor.apvts.getRawParameterValue("dustDensity")->load();
    const float level = std::pow(juce::jlimit(0.0f, 1.0f, density / 60.0f), 0.8f);
    if (level < 0.002f) return;
    const int n = dustImageSize;

    if (mottle.empty()) {
        // Value noise over a few octaves, normalised to 0..1.
        mottle.assign(static_cast<size_t>(n * n), 0.0f);
        dustytable::Rng rng(20261123);
        const int cellsPerOctave[] = { 4, 9, 19, 40, 85 };
        const float weights[] = { 1.0f, 0.7f, 0.45f, 0.3f, 0.22f };
        for (int o = 0; o < 5; ++o) {
            const int cells = cellsPerOctave[o];
            std::vector<float> grid(static_cast<size_t>((cells + 1) * (cells + 1)));
            for (auto& v : grid) v = static_cast<float>(rng.uniform());
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x) {
                    const float fx = x * static_cast<float>(cells) / n, fy = y * static_cast<float>(cells) / n;
                    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
                    float tx = fx - ix, ty = fy - iy;
                    tx = tx * tx * (3.0f - 2.0f * tx);
                    ty = ty * ty * (3.0f - 2.0f * ty);
                    auto at = [&](int gx, int gy) { return grid[static_cast<size_t>(gy * (cells + 1) + gx)]; };
                    const float v = (at(ix, iy) * (1 - tx) + at(ix + 1, iy) * tx) * (1 - ty)
                                    + (at(ix, iy + 1) * (1 - tx) + at(ix + 1, iy + 1) * tx) * ty;
                    mottle[static_cast<size_t>(y * n + x)] += weights[o] * v;
                }
        }
        const auto [lo, hi] = std::minmax_element(mottle.begin(), mottle.end());
        const float mn = *lo, range = juce::jmax(1e-6f, *hi - *lo);
        for (auto& v : mottle) v = (v - mn) / range;
        dustImage = juce::Image(juce::Image::ARGB, n, n, true);
    }

    if (std::abs(level - dustImageLevel) > 0.004f) {
        dustImageLevel = level;
        juce::Image::BitmapData data(dustImage, juce::Image::BitmapData::writeOnly);
        const juce::Colour grey(0xffaaa59c);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const float dx = (x + 0.5f) / n * 2.0f - 1.0f, dy = (y + 0.5f) / n * 2.0f - 1.0f;
                const float rr = std::sqrt(dx * dx + dy * dy);
                const float m = mottle[static_cast<size_t>(y * n + x)];
                // Never saturates, so the patches stay visible even when the disc is full.
                float a = 0.8f * level * (0.3f + 0.7f * m * m * (3.0f - 2.0f * m));
                a *= juce::jlimit(0.0f, 1.0f, (1.0f - rr) * 60.0f);
                data.setPixelColour(x, y, grey.withAlpha(a));
            }
    }

    juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.drawImageTransformed(dustImage,
        juce::AffineTransform::translation(-0.5f * n, -0.5f * n)
            .scaled(2.0f * l.discR / n)
            .rotated(static_cast<float>(l.phase))
            .translated(l.c));
}

void DiscView::paint(juce::Graphics& g)
{
    using dustytable::twoPi;
    auto& ap = processor.apvts;
    const auto L = layout();
    const int rpmIndex = static_cast<int>(ap.getRawParameterValue("rpm")->load());
    const auto& geom = L.geom;
    const double phase = L.phase;
    const double radius = L.radius;
    const double position = processor.uiPosition.load();
    const auto c = L.c;
    const float platterR = L.platterR, scale = L.scale, discR = L.discR;

    // Platter and its mat.
    g.setColour(juce::Colour(0xff2a272d));
    g.fillEllipse(c.x - platterR - 6, c.y - platterR - 6, (platterR + 6) * 2, (platterR + 6) * 2);
    g.setColour(juce::Colour(0xff1c1a1e));
    g.fillEllipse(c.x - platterR, c.y - platterR, platterR * 2, platterR * 2);

    // Disc and grooves.
    g.setColour(colours::vinyl);
    g.fillEllipse(c.x - discR, c.y - discR, discR * 2, discR * 2);
    paintDust(g, L);
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

    const auto pivot = L.pivot;
    const float armLength = L.armLength;
    const auto stylusPt = L.stylusPt;

    // Scratches.
    const double depth = ap.getRawParameterValue("scratchDepth")->load();
    const double pileUp = ap.getRawParameterValue("scratchPileUp")->load();
    const auto list = processor.getScratches();
    // The scratch a click of the eraser would remove.
    const int target = tool == DiscTool::eraser && hovering && !pressing ? eraserTarget(hoverPos, L) : -1;
    for (int i = 0; i < list.count; ++i) {
        const auto s = dustytable::buildScratch(list.items[static_cast<size_t>(i)], L.discIndex, depth, pileUp);
        const bool active = s.covers(radius);
        const auto path = scratchPath(s, L);
        const float w = static_cast<float>(0.8 + s.width * 1e4);
        // A dark edge keeps the cut visible on a dusty, grey surface.
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.strokePath(path, juce::PathStrokeType(w + 1.6f));
        if (i == target) {
            g.setColour(colours::accent);
            g.strokePath(path, juce::PathStrokeType(w + 1.5f));
        } else {
            g.setColour(juce::Colours::white.withAlpha(active ? 0.6f : 0.32f));
            g.strokePath(path, juce::PathStrokeType(w));
        }
    }
    // The cut the nail would make if released now.
    if (pressing && tool == DiscTool::nail && onGrooves(pressPos, L)) {
        const auto s = dustytable::buildScratch(nailCut(L, pressPos, now() - pressStart), L.discIndex, depth, pileUp);
        g.setColour(colours::accent.withAlpha(0.85f));
        g.strokePath(scratchPath(s, L), juce::PathStrokeType(static_cast<float>(0.8 + s.width * 1e4)));
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

    // See-through arm where the Drop point would set the stylus down.
    if (ghostAlpha > 0.0f) {
        const double dropRadius = dustytable::grooveRadius(geom, lastDropPoint * 0.01);
        const auto ghostStylus = stylusPointAt(L, dropRadius);
        const float ghostAngle = std::atan2(ghostStylus.y - pivot.y, ghostStylus.x - pivot.x);
        const juce::Point<float> gdir { std::cos(ghostAngle), std::sin(ghostAngle) };
        const auto ghostTip = pivot + gdir * armLength;
        const float a = ghostAlpha;
        g.setColour(juce::Colour(0xff4a464e).withAlpha(0.35f * a));
        g.drawLine({ pivot, pivot - gdir * (platterR * 0.22f) }, 12.0f);
        g.setColour(juce::Colour(0xffc9c4bc).withAlpha(0.4f * a));
        g.drawLine({ pivot, ghostTip - gdir * 14.0f }, 4.0f);
        {
            juce::Graphics::ScopedSaveState state(g);
            g.addTransform(juce::AffineTransform::rotation(ghostAngle, ghostTip.x, ghostTip.y));
            const juce::Rectangle<float> shell { ghostTip.x - 22.0f, ghostTip.y - 7.0f, 26.0f, 14.0f };
            g.setColour(juce::Colour(0xff2f2c33).withAlpha(0.45f * a));
            g.fillRoundedRectangle(shell, 3.0f);
            g.setColour(colours::accent.withAlpha(0.55f * a));
            g.drawRoundedRectangle(shell, 3.0f, 1.0f);
        }
        // The groove it would land on.
        const float rPx = static_cast<float>(dropRadius) * scale;
        g.setColour(colours::accent.withAlpha(0.18f * a));
        g.drawEllipse(c.x - rPx, c.y - rPx, rPx * 2.0f, rPx * 2.0f, 1.0f);
        g.setColour(colours::accent.withAlpha(0.8f * a));
        g.fillEllipse(ghostStylus.x - 2.5f, ghostStylus.y - 2.5f, 5.0f, 5.0f);
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
    const double speed = twoPi * rpm / 60.0 * radius * processor.uiPlatterSpeed.load();
    const float side = ap.getRawParameterValue("side")->load();
    const int elapsed = static_cast<int>(position * side * 60.0);
    g.setFont(juce::FontOptions(13.0f));
    auto infoArea = getLocalBounds().removeFromBottom(28).reduced(16, 0);
    // Current stylus position, on the same scale as the Drop point knob.
    {
        const juce::String label = "Stylus ";
        const juce::String value = juce::String(position * 100.0, 2) + " %";
        g.setColour(colours::dim);
        g.drawText(label, infoArea, juce::Justification::centredLeft);
        infoArea.removeFromLeft(juce::roundToInt(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), label)));
        g.setColour(colours::text);
        g.drawText(value, infoArea, juce::Justification::centredLeft);
        infoArea.removeFromLeft(juce::roundToInt(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), "100.00 %")) + 14);
    }
    g.setColour(colours::dim);
    const juce::String info = juce::String(radius * 1000.0, 1) + " mm   "
                              + juce::String(speed * 100.0, 1) + " cm/s   "
                              + juce::String(elapsed / 60) + ":" + juce::String(elapsed % 60).paddedLeft('0', 2)
                              + " / " + juce::String(side, 1) + " min";
    g.drawText(info, infoArea, juce::Justification::centredLeft);
}

//==============================================================================
namespace {
const char* const stylusNames[] = { "Conical", "Elliptical" };
const char* const stylusTips[] = {
    "Conical stylus: round 0.7 mil tip",
    "Elliptical stylus: 0.3 x 0.7 mil tip, narrow along the groove so it follows finer detail",
};
} // namespace

StylusSwitch::StylusSwitch(juce::RangedAudioParameter& param)
    : attachment(param, [this](float value) {
        selected = juce::jlimit(0, 1, juce::roundToInt(value));
        repaint();
    })
{
    setTitle("Stylus");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

int StylusSwitch::segmentAt(juce::Point<float> pos) const
{
    if (!getLocalBounds().toFloat().contains(pos)) return -1;
    return pos.x < getWidth() * 0.5f ? 0 : 1;
}

void StylusSwitch::mouseDown(const juce::MouseEvent& e)
{
    const int segment = segmentAt(e.position);
    if (segment >= 0 && segment != selected) attachment.setValueAsCompleteGesture(static_cast<float>(segment));
}

void StylusSwitch::mouseMove(const juce::MouseEvent& e)
{
    const int segment = segmentAt(e.position);
    if (segment != hovered) {
        hovered = segment;
        setTooltip(segment >= 0 ? stylusTips[segment] : "");
        repaint();
    }
}

void StylusSwitch::mouseExit(const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

// Groove seen from above, running left to right, with the diamond's contact
// footprint resting between the two walls. Drawn as fine line work in muted
// metallic tones: champagne when selected, pewter otherwise.
void StylusSwitch::drawTip(juce::Graphics& g, int stylusIndex, juce::Rectangle<float> area, bool on)
{
    const juce::Colour metal = on ? juce::Colour(0xffd2bd94) : juce::Colour(0xff8c857c);
    const juce::Colour deep = on ? juce::Colour(0xff5e4a2c) : juce::Colour(0xff3a3632);
    const auto c = area.getCentre();
    const float halfWidth = area.getHeight() * 0.28f;
    const float wiggle = area.getHeight() * 0.035f;
    const float k = juce::MathConstants<float>::twoPi / (area.getWidth() * 0.5f);

    auto wall = [&](float offset) {
        juce::Path p;
        for (float x = area.getX(); x <= area.getRight() + 0.01f; x += 0.5f) {
            const juce::Point<float> pt { x, c.y + offset + wiggle * std::sin(k * (x - c.x)) };
            if (x == area.getX()) p.startNewSubPath(pt);
            else p.lineTo(pt);
        }
        return p;
    };
    // Fade the groove out towards both ends.
    juce::ColourGradient fade(metal.withAlpha(0.0f), area.getX(), c.y, metal.withAlpha(0.0f), area.getRight(), c.y,
        false);
    fade.addColour(0.25, metal.withAlpha(0.55f));
    fade.addColour(0.75, metal.withAlpha(0.55f));
    g.setGradientFill(fade);
    g.strokePath(wall(-halfWidth), juce::PathStrokeType(0.9f));
    g.strokePath(wall(halfWidth), juce::PathStrokeType(0.9f));
    // Neighbouring grooves, fainter.
    g.setColour(metal.withAlpha(0.14f));
    g.strokePath(wall(-halfWidth * 2.3f), juce::PathStrokeType(0.6f));
    g.strokePath(wall(halfWidth * 2.3f), juce::PathStrokeType(0.6f));

    // Footprint: a circle for the conical tip, a narrow ellipse for the elliptical one.
    const float ry = halfWidth;
    const float rx = stylusIndex == 1 ? halfWidth * 0.42f : halfWidth;
    const juce::Rectangle<float> tip { c.x - rx, c.y - ry, rx * 2.0f, ry * 2.0f };
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillEllipse(tip.translated(0.0f, 1.0f).expanded(0.6f));
    juce::ColourGradient body(metal.brighter(0.25f), c.x - rx * 0.5f, c.y - ry * 0.7f, deep, c.x + rx * 0.6f,
        c.y + ry, false);
    g.setGradientFill(body);
    g.fillEllipse(tip);
    // Facet lines of the diamond and a small highlight.
    g.setColour(metal.brighter(0.4f).withAlpha(0.35f));
    g.drawLine(c.x - rx * 0.55f, c.y, c.x + rx * 0.55f, c.y, 0.6f);
    g.drawLine(c.x, c.y - ry * 0.6f, c.x, c.y + ry * 0.6f, 0.6f);
    g.setColour(juce::Colours::white.withAlpha(on ? 0.5f : 0.25f));
    g.fillEllipse(c.x - rx * 0.45f, c.y - ry * 0.62f, rx * 0.42f, ry * 0.3f);
    g.setColour(metal.brighter(0.2f));
    g.drawEllipse(tip, 0.8f);
}

void StylusSwitch::paint(juce::Graphics& g)
{
    const juce::Colour gold(0xffd2bd94);
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const float corner = 4.0f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1b191d), 0.0f, bounds.getY(), juce::Colour(0xff141215), 0.0f,
        bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, corner);

    const float segmentWidth = bounds.getWidth() * 0.5f;
    for (int i = 0; i < 2; ++i) {
        const auto seg = bounds.withWidth(segmentWidth).translated(i * segmentWidth, 0.0f);
        const bool on = i == selected;
        const auto inner = seg.reduced(2.5f);
        if (on) {
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2e2a2c), 0.0f, inner.getY(),
                juce::Colour(0xff242123), 0.0f, inner.getBottom(), false));
            g.fillRoundedRectangle(inner, corner - 1.0f);
            g.setColour(gold.withAlpha(0.7f));
            g.drawRoundedRectangle(inner, corner - 1.0f, 0.8f);
        } else if (i == hovered) {
            g.setColour(juce::Colours::white.withAlpha(0.04f));
            g.fillRoundedRectangle(inner, corner - 1.0f);
        }
        auto content = seg.reduced(8.0f, 6.0f);
        drawTip(g, i, content.removeFromLeft(content.getHeight() * 1.5f), on);
        content.removeFromLeft(7.0f);
        g.setColour(on ? gold : juce::Colour(0xff7d766e));
        g.setFont(juce::FontOptions(12.5f).withKerningFactor(0.03f));
        g.drawFittedText(stylusNames[i], content.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
    g.setColour(juce::Colour(0xff4a4448));
    g.drawRoundedRectangle(bounds, corner, 0.8f);
}

//==============================================================================
DustyTableEditor::DustyTableEditor(DustyTableProcessor& p)
    : AudioProcessorEditor(&p)
    , processor(p)
    , disc(p)
    , stylusSwitch(*p.apvts.getParameter("stylus"))
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(disc);

    auto section = [this](const juce::String& title, std::initializer_list<std::pair<const char*, const char*>> items) {
        Section s { title, {}, {} };
        for (const auto& [id, text] : items) s.knobs.push_back(addKnob(id, text));
        sections.push_back(std::move(s));
    };
    section("DECK", { { "vtf", "Tracking" }, { "position", "Drop point" }, { "side", "Side" }, { "cue", "Cue damping" } });
    // Scratches are cut and dust is spread with the tools on the record.
    section("SCRATCH", { { "scratchDepth", "Depth" }, { "scratchPileUp", "Pile-up" } });
    section("DUST", { { "dustSize", "Max size" } });
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
    addAndMakeVisible(discBox);
    addAndMakeVisible(stylusSwitch);
    // Deck buttons: START lowers the arm onto the record, STOP returns it.
    startButton.onClick = [this] { setArm(true); };
    stopButton.onClick = [this] { setArm(false); };
    startButton.setConnectedEdges(juce::Button::ConnectedOnRight);
    stopButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
    addAndMakeVisible(startButton);
    addAndMakeVisible(stopButton);
    // Cue: raises the arm where it is, or lowers it again. While it is up the
    // arm turns slowly to the Drop point.
    cueButton.setClickingTogglesState(false);
    cueButton.setTooltip("Raise or lower the arm where it is. While it is up, the arm moves to the Drop point.");
    cueButton.onClick = [this] { processor.requestCueToggle(); };
    addAndMakeVisible(cueButton);
    discAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, "disc", discBox);

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
    if (paramId == "position") {
        knob->slider.onDragStart = [this] { disc.setDropPointDragging(true); };
        knob->slider.onDragEnd = [this] { disc.setDropPointDragging(false); };
    }
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
    disc.tick();
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
    cueButton.setBounds(discWidth - 150, editorHeight - 82, 134, 30); // above START/STOP, clear of the info line

    auto panel = area.reduced(12, 0);
    panel.removeFromTop(60);
    auto selectors = panel.removeFromTop(40);
    auto small = selectors.withSizeKeepingCentre(selectors.getWidth(), 28);
    rpm33.setBounds(small.removeFromLeft(46));
    rpm45.setBounds(small.removeFromLeft(46));
    small.removeFromLeft(8);
    discBox.setBounds(small.removeFromLeft(64));
    selectors.removeFromLeft(46 + 46 + 8 + 64 + 10);
    stylusSwitch.setBounds(selectors);
    panel.removeFromTop(6);

    // Rows: DECK | SCRATCH + DUST + STATIC ELECTRICITY | OUTPUT. Cells shrink when
    // a row holds more knobs than fit; the last section takes the rest of the row.
    const std::vector<std::vector<size_t>> rows { { 0 }, { 1, 2, 3 }, { 4 } };
    for (const auto& row : rows) {
        auto rowArea = panel.removeFromTop(rowHeight);
        size_t knobsInRow = 0;
        for (size_t index : row) knobsInRow += sections[index].knobs.size();
        int cell = juce::jmin(cellWidth, rowArea.getWidth() / static_cast<int>(knobsInRow));
        // Leave the last section room for its whole title.
        const auto& last = sections[row.back()];
        const int lastKnobs = static_cast<int>(last.knobs.size());
        if (static_cast<int>(knobsInRow) > lastKnobs) {
            const int titleWidth = juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                                       juce::FontOptions(11.0f, juce::Font::bold), last.title))
                                   + 16;
            const int room = (rowArea.getWidth() - juce::jmax(titleWidth, cell * lastKnobs))
                             / (static_cast<int>(knobsInRow) - lastKnobs);
            cell = juce::jmin(cell, room);
        }
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
