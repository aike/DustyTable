#pragma once

#include "Disc.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dustytable {

// Heights along the wall normal (m), relative to the vinyl surface.
namespace armHeights {
constexpr double lifterUp = 5.0e-3;   // arm raised by the cue lifter
constexpr double lifterDown = -3.0e-3; // lifter parked below the arm
constexpr double cradle = 1.5e-3;     // arm tube resting on the arm rest
} // namespace armHeights

// Small brushed DC motor driving the arm cam through a worm and a spur stage.
// The electrical and mechanical equations are integrated per output sample;
// the forces it puts into its mount and gearbox are what is heard.
class CamMotor {
public:
    static constexpr double camTravel = twoPi;    // one cam revolution: down half, then up half
    static constexpr double wormRatio = 30.0;     // single-start worm, 30-tooth wheel
    static constexpr double spurRatio = 11.0;     // 8-tooth pinion, 88-tooth cam gear
    static constexpr int wheelTeeth = 30;
    static constexpr int pinionTeeth = 8;
    static constexpr int camTeeth = 88;

    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        currentStep = 1.0 - std::exp(-resistance / (inductance * sampleRate));
    }
    void reset()
    {
        omega = current = 0.0;
        voltage = 0.0;
    }
    // off = terminals shorted (braked). The supply sags a little differently each
    // run (battery, contact resistance), which shifts the motor speed and the gear whine.
    void setDrive(bool on, double supplyScale = 1.0) { voltage = on ? supply * supplyScale : 0.0; }

    // Advances one output sample; returns the change of cam phase (1 per revolution).
    double tick()
    {
        const double dt = 1.0 / sampleRate;
        // Three-slot rotor: the torque and back-EMF constants ripple six times per turn.
        const double k = torqueConstant * (1.0 + commutatorRipple * std::cos(6.0 * angle));
        current += currentStep * ((voltage - k * omega) / resistance - current);
        // Each commutator segment rubs the brushes with its own roughness: the
        // brush friction changes in steps as segments pass.
        const double brush = brushNoise * segmentRoughness * std::tanh(omega / 20.0);
        torque = k * current;
        const double friction = gearFriction * std::tanh(omega / 20.0);
        omega += (torque - viscous * omega - friction - brush) / inertia * dt;
        const double segmentBefore = std::floor(angle * 6.0 / twoPi);
        angle += omega * dt;
        if (std::floor(angle * 6.0 / twoPi) != segmentBefore) segmentRoughness = rng.gauss();
        if (angle > twoPi * 1000.0 || angle < -twoPi * 1000.0) angle = std::fmod(angle, twoPi);
        wormAngle += omega * dt;
        // Worm position kept within whole revolutions of the cam gear so the tooth indices stay exact.
        const double camGearTurn = twoPi * wormRatio * spurRatio;
        if (wormAngle > camGearTurn) wormAngle -= camGearTurn;
        brushTorque = brush;
        return omega * dt / (wormRatio * spurRatio * camTravel);
    }

    bool running() const { return voltage != 0.0 || std::abs(omega) > 1.0; }
    double speed() const { return omega; }
    double motorAngle() const { return angle; }
    double electromagneticTorque() const { return torque; }
    double brushFriction() const { return brushTorque; }
    double gearTorque() const { return gearFriction * std::tanh(omega / 20.0); }
    // Teeth that have passed through each mesh: the worm wheel and the pinion /
    // cam gear pair (whole numbers index the individual teeth).
    double wormMesh() const { return wormAngle / twoPi; }
    double spurMesh() const { return wormAngle / (twoPi * wormRatio) * pinionTeeth; }

private:
    // 1.5 V drive of a small 3 V motor (no-load about 5000 rpm at 1.5 V).
    static constexpr double supply = 1.5;
    static constexpr double resistance = 10.0;     // ohm
    static constexpr double inductance = 1.5e-3;   // H
    static constexpr double torqueConstant = 2.5e-3; // N m / A
    static constexpr double commutatorRipple = 0.08;
    static constexpr double inertia = 1.2e-7;      // kg m^2, rotor and worm
    static constexpr double viscous = 2e-8;        // N m s
    static constexpr double gearFriction = 1.5e-4; // N m, worm, gears and cam follower seen at the motor
    static constexpr double brushNoise = 4e-6;     // N m

    double sampleRate = 48000.0, currentStep = 0.1;
    double voltage = 0.0, current = 0.0, omega = 0.0, angle = 0.0, wormAngle = 0.0;
    double torque = 0.0, brushTorque = 0.0, segmentRoughness = 0.0;
    Rng rng { 4242 };
};

// Operates the turntable like an automatic deck: START and STOP push buttons,
// and a motor that turns a cam one way only. The first half of a cam
// revolution raises the lifter, swings the arm out over the lead-in and drops
// the lifter; the second half lifts the arm, swings it back against the
// arm-rest stop and lowers it onto the rest. A leaf limit switch riding on the
// cam drops into a notch at the end of each half and cuts the motor; a button
// latches the control circuit, which starts the motor (after START it first
// waits a second for the platter to come up to speed) and keeps it running
// until the leaf has climbed out of the notch. The arm-rest clamp is still
// worked by hand.
//
// The lifter rod sits on a spring-loaded follower lever. Each half of the cam
// lifts the lever on a gentle ramp and lets it fall off a step; the oil-damped
// rod then sinks at its own pace. The arm is swung through a slipping clutch,
// and the cam travels slightly past the arm-rest stop so that the arm is
// pressed against it.
class ArmSequencer {
public:
    enum class Event { None, ClampOpen, ClampClose, PressStart, PressStop, SwitchIn, SwitchOut, ArmStop };

    static constexpr double followerRise = 1.5e-3;   // m, lift of the follower lever on the cam
    static constexpr double metresPerTransit = 0.08; // arc at the arm-rest stop for a full swing
    static constexpr double overTravel = 0.06;       // cam swing beyond the stop (in swing units)

    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        motor.prepare(newSampleRate);
    }
    void setCueSeconds(double seconds) { cueSeconds = std::max(0.3, seconds); }

    // Jump straight to a resting state (used when a session is loaded).
    void setImmediate(bool down)
    {
        targetDown = down;
        clamped = !down;
        cam = (down ? 0.5 : 0.0) + 0.005;
        transit = down ? 1.0 : 0.0;
        sweep = sweepProfile(cam);
        lifter = armHeights::lifterDown;
        lifterVelocity = 0.0;
        motor.reset();
        stepCount = stepIndex = 0;
    }

    void request(bool down)
    {
        if (down == targetDown) return;
        targetDown = down;
        plan();
    }

    // Advances one output sample; returns how many events started on it (see event()).
    int tick()
    {
        const double dt = 1.0 / sampleRate;
        eventCount = 0;

        if (stepIndex < stepCount) {
            Step& s = steps[stepIndex];
            if (!s.started) {
                s.started = true;
                s.elapsed = 0.0;
                switch (s.kind) {
                case Kind::ClampOpen: clamped = false; push(Event::ClampOpen); break;
                case Kind::ClampClose: clamped = true; push(Event::ClampClose); break;
                case Kind::Press: push(targetDown ? Event::PressStart : Event::PressStop); break;
                case Kind::RunCam:
                    // The button bridges the limit switch; nothing happens if the
                    // cam already stands where it should.
                    if (!atStation(targetDown)) motor.setDrive(true, 0.94 + 0.08 * rng.uniform());
                    break;
                default: break;
                }
            }
            s.elapsed += dt;
            bool done = s.elapsed >= s.duration;
            if (s.kind == Kind::RunCam) {
                done = !motor.running();
            } else if (s.kind == Kind::WaitLifterParked) {
                done = lifter <= armHeights::lifterDown + 1e-6;
            } else if (s.kind == Kind::WaitArmOnRest) {
                done = done && lifter <= armHeights::cradle - 1e-3; // lifter has dropped away from the arm
            }
            if (done) ++stepIndex;
        }

        const double previousCam = cam;
        cam += motor.tick();
        for (int k = 0; k < 2; ++k) {
            const double station = 0.5 * k;
            // The leaf climbs out of the notch and closes the motor circuit.
            const double exit = station + notchExit;
            if (previousCam < exit && cam >= exit) push(Event::SwitchOut);
        }
        if (previousCam < 0.5 && cam >= 0.5) arriveAt(true);
        if (cam >= 1.0) {
            cam -= 1.0;
            arriveAt(false);
        }

        const double previousLifter = lifter;
        const double follower = followerProfile(cam);
        const double rod = armHeights::lifterDown + (armHeights::lifterUp - armHeights::lifterDown) * follower;
        if (rod >= lifter) {
            lifter = rod;
        } else {
            // The damper piston only works inside its bore; for the last fraction of
            // a millimetre it runs free and the rod drops onto the lever, so the arm
            // falls the rest of the way onto the record on its own.
            const double damped = lifter - armHeights::lifterUp / cueSeconds * dt;
            lifter = damped > freeFall ? std::max(rod, damped) : rod;
        }
        lifterVelocity = (lifter - previousLifter) / dt;

        // The clutch carries the arm with the cam until the arm-rest stop holds it.
        const double previousSweep = sweep;
        sweep = sweepProfile(cam);
        transit = std::max(0.0, sweep);
        if (previousSweep > 0.0 && sweep <= 0.0) {
            stopSpeed = (previousSweep - sweep) / dt * metresPerTransit;
            push(Event::ArmStop);
        }
        return eventCount;
    }

    Event event(int index) const { return events[static_cast<size_t>(index)]; }
    double lifterHeight() const { return lifter; }
    double lifterSpeed() const { return lifterVelocity; }
    // Height of the cam track under the follower lever (m).
    double followerHeight() const { return followerRise * followerProfile(cam); }
    double camPhase() const { return cam; }
    // Speed (m/s) at which the arm reached the arm-rest stop (valid with Event::ArmStop).
    double armStopSpeed() const { return stopSpeed; }
    double transitPosition() const { return transit; } // 0 = over the rest, 1 = over the disc
    bool overDisc() const { return transit >= 1.0; }
    bool overRest() const { return transit <= 0.0; }
    bool isLifterRaised() const { return lifter > 0.0; }
    bool isClamped() const { return clamped; }
    bool isBusy() const { return stepIndex < stepCount; }
    const CamMotor& getMotor() const { return motor; }

private:
    enum class Kind { Wait, ClampOpen, ClampClose, Press, RunCam, WaitLifterParked, WaitArmOnRest };
    struct Step {
        Kind kind = Kind::Wait;
        double duration = 0.0;
        bool started = false;
        double elapsed = 0.0;
    };

    static constexpr double notchExit = 0.015; // cam phase at which the leaf has left a notch
    static constexpr double dropPoint = 0.68; // step in the lift track, as a fraction of a half
    static constexpr double dropStep = 0.2;   // height of that step, as a fraction of the rise

    static double ramp(double x, double a, double b)
    {
        const double u = std::clamp((x - a) / (b - a), 0.0, 1.0);
        return 0.5 - 0.5 * std::cos(pi * u);
    }
    static double half(double phase) { return phase < 0.5 ? 2.0 * phase : 2.0 * phase - 1.0; }
    // Each half: ramp up, hold while the arm swings, then a short step down
    // (a fifth of the rise) followed by a steep descent.
    static double followerProfile(double phase)
    {
        const double h = half(phase);
        return h < dropPoint ? ramp(h, 0.04, 0.28) : (1.0 - dropStep) * (1.0 - ramp(h, dropPoint, dropPoint + 0.06));
    }
    // Where the cam would put the arm (below 0 the stop holds it back).
    static double sweepProfile(double phase)
    {
        const double travel = (1.0 + overTravel) * ramp(half(phase), 0.32, 0.64);
        return phase < 0.5 ? -overTravel + travel : 1.0 - travel;
    }

    bool atStation(bool down) const
    {
        if (motor.running()) return false;
        const double station = down ? 0.5 : 0.0;
        double d = cam - station;
        d -= std::floor(d + 0.5);
        return std::abs(d) < notchExit;
    }

    // The leaf drops into a notch: it opens the circuit if this is where the arm should be.
    void arriveAt(bool discStation)
    {
        if (discStation == targetDown) motor.setDrive(false);
        push(Event::SwitchIn);
    }
    void push(Event e)
    {
        if (eventCount < static_cast<int>(events.size())) events[static_cast<size_t>(eventCount++)] = e;
    }

    void add(Kind kind, double duration)
    {
        if (stepCount < static_cast<int>(steps.size())) steps[stepCount++] = { kind, duration };
    }
    // Hands are not machines: every pause varies a little.
    double human(double seconds) { return seconds * (0.8 + 0.4 * rng.uniform()); }

    // Plans the remaining actions from the current physical state. A request
    // while the cam is turning only changes where it will stop (the cam never
    // runs backwards).
    void plan()
    {
        stepCount = stepIndex = 0;
        if (targetDown) {
            if (clamped) {
                add(Kind::ClampOpen, human(0.6));
            }
            freeFall = (0.6 + 0.4 * rng.uniform()) * 1e-3; // where the damper lets go this time
            add(Kind::Press, pressTime);
            add(Kind::Wait, startDelay); // the deck waits for the platter to come up to speed
            add(Kind::RunCam, 0.0);
            add(Kind::WaitLifterParked, 0.0);
        } else {
            add(Kind::Press, pressTime);
            add(Kind::RunCam, 0.0);
            add(Kind::WaitArmOnRest, human(0.4));
            add(Kind::ClampClose, 0.0);
        }
    }

    static constexpr double pressTime = 0.04; // finger travel until the switch dome snaps
    static constexpr double startDelay = 1.0; // s from START to the arm mechanism starting

    double sampleRate = 48000.0;
    double cueSeconds = 2.0;
    double freeFall = 0.8e-3; // lifter height (m) below which the damper no longer holds the rod
    bool targetDown = true;
    bool clamped = false;
    double cam = 0.505;
    double transit = 1.0, sweep = 1.0;
    double stopSpeed = 0.0;
    double lifter = armHeights::lifterDown, lifterVelocity = 0.0;
    CamMotor motor;
    std::array<Step, 8> steps {};
    std::array<Event, 4> events {};
    int eventCount = 0;
    int stepCount = 0, stepIndex = 0;
    Rng rng { 31337 };
};

} // namespace dustytable
