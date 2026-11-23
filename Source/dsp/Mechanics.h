#pragma once

#include "ArmSequencer.h"
#include "Biquad.h"
#include "Disc.h"

#include <array>
#include <vector>

namespace dustytable {

// Airborne sound of the deck's controls and arm mechanism: START/STOP push
// buttons, cam motor and gears, cam follower lever, limit switch, cue lifter,
// arm-rest stop and arm rest (it never passes through the cartridge). Contact
// and drive forces excite the vibration modes of the parts involved; each mode
// radiates according to the part's size and its radiation efficiency, heard
// from a listening distance.
class Mechanics {
public:
    enum class Gesture { ClampOpen, ClampClose, PressStart, PressStop, SwitchIn, SwitchOut };
    enum class Contact { Lifter, Cradle }; // continuous contacts computed by the engine

    void prepare(double sampleRate, int oversampling);
    void reset();

    void trigger(Gesture gesture);
    // The arm, carried by the clutch, reaches the arm-rest stop at this speed (m/s).
    void armStop(double speed);
    // Impulse (N s) delivered by a contact during the current output sample.
    void addImpulse(Contact contact, double impulse);
    // Forces the running cam motor puts into its mount and gearbox this sample.
    void drive(const CamMotor& motor);
    // Height of the cam track under the follower lever (m) and the cam phase, this sample.
    void follow(double trackHeight, double camPhase);
    void render(float& left, float& right);

private:
    enum Part { ArmTube, Base, Clamp, LifterPost, RestPost, Motor, Gearbox, Key, Panel, Leaf, Lever, PartCount };

    struct Mode {
        double re = 0.0, im = 0.0;
        double rotRe = 1.0, rotIm = 0.0; // decay * e^{i w dt}
        double input = 0.0;              // 1 / modal mass
        double output = 0.0;             // radiated pressure per unit modal acceleration
        double omega = 0.0;
        double q = 1.0;                  // quality factor (kept when the mode is retuned)
        double nominal = 0.0;            // frequency (rad/s) as computed from the part's dimensions
    };
    struct PartModes {
        std::vector<Mode> modes;
        double gainL = 0.7, gainR = 0.7;
    };
    struct Coupling {
        Part part;
        double gain;
    };
    struct Excitation {
        std::array<Coupling, 4> to;
        int count;
    };
    // A mass thrown against a stop (Hunt-Crossley contact). A constant push
    // (spring preload, clutch) keeps it coming back until it settles.
    struct Impactor {
        bool active = false;
        int delay = 0, remaining = 0;
        double mass = 0.0, stiffness = 0.0, damping = 0.0, push = 0.0;
        double penetration = 0.0, velocity = 0.0;
        double release = 1.0; // how far the support that held the push has let go (0..1)
        Excitation target {};
    };
    struct ImpactSpec {
        double mass, velocity, stiffness, damping, push, seconds;
        Excitation target;
    };
    // Tactile push button: key and plunger pressed by a fingertip onto a
    // snap dome, with a bottom stop and the bezel as top stop.
    struct Button {
        bool active = false;
        double time = 0.0, hold = 0.0;
        double x = 0.0, v = 0.0; // key travel (down positive)
        double finger = 0.0, fingerSpeed = 0.0;
        double domeScale = 1.0; // this press: how stiff the dome feels where the finger lands
        Excitation target {};
    };
    // Spring-loaded lever riding on the cam track (one-sided contact).
    struct Follower {
        double y = 0.0, v = 0.0;
        double lastTrack = 0.0;
        double contactTime = 0.0; // how long the lever has been riding the track (s)
        bool primed = false;
    };

    // Cantilever with a mass at its free end, driven at a fraction of its length.
    void addBeam(Part part, double youngs, double density, double inertia, double area, double length,
        double tipMass, double drivePoint, double q, double radiatingArea, double corner, int count);
    // Simply supported plate, modes up to (maxM, maxN), driven at (x0, y0) as fractions of its sides.
    void addPlate(Part part, double youngs, double density, double thickness, double a, double b, double q,
        double radiationCorner, int maxM, int maxN, double x0, double y0);
    void addMode(Part part, double frequency, double modalMass, double q, double radiatingArea, double radiationCorner);
    void excite(const Excitation& e, double impulse);
    // Every action is a little different: where exactly the parts meet, and how
    // tightly they sit, shifts each part's resonances and how strongly it is driven.
    Excitation vary(const Excitation& e);
    void retune(Part part, double factor);
    void startImpact(const ImpactSpec& spec, double delaySeconds);
    void press(int which);
    double stepButton(Button& b);
    double stepFollower(double track, double trackSpeed);
    double roughness(double camPhase) const;

    double sampleRate = 48000.0;
    int oversampling = 8;
    double dtHigh = 1.0 / 384000.0;

    std::array<PartModes, PartCount> parts;
    std::array<Impactor, 6> impactors;
    std::array<Button, 2> buttons;
    Follower follower;
    // Cam follower lever into the gearbox frame, the plinth and the lifter post.
    static constexpr Excitation leverNominal { { { { Lever, 1.0 }, { Gearbox, 0.05 }, { Base, 0.35 }, { LifterPost, 0.1 } } }, 4 };
    Excitation leverTarget {};
    std::vector<double> camRoughness;
    std::array<double, CamMotor::wheelTeeth> wheelError {};
    std::array<double, CamMotor::pinionTeeth> pinionError {};
    std::array<double, CamMotor::camTeeth> camGearError {};
    static constexpr double clampMass = 0.45e-3; // spring strip with its hook (kg)

    std::array<double, 2> pendingContact { 0.0, 0.0 };
    std::array<double, 2> baseline { 0.0, 0.0 };
    double contactBaseline = 0.0;
    bool baselinePrimed = false;
    double pendingTrack = 0.0, pendingPhase = 0.0;
    bool trackValid = false;
    std::array<double, PartCount> lastVelocity {};
    Biquad grommetFilter;
    double dcL = 0.0, dcR = 0.0, dcCoeff = 0.0;
    Rng rng { 2718 };
};

} // namespace dustytable
