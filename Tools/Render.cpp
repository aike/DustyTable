// Offline renderer: writes each noise component to a WAV file and prints its
// level, so the physical model can be checked and calibrated without a host.
//   DustyTableRender [outputDir] [seconds]

#include "dsp/Biquad.h"
#include "dsp/VinylEngine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using dustytable::EngineParams;
using dustytable::VinylEngine;

namespace {

void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int rate)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t frames = static_cast<uint32_t>(l.size());
    const uint32_t dataBytes = frames * 2 * 4;
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(3); u16(2); u32(rate); u32(rate * 8); u16(8); u16(32);
    f.write("data", 4); u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        f.write(reinterpret_cast<const char*>(&l[i]), 4);
        f.write(reinterpret_cast<const char*>(&r[i]), 4);
    }
}

double toDb(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

using Automation = std::function<void(EngineParams&, double)>;

// Lists the peaks above a threshold (one per 50 ms) to show a sequence of events.
void printTimeline(const char* label, const std::vector<float>& l, const std::vector<float>& r, int rate,
    double thresholdDb)
{
    const double threshold = std::pow(10.0, thresholdDb / 20.0);
    std::printf("    %-10s", label);
    const int window = rate / 20;
    for (size_t start = 0; start < l.size(); start += window) {
        double peak = 0.0;
        for (size_t i = start; i < std::min(l.size(), start + window); ++i)
            peak = std::max(peak, static_cast<double>(std::max(std::abs(l[i]), std::abs(r[i]))));
        if (peak > threshold) std::printf(" %.2fs:%.0f", static_cast<double>(start) / rate, toDb(peak));
    }
    std::printf("\n");
}

// Octave-band levels (63 Hz - 8 kHz) of a stretch of the left channel, to see where a sound sits.
void printBands(const char* label, const std::vector<float>& x, int rate, double from, double to)
{
    std::printf("    %-10s %.2f-%.2fs", label, from, to);
    const size_t a = static_cast<size_t>(from * rate), b = std::min(x.size(), static_cast<size_t>(to * rate));
    for (double f = 63.0; f < 9000.0; f *= 2.0) {
        auto bp = dustytable::Biquad::bandpass(rate, f, 1.41);
        double e = 0.0;
        for (size_t i = 0; i < b; ++i) {
            const double y = bp.process(x[i]);
            if (i >= a) e += y * y;
        }
        std::printf(" %.0f:%.0f", f, toDb(std::sqrt(e / std::max<size_t>(1, b - a))));
    }
    std::printf("\n");
}

void render(const std::string& dir, const char* name, EngineParams p, const dustytable::ScratchList& scratches, double seconds, int rate = 48000,
    const Automation& automate = {})
{
    VinylEngine engine;
    engine.prepare(rate);
    engine.setParams(p);
    engine.setScratches(scratches);
    engine.setArmImmediate(p.armDown);
    const int n = static_cast<int>(seconds * rate);
    std::vector<float> l(n), r(n), ml(n), mr(n), contact(n);
    const int block = 512;
    std::string platterLog;
    double platterBefore = engine.getPlatterSpeed();
    for (int i = 0; i < n; i += block) {
        const int m = std::min(block, n - i);
        const double t = static_cast<double>(i) / rate;
        if (automate) {
            automate(p, t);
            engine.setParams(p);
        }
        engine.process(l.data() + i, r.data() + i, ml.data() + i, mr.data() + i, contact.data() + i, m, t);
        const double platter = engine.getPlatterSpeed();
        if ((platter >= 1.0) != (platterBefore >= 1.0) || (platter <= 0.0) != (platterBefore <= 0.0)) {
            char text[48];
            std::snprintf(text, sizeof text, " %.2fs:%s", t, platter >= 1.0 ? "full" : platter <= 0.0 ? "stopped" : (platter > platterBefore ? "starting" : "stopping"));
            platterLog += text;
        }
        platterBefore = platter;
    }
    if (automate) {
        printTimeline("cartridge", l, r, rate, -50.0);
        printTimeline("mechanics", ml, mr, rate, -50.0);
        printTimeline("mech -70", ml, mr, rate, -70.0);
        std::printf("    contact   ");
        for (int i = 1; i < n; ++i)
            if ((contact[i] > 0.5f) != (contact[i - 1] > 0.5f))
                std::printf(" %.2fs:%s", static_cast<double>(i) / rate, contact[i] > 0.5f ? "on" : "off");
        std::printf("\n");
        std::printf("    platter   %s   (stylus at the end: %.4f)\n", platterLog.c_str(), engine.getPosition());
        writeWav(dir + "/" + name + "_mechanics.wav", ml, mr, rate);
        if (std::string(name) == "arm_down") {
            printBands("clamp", ml, rate, 0.5, 0.7);
            printBands("motor", ml, rate, 2.5, 3.2);
        } else if (std::string(name) == "arm_up") {
            printBands("lift-off", l, rate, 0.9, 1.3);
            printBands("close", ml, rate, 4.0, 7.0);
        }
    }
    // Skip the first half second while the arm settles. Low band < 150 Hz and
    // high band > 3 kHz (one-pole splits) show how dark or bright a sound is;
    // events counts peaks above -40 dBFS with a 10 ms hold; fine counts ticks
    // above -55 dBFS with a 1 ms hold, busy is the share of 20 ms windows with a tick.
    double sum = 0.0, peak = 0.0, low = 0.0, high = 0.0, lp1 = 0.0, lp2 = 0.0, hpState = 0.0;
    const double aLow = 1.0 - std::exp(-2.0 * 3.141592653589793 * 150.0 / rate);
    const double aHigh = 1.0 - std::exp(-2.0 * 3.141592653589793 * 3000.0 / rate);
    int events = 0, hold = 0, fine = 0, fineHold = 0, windows = 0, busyWindows = 0;
    bool windowBusy = false;
    const int skip = rate / 2;
    for (int i = 0; i < n; ++i) {
        const double x = l[i]; // one channel: vertical (L-R) motion would cancel in a mono sum
        lp1 += aLow * (x - lp1);
        lp2 += aLow * (lp1 - lp2);
        hpState += aHigh * (x - hpState);
        const double hp = x - hpState;
        if (i < skip) continue;
        sum += 0.5 * (l[i] * l[i] + r[i] * r[i]);
        low += lp2 * lp2;
        high += hp * hp;
        const double a = std::max(std::abs(l[i]), std::abs(r[i]));
        peak = std::max(peak, a);
        if (hold > 0) --hold;
        else if (a > 0.01) { ++events; hold = rate / 100; }
        if (fineHold > 0) --fineHold;
        else if (a > 0.00178) { ++fine; fineHold = rate / 1000; windowBusy = true; }
        if ((i - skip) % (rate / 50) == rate / 50 - 1) {
            ++windows;
            busyWindows += windowBusy ? 1 : 0;
            windowBusy = false;
        }
    }
    const double frames = n - skip;
    std::printf("%-20s rms %6.1f  peak %6.1f  low %6.1f  high %6.1f dBFS  events/s %5.1f  fine/s %6.1f  busy %3.0f%%\n",
        name, toDb(std::sqrt(sum / frames)), toDb(peak), toDb(std::sqrt(low / frames)),
        toDb(std::sqrt(high / frames)), events / (frames / rate), fine / (frames / rate),
        100.0 * busyWindows / std::max(1, windows));
    writeWav(dir + "/" + name + ".wav", l, r, rate);
}

EngineParams silent()
{
    EngineParams p;
    p.dustPerMetre = 0.0;
    p.staticAmount = 0.0;
    return p;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const double seconds = argc > 2 ? std::atof(argv[2]) : 8.0;

    using dustytable::legacyScratches;
    dustytable::ScratchList sc;
    EngineParams p = silent();
    render(dir, "silence", p, sc, 2.0);

    p = silent(); p.dustPerMetre = 3.0;
    render(dir, "dust_33", p, sc, seconds);
    p.rpmIndex = 1;
    render(dir, "dust_45", p, sc, seconds);

    p = silent(); p.dropPosition = 0.5; sc = legacyScratches(1, 0, 1.0, 2);
    render(dir, "scratch_33", p, sc, seconds);
    p.rpmIndex = 1;
    render(dir, "scratch_45", p, sc, seconds);
    p.rpmIndex = 0;
    for (double pileUp : { 0.3, 0.6, 1.5, 3.0 }) {
        p.scratchPileUpMillimetres = pileUp;
        render(dir, ("scratch_33_pileup_" + std::to_string(static_cast<int>(pileUp * 10))).c_str(), p, sc, seconds);
    }

    // Hand-cut scratches: a short, a medium and a maximum-length cut around the stylus radius.
    p = silent(); p.dropPosition = 0.5; sc = {};
    {
        const double r = dustytable::grooveRadius(dustytable::discGeometry(0), 0.5);
        sc.add(dustytable::cutScratch(0, r, 0.5, 0.02, 11));
        sc.add(dustytable::cutScratch(0, r, 2.5, 0.3, 12));
        sc.add(dustytable::cutScratch(0, r, 4.5, 2.0 / 3.0, 13));
    }
    render(dir, "scratch_cut", p, sc, seconds);
    sc = {};

    p = silent(); p.staticAmount = 0.3;
    render(dir, "static_30", p, sc, seconds);
    p.staticAmount = 0.6;
    render(dir, "static", p, sc, seconds);
    p.staticAmount = 1.0;
    render(dir, "static_100", p, sc, seconds);

    // Arm down from the rest at 0.5 s, and up from the disc at 0.5 s.
    p = silent(); p.armDown = false;
    render(dir, "arm_down", p, sc, 8.0, 48000, [](EngineParams& q, double t) { q.armDown = t >= 0.5; });
    p = silent(); p.armDown = true;
    render(dir, "arm_up", p, sc, 7.0, 48000, [](EngineParams& q, double t) { q.armDown = t < 0.5; });
    // Changing its mind halfway through the arm-down sequence.
    p = silent(); p.armDown = false;
    render(dir, "arm_reverse", p, sc, 8.0, 48000, [](EngineParams& q, double t) { q.armDown = t >= 0.5 && t < 2.0; });
    // Several down/up cycles in a row: no two should sound exactly alike.
    p = silent(); p.armDown = false;
    render(dir, "arm_cycles", p, sc, 40.0, 48000, [](EngineParams& q, double t) { q.armDown = std::fmod(t, 10.0) >= 0.5 && std::fmod(t, 10.0) < 7.0; });
    // End of the side: dropped near the end of a 1-minute side, the deck stops by itself
    // (and stays stopped although Start is still set).
    p = silent(); p.sideMinutes = 1.0; p.dropPosition = 0.98;
    render(dir, "end_of_side", p, sc, 8.0, 48000, [](EngineParams& q, double) { q.armDown = true; });

    // Cue in place: START, UP at 9 s, the drop point moved from 20 % to 60 % at 11 s
    // (the arm follows), DOWN at 20 s; then the drop point moved back to 30 % while
    // playing, UP at 26 s (the arm moves once it is up), STOP at 40 s while still cued up.
    {
        VinylEngine engine;
        EngineParams q = silent();
        q.armDown = false;
        q.dropPosition = 0.2;
        q.sideMinutes = 40.0;
        const int rate = 48000, block = 512;
        engine.prepare(rate);
        engine.setParams(q);
        engine.setArmImmediate(false);
        const double seconds = 48.0;
        const int n = static_cast<int>(seconds * rate);
        std::vector<float> l(n), r(n), ml(n), mr(n), contact(n);
        const double band = dustytable::discGeometry(0).outerGroove - dustytable::discGeometry(0).innerGroove;
        double lastPos = engine.getPosition(), lastLogged = -1.0;
        bool wasCue = false;
        for (int i = 0; i < n; i += block) {
            const double t = static_cast<double>(i) / rate;
            q.armDown = t >= 0.5 && t < 40.0;
            q.dropPosition = t < 11.0 ? 0.2 : (t < 22.0 ? 0.6 : 0.3);
            engine.setParams(q);
            for (double at : { 9.0, 20.0, 26.0 })
                if (t <= at && at < t + static_cast<double>(block) / rate) engine.toggleCue();
            const int m = std::min(block, n - i);
            engine.process(l.data() + i, r.data() + i, ml.data() + i, mr.data() + i, contact.data() + i, m, t);
            const double pos = engine.getPosition();
            const double speed = (pos - lastPos) * band / (static_cast<double>(m) / rate);
            if (engine.isCueUp() != wasCue || (std::floor(t) != std::floor(lastLogged) && std::fmod(std::floor(t), 2.0) == 0.0)) {
                std::printf("    cue t=%5.2f  cue %s  stylus %.4f  speed %5.2f mm/s  contact %.2f\n", t,
                    engine.isCueUp() ? "up  " : "down", pos, speed * 1000.0, contact[i]);
                lastLogged = t;
                wasCue = engine.isCueUp();
            }
            lastPos = pos;
        }
        printTimeline("cartridge", l, r, rate, -50.0);
        printTimeline("mechanics", ml, mr, rate, -50.0);
        printTimeline("mech -70", ml, mr, rate, -70.0);
        std::printf("    contact   ");
        for (int i = 1; i < n; ++i)
            if ((contact[i] > 0.5f) != (contact[i - 1] > 0.5f))
                std::printf(" %.2fs:%s", static_cast<double>(i) / rate, contact[i] > 0.5f ? "on" : "off");
        std::printf("\n");
        printBands("cue up", ml, rate, 9.0, 10.5);
        printBands("traverse", ml, rate, 13.0, 16.0);
        writeWav(dir + "/cue_mechanics.wav", ml, mr, rate);
        writeWav(dir + "/cue.wav", l, r, rate);
    }

    p = EngineParams {}; sc = legacyScratches(1, 0, 0.6, 2);
    render(dir, "all_default", p, sc, seconds);
    render(dir, "all_default_44k", p, sc, seconds, 44100);
    render(dir, "all_default_96k", p, sc, seconds, 96000);

    // Stress: heavy dust, deep scratches, light tracking force.
    p.dustPerMetre = 40.0; p.dustMaxMicrons = 40.0; p.scratchDepthMicrons = 60.0;
    p.trackingForceGrams = 1.0; p.staticAmount = 1.0; sc = legacyScratches(1, 0, 1.0, 8);
    render(dir, "stress", p, sc, seconds);
    p.stylusIndex = 1; p.rpmIndex = 1;
    render(dir, "stress_elliptic_45", p, sc, seconds);
    return 0;
}
