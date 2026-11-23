#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// Disc geometry, scratch layout and RNG shared by the DSP engine and the editor.
// All lengths are in metres, angles in radians.
namespace dustytable {

constexpr double pi = 3.14159265358979323846;
constexpr double twoPi = 2.0 * pi;

inline double rpmValue(int rpmIndex) { return rpmIndex == 1 ? 45.0 : 100.0 / 3.0; }

struct DiscGeometry {
    double discRadius;
    double outerGroove; // lead-in, start of the recorded band
    double innerGroove; // end of the recorded band
    double labelRadius;
};

inline DiscGeometry discGeometry(int discIndex)
{
    switch (discIndex) {
    case 1: return { 0.1270, 0.1210, 0.0603, 0.0500 }; // 10"
    case 2: return { 0.0875, 0.0835, 0.0533, 0.0445 }; // 7"
    default: return { 0.1510, 0.1460, 0.0603, 0.0500 }; // 12"
    }
}

// Groove radius for a normalised position (0 = lead-in, 1 = run-out).
inline double grooveRadius(const DiscGeometry& g, double position)
{
    return g.outerGroove - position * (g.outerGroove - g.innerGroove);
}

class Rng {
public:
    Rng() { setSeed(0); }
    explicit Rng(uint64_t seed) { setSeed(seed); }
    void setSeed(uint64_t seed)
    {
        // splitmix64 so that small consecutive seeds give unrelated streams
        uint64_t z = seed + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        state = (z ^ (z >> 31)) | 1ull;
    }
    uint64_t next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
    double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    double uniformOpen() { return (static_cast<double>(next() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }
    // Unit-variance, roughly Gaussian (Irwin-Hall with four terms).
    double gauss() { return (uniform() + uniform() + uniform() + uniform() - 2.0) * 1.7320508075688772; }

private:
    uint64_t state = 1;
};

// A scratch is a narrow trench cut across the grooves, with material piled up
// along both edges. It sits at a fixed angle on the disc, so the stylus meets
// it once per revolution for as long as the groove radius is inside its span.
struct Scratch {
    double angle; // disc angle at rStart
    double twist; // angle change from rStart to rEnd (scratches are rarely exactly radial)
    double bend;  // extra bow in the middle of the span, in radians
    // Straight cuts: signed distance (m) of the cut's line from the disc centre
    // (0 = radial). A non-zero twist or bend only comes from older sessions.
    double lineOffset;
    double rStart, rEnd;
    double width, depth, burr;
    double bulgeWidth, bulgeHeight; // half-width and height of the pile-up around the cut
    double wallGain[2];

    bool covers(double r) const { return r >= rStart && r <= rEnd; }
    double angleAt(double r) const
    {
        const double s = (r - rStart) / (rEnd - rStart);
        double a = angle + twist * s + bend * 4.0 * s * (1.0 - s);
        if (lineOffset != 0.0) {
            // A point at radius r on a line passing lineOffset from the centre.
            const double p = std::fabs(lineOffset);
            const double line = std::acos(std::fmin(1.0, p / r)) - std::acos(std::fmin(1.0, p / rStart));
            a += lineOffset > 0.0 ? line : -line;
        }
        return a;
    }
};

constexpr int maxScratches = 32;
using ScratchSet = std::array<Scratch, maxScratches>;

// A scratch as the user cut it, independent of the disc size and of the
// Depth / Pile-up controls: its span is kept as a fraction of the recorded band
// (0 = inner end, 1 = outer end) and its random shape as factors.
struct DrawnScratch {
    double angle = 0.0, twist = 0.0, bend = 0.0;
    double tilt = 0.0; // angle between the (straight) cut and the radius through its middle
    double uStart = 0.0, uEnd = 1.0;
    double widthMicrons = 80.0;
    double depthFactor = 0.75, burrFactor = 0.25;
    double bulgeFactor = 1.0, heightFactor = 0.75;
    double wallGain[2] { 0.85, 0.85 };
};

struct ScratchList {
    std::array<DrawnScratch, maxScratches> items {};
    int count = 0;

    // When the list is full the oldest scratch gives way.
    void add(const DrawnScratch& s)
    {
        if (count == maxScratches) remove(0);
        items[static_cast<size_t>(count++)] = s;
    }
    void remove(int index)
    {
        if (index < 0 || index >= count) return;
        for (int i = index; i + 1 < count; ++i) items[static_cast<size_t>(i)] = items[static_cast<size_t>(i + 1)];
        --count;
    }
};

// Random cut width, depth and pile-up of one scratch (drawn in the same order
// as the earlier generated layouts, so those convert exactly).
inline void randomiseScratchShape(DrawnScratch& s, Rng& rng)
{
    s.widthMicrons = 30.0 + 130.0 * rng.uniform();
    s.depthFactor = 0.5 + 0.5 * rng.uniform();
    s.burrFactor = 0.1 + 0.3 * rng.uniform();
    // The displaced vinyl spreads wider than the cut itself and rises by a
    // good part of the cut depth.
    s.bulgeFactor = 0.6 + 0.8 * rng.uniform();
    s.heightFactor = 0.5 + 0.5 * rng.uniform();
    s.wallGain[0] = 0.7 + 0.3 * rng.uniform();
    s.wallGain[1] = 0.7 + 0.3 * rng.uniform();
}

inline Scratch buildScratch(const DrawnScratch& d, int discIndex, double depthMicrons, double pileUpMillimetres)
{
    const DiscGeometry g = discGeometry(discIndex);
    const double band = g.outerGroove - g.innerGroove;
    Scratch s {};
    s.angle = d.angle;
    s.twist = d.twist;
    s.bend = d.bend;
    s.rStart = g.innerGroove + d.uStart * band;
    s.rEnd = std::fmax(s.rStart + 1e-6, g.innerGroove + d.uEnd * band);
    const double offset = 0.5 * (s.rStart + s.rEnd) * std::sin(d.tilt);
    s.lineOffset = std::fmax(-0.95 * s.rStart, std::fmin(0.95 * s.rStart, offset));
    s.width = d.widthMicrons * 1e-6;
    s.depth = depthMicrons * d.depthFactor * 1e-6;
    s.burr = s.depth * d.burrFactor;
    s.bulgeWidth = pileUpMillimetres * d.bulgeFactor * 1e-3;
    s.bulgeHeight = s.depth * d.heightFactor;
    s.wallGain[0] = d.wallGain[0];
    s.wallGain[1] = d.wallGain[1];
    return s;
}

// A scratch cut by hand: centred on the groove radius r and disc angle theta
// where the nail left the disc, spanning `fraction` of the recorded band.
inline DrawnScratch cutScratch(int discIndex, double r, double theta, double fraction, uint64_t seed)
{
    const DiscGeometry g = discGeometry(discIndex);
    const double band = g.outerGroove - g.innerGroove;
    Rng rng(seed);
    DrawnScratch d;
    fraction = std::fmin(1.0, std::fmax(1e-3, fraction));
    const double u = std::fmin(1.0, std::fmax(0.0, (r - g.innerGroove) / band));
    d.uStart = std::fmin(1.0 - fraction, std::fmax(0.0, u - 0.5 * fraction));
    d.uEnd = d.uStart + fraction;
    // A straight cut, a little off the radial direction.
    d.tilt = (rng.uniform() - 0.5) * 0.4;
    rng.uniform(); // was the bend of curved cuts; keeps the shape factors below as before
    randomiseScratchShape(d, rng);
    // Keep the point under the nail on the cut.
    const Scratch built = buildScratch(d, discIndex, 1.0, 1.0);
    const double rOnCut = std::fmin(built.rEnd, std::fmax(built.rStart, r));
    d.angle = theta - built.angleAt(rOnCut);
    d.angle -= twoPi * std::floor(d.angle / twoPi);
    return d;
}

// The layouts generated by the earlier Count / Length / Layout controls, for
// sessions saved before scratches were cut by hand.
inline ScratchList legacyScratches(int seed, int discIndex, double lengthFraction, int count)
{
    const DiscGeometry g = discGeometry(discIndex);
    const double band = g.outerGroove - g.innerGroove;
    Rng rng(static_cast<uint64_t>(seed) * 7919ull + 17ull);
    ScratchList list;
    for (int i = 0; i < 8; ++i) {
        DrawnScratch d;
        d.angle = rng.uniform() * twoPi;
        d.twist = (rng.uniform() - 0.5) * 0.6;
        const double span = std::fmax(0.002, band * lengthFraction * (0.6 + 0.4 * rng.uniform()));
        const double centre = g.innerGroove + rng.uniform() * band;
        double rStart = std::fmax(g.innerGroove, centre - 0.5 * span);
        const double rEnd = std::fmin(g.outerGroove, rStart + span);
        rStart = std::fmax(g.innerGroove, rEnd - span);
        d.uStart = (rStart - g.innerGroove) / band;
        d.uEnd = (rEnd - g.innerGroove) / band;
        randomiseScratchShape(d, rng);
        if (i < count) list.add(d);
    }
    return list;
}

} // namespace dustytable
