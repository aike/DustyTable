#pragma once

#include "ArmSequencer.h"
#include "Biquad.h"
#include "Disc.h"
#include "Mechanics.h"

#include <array>
#include <cstdint>

namespace dustytable {

struct EngineParams {
    int rpmIndex = 0;     // 0: 33 1/3, 1: 45
    int discIndex = 0;    // 0: 12", 1: 10", 2: 7"
    int stylusIndex = 0;  // 0: conical, 1: elliptical
    double trackingForceGrams = 2.0;
    double dropPosition = 0.0; // where the arm sets the stylus down: 0 = lead-in, 1 = end of the recorded band
    double sideMinutes = 20.0;
    double dustPerMetre = 6.0;
    double dustMaxMicrons = 12.0;
    double scratchDepthMicrons = 20.0;
    double scratchPileUpMillimetres = 1.5; // half-width of the pile-up around each cut
    double staticAmount = 0.0; // 0..1
    bool armDown = true;
    double cueSeconds = 2.0;   // lifter descent time
};

// Synthesises the sound of an empty (unmodulated) groove by simulating the
// stylus riding on the two groove walls. Each wall carries its own dust and
// scratch profile; a nonlinear (Hertz, one-sided) contact drives a
// tip mass on a compliant cantilever attached to the tonearm. The cartridge
// output is the tip velocity relative to the arm, followed by RIAA playback EQ.
// The arm carries its own weight and can rest on the cue lifter or the arm
// rest instead of the stylus, which gives touchdown and lift-off; the hand
// actions around it are heard acoustically through Mechanics.
class VinylEngine {
public:
    void prepare(double sampleRate);
    void reset();
    void setParams(const EngineParams& p);
    // Puts the arm straight on the disc or on the rest, without a sequence.
    void setArmImmediate(bool down);
    // The scratches cut into the disc (by hand, see DrawnScratch).
    void setScratches(const ScratchList& list);

    // Renders n samples. tStart is the transport time in seconds, which fixes
    // the disc angle while the platter runs at speed. cartridge*
    // receive the phono output, mechanics* the airborne sound of the arm
    // mechanism, and contact (0..1) how firmly the stylus sits in the groove.
    void process(float* cartridgeL, float* cartridgeR, float* mechanicsL, float* mechanicsR, float* contact, int n,
        double tStart);

    double getPhase() const { return phase; }
    // Platter speed as a fraction of the nominal speed (0 = standing still).
    double getPlatterSpeed() const { return platterSpeed; }
    // Current stylus position (0 = lead-in, 1 = end of the recorded band).
    double getPosition() const { return position; }
    // Puts the stylus at a saved position (after setArmImmediate(true)).
    void setStylusPosition(double p);
    // The UP/DOWN button: raises the arm where it is, or lowers it again. While
    // the arm is held up, it is turned (slowly) to the drop point.
    void toggleCue() { arm.toggleCue(); }
    // Whether the cue mechanism holds (or is about to hold) the arm up.
    bool isCueUp() const { return arm.wantsCueUp(); }
    // A loaded session with the arm cued up (after setArmImmediate(true) and setStylusPosition).
    void setCueImmediate(bool up);
    // True once each time the stylus has reached the end of the side and the
    // deck has stopped by itself; the host-side Start/Stop should follow.
    bool takeEndOfSide()
    {
        const bool r = endOfSidePending;
        endOfSidePending = false;
        return r;
    }
    double getRadius() const { return radius; }
    double getArmTransit() const { return arm.transitPosition(); }
    double getArmHeight() const { return stylus[0].x; }
    bool isLifterRaised() const { return arm.isLifterRaised(); }
    bool isMotorRunning() const { return arm.getMotor().running(); }
    bool isClamped() const { return arm.isClamped(); }

private:
    struct Stylus {
        double y = 0.0, vy = 0.0; // tip, along the wall normal
        double x = 0.0, vx = 0.0; // tonearm / cartridge body
    };

    struct Particle {
        double s0 = 0.0, rho = 0.0, reach = 0.0;
        uint8_t walls = 0;
    };

    void updateStylusConstants();
    void settleStylus();
    void drawNextParticle(double fromS);
    double dustProfile(int wall, double s) const;
    double scratchProfile(const Scratch& sc, double x, int wall) const;

    struct Supports {
        double lifter, lifterSpeed;
        bool overDisc, overRest;
    };
    struct Forces {
        double lifter = 0.0, cradle = 0.0, groove = 0.0;
    };
    double stepStylus(Stylus& st, double z, const Supports& s, Forces& f);

    EngineParams params;
    double sampleRate = 48000.0;
    int oversampling = 8;
    double dtHigh = 1.0 / 384000.0;

    // stylus model
    double tipRadius = 17.8e-6;     // radius seen along the groove (tracing)
    double contactConstant = 0.0;   // Hertz: F = K * delta^1.5
    double tipMass = 0.6e-6;
    double preload = 0.0;           // per-wall share of the tracking force
    double cantileverDamping = 0.0;
    double armDamping = 0.0;
    int stylusIndexApplied = -1;

    std::array<Stylus, 2> stylus;
    std::array<Biquad, 2> riaa;
    std::array<Biquad, 2> sparkResonance;
    std::array<std::array<Biquad, 3>, 2> antiAlias;

    std::array<Particle, 48> particles;
    int particleCount = 0;
    Particle pending;
    bool pendingValid = false;
    Rng eventRng;
    double groovePath = 0.0; // distance travelled along the groove

    ScratchSet scratches {};
    ScratchList drawnScratches;
    int scratchCount = 0;
    int scratchDiscApplied = -1;
    double scratchDepthApplied = -1.0, scratchPileUpApplied = -1.0;
    void rebuildScratches();

    double charge = 0.0;
    double chargeDrift = 0.0;
    bool avalanche = false;
    double avalancheEnd = 0.0;

    ArmSequencer arm;
    Mechanics mechanics;
    double contactSmoothed = 1.0;
    double contactSmoothing = 0.0;
    double lastLifter = 0.0;

    double phase = 0.0, position = 0.0, radius = 0.1;

    // The stylus moves inward with the groove once it has landed; at the end of
    // the side the deck stops by itself and stays stopped until Stop is set.
    bool landed = true;
    int traverseDirection = 0; // arm traverse towards the drop point while cued up
    bool endOfSide = false, endOfSidePending = false;

    // The platter runs from START until STOP has put the arm back on its rest.
    bool platterMotor = true;
    double platterSpeed = 1.0;
    double platterOffset = 0.0; // disc angle lost to stops, relative to the transport
};

} // namespace dustytable
