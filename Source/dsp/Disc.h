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
    double rStart, rEnd;
    double width, depth, burr;
    double bulgeWidth, bulgeHeight; // half-width and height of the pile-up around the cut
    double wallGain[2];

    bool covers(double r) const { return r >= rStart && r <= rEnd; }
    double angleAt(double r) const { return angle + twist * (r - rStart) / (rEnd - rStart); }
};

constexpr int maxScratches = 8;
using ScratchSet = std::array<Scratch, maxScratches>;

// Always draws all eight scratches so that changing the count adds or removes
// scratches without reshuffling the others.
inline ScratchSet makeScratches(int seed, int discIndex, double lengthFraction, double depthMicrons,
    double pileUpMillimetres)
{
    const DiscGeometry g = discGeometry(discIndex);
    const double band = g.outerGroove - g.innerGroove;
    Rng rng(static_cast<uint64_t>(seed) * 7919ull + 17ull);
    ScratchSet set {};
    for (auto& s : set) {
        s.angle = rng.uniform() * twoPi;
        s.twist = (rng.uniform() - 0.5) * 0.6;
        const double span = std::fmax(0.002, band * lengthFraction * (0.6 + 0.4 * rng.uniform()));
        const double centre = g.innerGroove + rng.uniform() * band;
        s.rStart = std::fmax(g.innerGroove, centre - 0.5 * span);
        s.rEnd = std::fmin(g.outerGroove, s.rStart + span);
        s.rStart = std::fmax(g.innerGroove, s.rEnd - span);
        s.width = (30.0 + 130.0 * rng.uniform()) * 1e-6;
        s.depth = depthMicrons * (0.5 + 0.5 * rng.uniform()) * 1e-6;
        s.burr = s.depth * (0.1 + 0.3 * rng.uniform());
        // The displaced vinyl spreads wider than the cut itself and rises by a
        // good part of the cut depth.
        s.bulgeWidth = pileUpMillimetres * (0.6 + 0.8 * rng.uniform()) * 1e-3;
        s.bulgeHeight = s.depth * (0.5 + 0.5 * rng.uniform());
        s.wallGain[0] = 0.7 + 0.3 * rng.uniform();
        s.wallGain[1] = 0.7 + 0.3 * rng.uniform();
    }
    return set;
}

} // namespace dustytable
