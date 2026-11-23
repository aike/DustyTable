#include "VinylEngine.h"

#include <algorithm>
#include <cmath>

namespace dustytable {

namespace {

constexpr double vinylModulus = 3.5e9;        // effective modulus E* of PVC against diamond (Pa)
constexpr double cantileverStiffness = 50.0;  // N/m, i.e. 20 um/mN compliance
constexpr double armMass = 0.012;             // kg, effective tonearm + cartridge mass
constexpr double armQ = 1.5;                  // arm/cartridge low-frequency resonance (~10 Hz)
constexpr double tipZeta = 0.12;              // damping of the tip-mass / vinyl resonance
constexpr double lifterStiffness = 3.0e4;     // silicone pad of the cue lifter bar (N/m)
constexpr double lifterZeta = 0.4;
constexpr double cradleStiffness = 2.0e4;     // rubber cradle of the arm rest (N/m)
constexpr double cradleZeta = 0.3;
constexpr double gravity = 9.80665;
constexpr double maxPenetration = 50e-6;      // keeps the explicit integrator stable
constexpr double referenceVelocity = 0.05;    // 5 cm/s at 1 kHz ...
constexpr double referenceLevel = 0.125;      // ... plays back at -18 dBFS
constexpr double crosstalk = 0.056;           // -25 dB channel separation
constexpr double targetInternalRate = 352800.0;
constexpr double minDustMicrons = 0.3;
constexpr double dustSizeExponent = 1.5;      // Pareto: small particles are far more common
// A static discharge is a short current pulse into the cartridge. It rings the
// resonance of the coil inductance (~0.5 H) with the load capacitance (~200 pF)
// and enters before the RIAA stage; scaled as an equivalent stylus velocity.
constexpr double staticPulseVelocity = 5.0;
constexpr double staticMaxRate = 1500.0;      // discharges per second inside an avalanche
constexpr double staticIdleRate = 60.0;       // isolated discharges between avalanches
constexpr double staticOnsetRate = 25.0;      // avalanches per second at full charge
constexpr double staticDrainPerEvent = 0.015;
constexpr double cartridgeResonance = 16000.0;
constexpr double cartridgeQ = 3.0;

struct StylusSpec {
    double tracingRadius; // along the groove
    double contactRadius; // Hertz contact against the wall
    double mass;          // effective tip mass
};

StylusSpec stylusSpec(int index)
{
    // 0.7 mil conical; 0.3 x 0.7 mil elliptical
    if (index == 1) return { 7.6e-6, 11.6e-6, 0.5e-6 };
    return { 17.8e-6, 17.8e-6, 0.6e-6 };
}

double frac(double x) { return x - std::floor(x); }

double wrapPi(double x)
{
    x = std::fmod(x + pi, twoPi);
    if (x < 0.0) x += twoPi;
    return x - pi;
}

} // namespace

void VinylEngine::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    oversampling = std::clamp(static_cast<int>(std::ceil(targetInternalRate / sampleRate)), 1, 16);
    const double highRate = sampleRate * oversampling;
    dtHigh = 1.0 / highRate;

    const double cutoff = std::min(20000.0, 0.45 * sampleRate);
    const double qs[3] = { 0.5176380902, 0.7071067812, 1.9318516526 }; // 6th-order Butterworth
    for (int w = 0; w < 2; ++w) {
        riaa[w] = Biquad::riaaPlayback(highRate);
        sparkResonance[w] = Biquad::bandpass(highRate, cartridgeResonance, cartridgeQ);
        for (int k = 0; k < 3; ++k)
            antiAlias[w][k] = Biquad::lowpass(highRate, cutoff, qs[k]);
    }
    arm.prepare(sampleRate);
    mechanics.prepare(sampleRate, oversampling);
    contactSmoothing = 1.0 - std::exp(-1.0 / (0.005 * sampleRate));

    reset();
}

void VinylEngine::reset()
{
    for (int w = 0; w < 2; ++w) {
        riaa[w].reset();
        sparkResonance[w].reset();
        for (auto& f : antiAlias[w]) f.reset();
    }
    eventRng.setSeed(777);
    particleCount = 0;
    pendingValid = false;
    groovePath = 0.0;
    charge = 0.0;
    chargeDrift = 0.0;
    avalanche = false;
    mechanics.reset();
    stylusIndexApplied = -1;
    updateStylusConstants();
    setArmImmediate(params.armDown);
}

void VinylEngine::setArmImmediate(bool down)
{
    params.armDown = down;
    arm.setImmediate(down);
    mechanics.reset();
    lastLifter = arm.lifterHeight();
    contactSmoothed = down ? 1.0 : 0.0;
    settleStylus();
}

void VinylEngine::setParams(const EngineParams& p)
{
    params = p;
    params.scratchCount = std::clamp(params.scratchCount, 0, maxScratches);
    updateStylusConstants();
    arm.setCueSeconds(p.cueSeconds);
    arm.request(p.armDown);

    if (p.scratchSeed != scratchSeedApplied || p.discIndex != scratchDiscApplied
        || p.scratchLength != scratchLengthApplied || p.scratchDepthMicrons != scratchDepthApplied
        || p.scratchPileUpMillimetres != scratchPileUpApplied) {
        scratches = makeScratches(p.scratchSeed, p.discIndex, p.scratchLength, p.scratchDepthMicrons,
            p.scratchPileUpMillimetres);
        scratchSeedApplied = p.scratchSeed;
        scratchDiscApplied = p.discIndex;
        scratchLengthApplied = p.scratchLength;
        scratchDepthApplied = p.scratchDepthMicrons;
        scratchPileUpApplied = p.scratchPileUpMillimetres;
    }
}

void VinylEngine::updateStylusConstants()
{
    const StylusSpec spec = stylusSpec(params.stylusIndex);
    tipRadius = spec.tracingRadius;
    tipMass = spec.mass;
    contactConstant = 4.0 / 3.0 * vinylModulus * std::sqrt(spec.contactRadius);
    // Each 45-degree wall carries the tracking force divided by sqrt(2).
    preload = params.trackingForceGrams * 1e-3 * gravity / std::sqrt(2.0);

    const double rest = std::pow(preload / contactConstant, 2.0 / 3.0);
    const double contactStiffness = 1.5 * contactConstant * std::sqrt(rest);
    cantileverDamping = 2.0 * tipZeta * std::sqrt(contactStiffness * tipMass);
    armDamping = std::sqrt(cantileverStiffness * armMass) / armQ;

    if (params.stylusIndex != stylusIndexApplied) {
        stylusIndexApplied = params.stylusIndex;
        settleStylus();
    }
}

void VinylEngine::settleStylus()
{
    for (auto& st : stylus) {
        st.vy = st.vx = 0.0;
        if (arm.overDisc() && arm.lifterHeight() < 0.0) {
            // Stylus in the groove, cantilever deflected by the arm's weight.
            st.y = -std::pow(preload / contactConstant, 2.0 / 3.0);
            st.x = st.y - preload / cantileverStiffness;
        } else {
            // Arm carried by the lifter or the rest, stylus hanging free.
            const bool onRest = arm.overRest() && arm.lifterHeight() < armHeights::cradle;
            st.x = onRest ? armHeights::cradle - preload / cradleStiffness
                          : arm.lifterHeight() - preload / lifterStiffness;
            st.y = st.x;
        }
    }
}

void VinylEngine::drawNextParticle(double fromS)
{
    const double density = params.dustPerMetre * (1.0 + 3.0 * params.staticAmount * charge);
    if (density <= 1e-9 || params.dustMaxMicrons <= minDustMicrons) {
        pendingValid = false;
        return;
    }
    const double gap = -std::log(eventRng.uniformOpen()) / density;

    const double rMin = minDustMicrons * 1e-6, rMax = params.dustMaxMicrons * 1e-6;
    const double span = 1.0 - std::pow(rMin / rMax, dustSizeExponent);
    const double rho = rMin * std::pow(1.0 - eventRng.uniform() * span, -1.0 / dustSizeExponent);

    const double side = eventRng.uniform();
    pending.walls = side < 0.45 ? 1 : (side < 0.9 ? 2 : 3);
    pending.s0 = fromS + gap;
    pending.rho = rho;
    // Large particles get pushed and roll instead of presenting a vertical face,
    // so the approach radius never drops below 1.5x the particle radius.
    const double a = std::max(tipRadius, 1.5 * rho);
    pending.reach = 2.0 * std::sqrt(a * rho);
    pendingValid = true;
}

double VinylEngine::dustProfile(int wall, double s) const
{
    const uint8_t bit = static_cast<uint8_t>(1 << wall);
    double h = 0.0;
    for (int i = 0; i < particleCount; ++i) {
        const Particle& p = particles[i];
        if ((p.walls & bit) == 0) continue;
        const double x = s - p.s0;
        if (std::abs(x) >= p.reach) continue;
        // Path of the tip centre rolling over a sphere resting on the wall.
        const double a = std::max(tipRadius, 1.5 * p.rho);
        const double q = (a + p.rho) * (a + p.rho) - x * x;
        h = std::max(h, p.rho + std::sqrt(std::max(0.0, q)) - a);
    }
    return h;
}

double VinylEngine::scratchProfile(const Scratch& sc, double x, int wall) const
{
    const double depth = sc.depth * sc.wallGain[wall];
    const double burr = sc.burr * sc.wallGain[wall];
    const double half = 0.5 * sc.width;
    const double burrWidth = 0.5 * sc.width;
    const double ax = std::abs(x);

    // Vinyl displaced by the cut piles up in a broad, smooth rise. It lifts the
    // stylus slowly enough to pass the RIAA bass boost, but faster than the
    // ~10 Hz arm resonance, so the arm stays put and the cartridge sees it:
    // this is the low "thud".
    double base = 0.0;
    if (ax < sc.bulgeWidth) base = sc.bulgeHeight * sc.wallGain[wall] * 0.5 * (1.0 + std::cos(pi * ax / sc.bulgeWidth));

    double z = 0.0;
    if (ax < half) {
        z = -depth;
    } else if (ax < half + burrWidth) {
        const double q = 1.0 - (ax - half) / burrWidth;
        z = burr * q * q;
    }
    // The rounded tip rests on the trench corners (raised by the burr) instead
    // of following the sharp edges.
    const double a = tipRadius;
    const double near = std::abs(ax - half);
    if (near < a) z = std::max(z, burr + std::sqrt(a * a - near * near) - a);
    const double far = ax + half;
    if (far < a) z = std::max(z, burr + std::sqrt(a * a - far * far) - a);
    return base + z;
}

double VinylEngine::stepStylus(Stylus& st, double z, const Supports& s, Forces& f)
{
    double force = 0.0;
    if (s.overDisc) {
        const double penetration = std::min(z - st.y, maxPenetration);
        if (penetration > 0.0) force = contactConstant * penetration * std::sqrt(penetration);
    }

    // Cantilever deflection relative to the cartridge body. The arm's weight
    // (the preload) bends it until the groove carries the tracking force.
    const double u = st.y - st.x;
    const double du = st.vy - st.vx;
    const double spring = -cantileverStiffness * u - cantileverDamping * du;

    // One-sided supports from below: the cue lifter pad and the arm-rest cradle.
    double lift = 0.0;
    if (st.x < s.lifter) {
        const double c = 2.0 * lifterZeta * std::sqrt(lifterStiffness * armMass);
        lift = std::max(0.0, lifterStiffness * (s.lifter - st.x) + c * (s.lifterSpeed - st.vx));
    }
    double cradle = 0.0;
    if (s.overRest && st.x < armHeights::cradle) {
        const double c = 2.0 * cradleZeta * std::sqrt(cradleStiffness * armMass);
        cradle = std::max(0.0, cradleStiffness * (armHeights::cradle - st.x) - c * st.vx);
    }

    const double tipAcc = (force + spring) / tipMass;
    const double armAcc = (-spring - preload - armDamping * st.vx + lift + cradle) / armMass;
    f.lifter += lift;
    f.cradle += cradle;
    f.groove += force;

    st.vy += tipAcc * dtHigh;
    st.vx += armAcc * dtHigh;
    st.y += st.vy * dtHigh;
    st.x += st.vx * dtHigh;
    // Moving-magnet generator: output follows the relative velocity.
    return st.vy - st.vx;
}

void VinylEngine::process(float* left, float* right, float* mechanicsL, float* mechanicsR, float* contact, int n,
    double tStart)
{
    const DiscGeometry geom = discGeometry(params.discIndex);
    const double rps = rpmValue(params.rpmIndex) / 60.0;
    const double omega = twoPi * rps;
    const double sideSeconds = std::max(1.0, params.sideMinutes * 60.0);
    const double outScale = referenceLevel / referenceVelocity;
    const double dtOut = 1.0 / sampleRate;
    const double driftSmoothing = 1.0 - std::exp(-dtOut * twoPi * 0.3);

    struct Near {
        const Scratch* scratch;
        double x;
    };

    for (int i = 0; i < n; ++i) {
        const double t = tStart + i * dtOut;
        position = params.autoAdvance ? frac(params.position + t / sideSeconds) : params.position;
        radius = grooveRadius(geom, position);
        phase = frac(rps * t) * twoPi;
        const double speed = omega * radius;
        const double step = speed * dtHigh;

        const int armEvents = arm.tick();
        for (int e = 0; e < armEvents; ++e) {
            using E = ArmSequencer::Event;
            using G = Mechanics::Gesture;
            switch (arm.event(e)) {
            case E::ClampOpen: mechanics.trigger(G::ClampOpen); break;
            case E::ClampClose: mechanics.trigger(G::ClampClose); break;
            case E::PressStart: mechanics.trigger(G::PressStart); break;
            case E::PressStop: mechanics.trigger(G::PressStop); break;
            case E::SwitchIn: mechanics.trigger(G::SwitchIn); break;
            case E::SwitchOut: mechanics.trigger(G::SwitchOut); break;
            case E::ArmStop: mechanics.armStop(arm.armStopSpeed()); break;
            case E::None: break;
            }
        }
        mechanics.follow(arm.followerHeight(), arm.camPhase());
        const double lifter = arm.lifterHeight();
        Supports supports { lastLifter, arm.lifterSpeed(), arm.overDisc(), arm.overRest() };
        const bool inGroove = contactSmoothed > 0.5;

        // Static charge builds up with friction and discharges in bursts; it
        // needs the stylus in the groove, otherwise the charge just leaks away.
        int kickWalls = 0;
        double kick = 0.0;
        if (params.staticAmount <= 0.0) {
            charge = 0.0;
            avalanche = false;
        } else if (!inGroove) {
            charge *= 1.0 - 0.5 * dtOut;
            avalanche = false;
        } else {
            chargeDrift += driftSmoothing * (eventRng.gauss() * 6.0 - chargeDrift);
            const double drive = 0.8 * params.staticAmount * (speed / 0.4)
                                 * std::clamp(0.6 + 0.8 * chargeDrift, 0.0, 2.0);
            charge = std::clamp(charge + (drive - 0.5 * charge) * dtOut, 0.0, 1.5);
            // Charge is released by avalanches: once one starts, tiny
            // micro-discharges follow densely until a part of the charge is gone,
            // then the disc charges up again. Between avalanches only a few
            // isolated ticks occur.
            const double c2 = charge * charge;
            if (!avalanche && eventRng.uniform() < staticOnsetRate * c2 * c2 * dtOut) {
                avalanche = true;
                avalancheEnd = charge * (0.4 + 0.4 * eventRng.uniform());
            }
            const double rate = avalanche ? staticMaxRate * charge : staticIdleRate * c2 * c2;
            if (eventRng.uniform() < rate * dtOut) {
                const double u = eventRng.uniform();
                const double magnitude = charge * (0.1 + 0.9 * u * u * u * u * u * u);
                charge = std::max(0.0, charge - staticDrainPerEvent * magnitude);
                if (charge < avalancheEnd) avalanche = false;
                kick = magnitude * staticPulseVelocity * (eventRng.uniform() < 0.5 ? -1.0 : 1.0);
                const double side = eventRng.uniform();
                kickWalls = side < 0.4 ? 1 : (side < 0.8 ? 2 : 3);
            }
        }

        for (int k = 0; k < particleCount;) {
            if (groovePath > particles[k].s0 + particles[k].reach)
                particles[k] = particles[--particleCount];
            else
                ++k;
        }
        if (!pendingValid) drawNextParticle(groovePath);

        Near near[maxScratches];
        int nearCount = 0;
        for (int k = 0; k < params.scratchCount; ++k) {
            const Scratch& sc = scratches[k];
            if (!sc.covers(radius)) continue;
            const double x = wrapPi(phase - sc.angleAt(radius)) * radius;
            const double reach = std::max(sc.width + tipRadius, sc.bulgeWidth) + 2.0 * speed * dtOut;
            if (std::abs(x) < reach) near[nearCount++] = { &sc, x };
        }

        double out[2] = { 0.0, 0.0 };
        Forces forces;
        for (int k = 0; k < oversampling; ++k) {
            supports.lifter = lastLifter + (lifter - lastLifter) * (k + 1) / oversampling;
            groovePath += step;
            while (pendingValid && groovePath >= pending.s0 - pending.reach) {
                const Particle p = pending;
                if (particleCount < static_cast<int>(particles.size())) particles[particleCount++] = p;
                drawNextParticle(p.s0);
            }

            for (int w = 0; w < 2; ++w) {
                double z = 0.0;
                for (int j = 0; j < nearCount; ++j)
                    z += scratchProfile(*near[j].scratch, near[j].x + step * (k + 1), w);
                const double dust = dustProfile(w, groovePath);
                if (dust > 0.0) z = std::max(z, dust);

                const double pulse = (k == 0 && (kickWalls & (1 << w)) != 0) ? kick : 0.0;
                double y = stepStylus(stylus[w], z, supports, forces) + sparkResonance[w].process(pulse);
                y = riaa[w].process(y);
                for (auto& f : antiAlias[w]) y = f.process(y);
                out[w] = y;
            }
        }

        if (!std::isfinite(out[0]) || !std::isfinite(out[1])) {
            settleStylus();
            for (int w = 0; w < 2; ++w) {
                riaa[w].reset();
                for (auto& f : antiAlias[w]) f.reset();
            }
            out[0] = out[1] = 0.0;
        }
        lastLifter = lifter;

        mechanics.addImpulse(Mechanics::Contact::Lifter, forces.lifter * dtHigh);
        mechanics.addImpulse(Mechanics::Contact::Cradle, forces.cradle * dtHigh);
        mechanics.drive(arm.getMotor());
        mechanics.render(mechanicsL[i], mechanicsR[i]);

        const double groove = std::clamp(forces.groove / (2.0 * oversampling * preload), 0.0, 1.0);
        contactSmoothed += contactSmoothing * (groove - contactSmoothed);
        contact[i] = static_cast<float>(contactSmoothed);

        // 45/45 convention: lateral motion is in phase, vertical motion (both
        // walls pushed out) comes out as L-R.
        out[1] = -out[1];
        left[i] = static_cast<float>((out[0] + crosstalk * out[1]) * outScale);
        right[i] = static_cast<float>((out[1] + crosstalk * out[0]) * outScale);
    }
}

} // namespace dustytable
