#include "Mechanics.h"

#include <algorithm>
#include <cmath>

namespace dustytable {

namespace {

constexpr double airDensity = 1.2;
constexpr double soundSpeed = 343.0;
constexpr double listeningDistance = 0.7;  // m, listener in front of the turntable
constexpr double pascalToFullScale = 0.4;  // calibrated: the arm-rest clamp snapping shut peaks near -20 dBFS

// Materials (Young's modulus Pa, density kg/m^3)
constexpr double aluminiumE = 70e9, aluminiumRho = 2700.0;
constexpr double absE = 2.3e9, absRho = 1050.0;
constexpr double steelE = 200e9, steelRho = 7850.0;
constexpr double mdfE = 3.5e9, mdfRho = 750.0;
constexpr double bronzeE = 110e9, bronzeRho = 8800.0; // phosphor bronze switch leaf
constexpr double pomE = 2.8e9, pomRho = 1410.0;       // acetal (cam, follower lever)
constexpr double grommetStiffness = 2.4e4; // N/m, rubber motor mounts

// Push button: fingertip pulp, key with its plunger, snap dome (forces in N, travel in m).
constexpr double keyMass = 0.4e-3;
constexpr double fingerStiffness = 2500.0, fingerDamping = 2.0;
constexpr double fingerPress = 0.07, fingerRelease = 0.06; // m/s
constexpr double fingerHoldForce = 3.0;
constexpr double domePreload = 0.1, domePeak = 0.5, domeValley = 0.18;
constexpr double domePeakTravel = 0.12e-3, domeValleyTravel = 0.22e-3, keyBottom = 0.25e-3;

// Cam follower lever: effective mass at the follower pin, spring preload and rate.
constexpr double leverMass = 4e-3, leverPreload = 0.1, leverSpring = 30.0;
constexpr double camRadius = 0.012;   // m, radius of the lift track
constexpr int roughnessPoints = 16384; // per cam revolution (4.6 um apart)

// Arm swinging into the arm-rest stop: moment of inertia about the pivot seen at the stop.
constexpr double armStopMass = 0.034;
constexpr double clutchForce = 0.11; // N, slipping clutch pressing the arm on

void panGains(double pan, double& l, double& r)
{
    const double a = (pan + 1.0) * 0.25 * pi;
    l = std::cos(a);
    r = std::sin(a);
}

// A part much smaller than the wavelength radiates poorly (both faces work
// against each other); above c / (pi d) it radiates fully.
double radiationCorner(double smallestDimension) { return soundSpeed / (pi * smallestDimension); }

// Hertz constant (N/m^1.5) of a sphere of radius r on a flat, effective modulus e.
double hertz(double effectiveModulus, double r) { return 4.0 / 3.0 * effectiveModulus * std::sqrt(r); }

// Hunt-Crossley contact force for penetration d and approach speed v (never pulls).
double huntCrossley(double k, double alpha, double d, double v)
{
    if (d <= 0.0) return 0.0;
    return std::max(0.0, k * d * std::sqrt(d) * (1.0 + alpha * v));
}

// Clamped-free beam carrying a tip mass (ratio mu = tip mass / beam mass):
// 1 + cos b cosh b + mu b (cos b sinh b - sin b cosh b) = 0.
double tipMassEquation(double b, double mu)
{
    return 1.0 + std::cos(b) * std::cosh(b) + mu * b * (std::cos(b) * std::sinh(b) - std::sin(b) * std::cosh(b));
}

double modeShape(double b, double sigma, double xi)
{
    return std::cosh(b * xi) - std::cos(b * xi) - sigma * (std::sinh(b * xi) - std::sin(b * xi));
}

// Upward force of a tactile snap dome against travel: it stiffens to a peak,
// collapses (negative rate) and bottoms out.
double domeForce(double x)
{
    if (x <= 0.0) return domePreload;
    if (x < domePeakTravel) return domePreload + (domePeak - domePreload) * x / domePeakTravel;
    if (x < domeValleyTravel)
        return domePeak - (domePeak - domeValley) * (x - domePeakTravel) / (domeValleyTravel - domePeakTravel);
    return domeValley + 5000.0 * (x - domeValleyTravel);
}

} // namespace

void Mechanics::prepare(double newSampleRate, int newOversampling)
{
    sampleRate = newSampleRate;
    oversampling = newOversampling;
    dtHigh = 1.0 / (sampleRate * oversampling);
    dcCoeff = 1.0 - std::exp(-twoPi * 20.0 / sampleRate);
    contactBaseline = 1.0 - std::exp(-1.0 / (0.03 * sampleRate));

    for (auto& p : parts) p.modes.clear();

    // Arm tube: aluminium, 230 mm, 8 mm outer diameter, 0.5 mm wall, held at the
    // pivot, carrying headshell and cartridge (15 g) at its end. The lifter and
    // the rest touch it near the headshell.
    {
        const double d0 = 8e-3, d1 = 7e-3, length = 0.23;
        const double inertia = pi / 64.0 * (std::pow(d0, 4) - std::pow(d1, 4));
        const double area = pi / 4.0 * (d0 * d0 - d1 * d1);
        addBeam(ArmTube, aluminiumE, aluminiumRho, inertia, area, length, 0.015, 0.8, 40.0, d0 * length,
            radiationCorner(d0), 4);
    }
    // Plinth: MDF plate 450 x 350 x 15 mm, simply supported, driven off-centre.
    addPlate(Base, mdfE, mdfRho, 0.015, 0.45, 0.35, 15.0, 300.0, 6, 6, 0.37, 0.29);
    // Arm-rest clamp: spring steel strip, 15 mm, 4 x 0.5 mm, with a 0.2 g hook.
    {
        const double b = 4e-3, h = 0.5e-3, length = 0.015;
        addBeam(Clamp, steelE, steelRho, b * h * h * h / 12.0, b * h, length, 0.2e-3, 1.0, 150.0, b * length,
            radiationCorner(b), 2);
    }
    // Lifter post and arm-rest post: steel rods, 6 mm x 30 mm, topped by the
    // lifter bar (4 g) and the rest cradle with its stop (3 g).
    {
        const double d = 6e-3, length = 0.03;
        const double inertia = pi / 64.0 * d * d * d * d, area = pi / 4.0 * d * d;
        addBeam(LifterPost, steelE, steelRho, inertia, area, length, 4e-3, 1.0, 80.0, d * length,
            radiationCorner(d), 2);
        addBeam(RestPost, steelE, steelRho, inertia, area, length, 3e-3, 1.0, 80.0, d * length,
            radiationCorner(d), 2);
    }
    // Cam motor: 20 mm steel can, 18 g, on rubber grommets (bounce mode), plus
    // the n = 2, 3 flexural modes of its 0.4 mm shell.
    {
        const double radius = 0.01, wall = 0.4e-3, mass = 0.018;
        const double corner = radiationCorner(2.0 * radius);
        const double canArea = 2.0 * radius * 0.025;
        const double bounce = std::sqrt(grommetStiffness / mass) / twoPi;
        addMode(Motor, bounce, mass, 4.0, canArea, corner);
        grommetFilter = Biquad::lowpass(sampleRate, bounce, 4.0);
        const double shell = wall / (twoPi * radius * radius) * std::sqrt(steelE / (12.0 * steelRho * (1.0 - 0.09)));
        for (int n = 2; n <= 3; ++n)
            addMode(Motor, shell * n * (n * n - 1.0) / std::sqrt(n * n + 1.0), 0.004, 60.0, canArea, corner);
    }
    // Gearbox cover: ABS plate 70 x 50 x 2 mm.
    addPlate(Gearbox, absE, absRho, 2e-3, 0.07, 0.05, 20.0, radiationCorner(0.05), 3, 3, 0.4, 0.35);
    // Button key: ABS tongue moulded into the front panel, 16 x 8 x 1.6 mm,
    // carrying its 0.3 g cap and plunger; the plunger presses at its tip.
    {
        const double b = 8e-3, h = 1.6e-3, length = 0.016;
        addBeam(Key, absE, absRho, b * h * h * h / 12.0, b * h, length, 0.3e-3, 1.0, 30.0, b * length,
            radiationCorner(b), 3);
    }
    // Front skirt of the plinth carrying the buttons: ABS 300 x 60 x 3 mm.
    addPlate(Panel, absE, absRho, 3e-3, 0.30, 0.06, 25.0, radiationCorner(0.06), 6, 2, 0.75, 0.35);
    // Limit switch leaf: phosphor bronze, 15 x 3 x 0.2 mm, 0.05 g contact rivet.
    {
        const double b = 3e-3, h = 0.2e-3, length = 0.015;
        addBeam(Leaf, bronzeE, bronzeRho, b * h * h * h / 12.0, b * h, length, 0.05e-3, 1.0, 200.0, b * length,
            radiationCorner(b), 2);
    }
    // Cam follower lever: moulded POM, 40 x 6 x 3 mm, with its follower pin
    // and the lifter-rod seat (1.5 g) at the end.
    {
        const double b = 6e-3, h = 3e-3, length = 0.04;
        addBeam(Lever, pomE, pomRho, b * h * h * h / 12.0, b * h, length, 1.5e-3, 1.0, 25.0, b * length,
            radiationCorner(b), 2);
    }

    // The arm and its parts sit to the right of the platter, the buttons at the front right.
    panGains(0.45, parts[ArmTube].gainL, parts[ArmTube].gainR);
    panGains(0.15, parts[Base].gainL, parts[Base].gainR);
    panGains(0.7, parts[Clamp].gainL, parts[Clamp].gainR);
    panGains(0.5, parts[LifterPost].gainL, parts[LifterPost].gainR);
    panGains(0.7, parts[RestPost].gainL, parts[RestPost].gainR);
    panGains(0.55, parts[Motor].gainL, parts[Motor].gainR);
    panGains(0.55, parts[Gearbox].gainL, parts[Gearbox].gainR);
    panGains(0.3, parts[Key].gainL, parts[Key].gainR);
    panGains(0.1, parts[Panel].gainL, parts[Panel].gainR);
    panGains(0.55, parts[Leaf].gainL, parts[Leaf].gainR);
    panGains(0.55, parts[Lever].gainL, parts[Lever].gainR);

    // Moulded parts: every tooth and every stretch of the cam track has its own error.
    Rng shape(1729);
    for (auto& e : wheelError) e = shape.gauss();
    for (auto& e : pinionError) e = shape.gauss();
    for (auto& e : camGearError) e = shape.gauss();
    camRoughness.assign(roughnessPoints, 0.0);
    {
        // Surface texture with a correlation length of a few tens of microns.
        double a = 0.0, b = 0.0, sum = 0.0;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < roughnessPoints; ++i) {
                a += 0.35 * (shape.gauss() - a);
                b += 0.35 * (a - b);
                camRoughness[static_cast<size_t>(i)] = b; // second pass overwrites the start-up transient
            }
        for (double h : camRoughness) sum += h * h;
        const double scale = 0.05e-6 / std::sqrt(sum / roughnessPoints); // 0.05 um rms
        for (double& h : camRoughness) h *= scale;
    }

    reset();
}

void Mechanics::reset()
{
    for (auto& p : parts)
        for (auto& m : p.modes) m.re = m.im = 0.0;
    lastVelocity.fill(0.0);
    baseline = { 0.0, 0.0 };
    baselinePrimed = false;
    for (auto& i : impactors) i.active = false;
    for (auto& b : buttons) b.active = false;
    follower = Follower {};
    leverTarget = leverNominal;
    trackValid = false;
    pendingContact = { 0.0, 0.0 };
    grommetFilter.reset();
    dcL = dcR = 0.0;
}

void Mechanics::addBeam(Part part, double youngs, double density, double inertia, double area, double length,
    double tipMass, double drivePoint, double q, double radiatingArea, double corner, int count)
{
    const double c = std::sqrt(youngs * inertia / (density * area));
    const double beamMass = density * area * length;
    const double mu = tipMass / beamMass;

    int found = 0;
    double previous = tipMassEquation(0.05, mu);
    for (double b = 0.06; b < 30.0 && found < count; b += 0.01) {
        const double value = tipMassEquation(b, mu);
        if ((value > 0.0) != (previous > 0.0)) {
            double lo = b - 0.01, hi = b;
            for (int it = 0; it < 50; ++it) {
                const double mid = 0.5 * (lo + hi);
                if ((tipMassEquation(mid, mu) > 0.0) == (tipMassEquation(lo, mu) > 0.0))
                    lo = mid;
                else
                    hi = mid;
            }
            const double root = 0.5 * (lo + hi);
            const double sigma = (std::cosh(root) + std::cos(root)) / (std::sinh(root) + std::sin(root));
            // Modal mass seen at the drive point.
            double integral = 0.0;
            constexpr int steps = 200;
            for (int k = 0; k < steps; ++k) {
                const double phi = modeShape(root, sigma, (k + 0.5) / steps);
                integral += phi * phi / steps;
            }
            const double tip = modeShape(root, sigma, 1.0);
            const double atDrive = modeShape(root, sigma, drivePoint);
            const double modalMass = (beamMass * integral + tipMass * tip * tip) / std::max(1e-6, atDrive * atDrive);
            addMode(part, root * root / (twoPi * length * length) * c, modalMass, q, radiatingArea, corner);
            ++found;
        }
        previous = value;
    }
}

void Mechanics::addPlate(Part part, double youngs, double density, double thickness, double a, double b, double q,
    double radiationCorner, int maxM, int maxN, double x0, double y0)
{
    const double rigidity = youngs * thickness * thickness * thickness / (12.0 * (1.0 - 0.3 * 0.3));
    const double mass = density * thickness * a * b;
    const double c = 0.5 * pi * std::sqrt(rigidity / (density * thickness));
    for (int m = 1; m <= maxM; ++m)
        for (int n = 1; n <= maxN; ++n) {
            // Modal mass M/4, seen through the mode shape at the drive point; a
            // drive near a nodal line hardly excites that mode.
            const double shape = std::sin(m * pi * x0) * std::sin(n * pi * y0);
            if (std::abs(shape) < 0.1) continue;
            const double f = c * ((m / a) * (m / a) + (n / b) * (n / b));
            addMode(part, f, 0.25 * mass / (shape * shape), q, a * b, radiationCorner);
        }
}

void Mechanics::addMode(Part part, double frequency, double modalMass, double q, double radiatingArea,
    double radiationCorner)
{
    if (frequency > 0.45 * sampleRate) return;
    Mode m;
    m.omega = twoPi * frequency;
    m.nominal = m.omega;
    m.q = q;
    const double decay = std::exp(-pi * frequency / (q * sampleRate));
    m.rotRe = decay * std::cos(m.omega / sampleRate);
    m.rotIm = decay * std::sin(m.omega / sampleRate);
    m.input = 1.0 / modalMass;
    const double ratio = radiationCorner / frequency;
    const double efficiency = 1.0 / (1.0 + ratio * ratio);
    // Far-field pressure is proportional to the surface acceleration; the
    // velocity is weighted here and differentiated after summing.
    m.output = airDensity * radiatingArea * efficiency / (twoPi * listeningDistance);
    parts[part].modes.push_back(m);
}

void Mechanics::startImpact(const ImpactSpec& spec, double delaySeconds)
{
    for (auto& i : impactors) {
        if (i.active) continue;
        i.active = true;
        i.delay = static_cast<int>(delaySeconds * sampleRate);
        i.remaining = static_cast<int>(spec.seconds * sampleRate);
        i.mass = spec.mass;
        i.stiffness = spec.stiffness;
        i.damping = spec.damping;
        i.push = spec.push;
        i.penetration = 0.0;
        i.release = 0.0;
        i.velocity = spec.velocity * (0.75 + 0.5 * rng.uniform());
        i.target = vary(spec.target);
        return;
    }
}

void Mechanics::trigger(Gesture gesture)
{
    // Masses (kg), approach velocities (m/s), Hertz constants, Hunt-Crossley
    // damping (s/m), constant push (N) and how long to follow the contact (s).
    const Excitation clampStop { { { { Clamp, 1.0 }, { RestPost, 0.5 }, { Base, 1.0 }, { Base, 0.0 } } }, 3 };
    const Excitation clampOnArm { { { { Clamp, 1.0 }, { ArmTube, 0.6 }, { Base, 1.0 }, { Base, 0.0 } } }, 3 };
    const Excitation leaf { { { { Leaf, 0.05 }, { Gearbox, 0.4 }, { Base, 1.0 }, { Base, 0.0 } } }, 3 };
    const Excitation leafDrop { { { { Leaf, 0.02 }, { Gearbox, 0.1 }, { Base, 1.0 }, { Base, 0.0 } } }, 3 };
    // The hook is PVC-sleeved (E* about 30 MPa, tip radius 1 mm) and swings
    // back onto a rubber bumper (E* about 8 MPa, 2 mm) when opened.
    const double sleeve = hertz(30e6, 1e-3);
    const double stop = hertz(8e6, 2e-3);

    switch (gesture) {
    case Gesture::ClampOpen:
        startImpact({ clampMass, 0.4, stop, 0.3, 0.0, 0.1, clampStop }, 0.0);
        break;
    case Gesture::ClampClose:
        startImpact({ clampMass, 0.5, sleeve, 0.5, 0.0, 0.1, clampOnArm }, 0.0);
        startImpact({ clampMass, 0.2, sleeve, 0.5, 0.0, 0.1, clampOnArm }, 0.008 + 0.007 * rng.uniform()); // latch settles
        break;
    case Gesture::PressStart: press(0); break;
    case Gesture::PressStop: press(1); break;
    case Gesture::SwitchIn:
        // The switch actuator (leaf and its moulded follower, 0.5 g) drops off the
        // edge into the notch; the follower has an elastomer tip (E* about 20 MPa)
        // that lands softly on the notch floor.
        startImpact({ 0.5e-3, 0.006, hertz(20e6, 1e-3), 1.0, 0.1, 0.05, leafDrop }, 0.0);
        break;
    case Gesture::SwitchOut:
        // Leaving the notch: the contact rivets meet.
        startImpact({ 0.05e-3, 0.012, hertz(60e9, 1e-3), 0.3, 0.1, 0.03, leaf }, 0.0);
        break;
    }
}

void Mechanics::armStop(double speed)
{
    // Aluminium tube against the PVC-capped stop of the rest (tube radius 4 mm);
    // the slipping clutch keeps pushing, so the arm bounces back onto it.
    const Excitation restStop { { { { ArmTube, 1.0 }, { RestPost, 1.0 }, { Base, 0.5 }, { Base, 0.0 } } }, 3 };
    startImpact({ armStopMass, speed, hertz(0.8e9, 4e-3), 12.0, clutchForce, 0.4, restStop }, 0.0);
}

void Mechanics::press(int which)
{
    Button& b = buttons[static_cast<size_t>(which)];
    b.active = true;
    b.time = 0.0;
    b.hold = 0.25 + 0.35 * rng.uniform(); // how long the finger stays on the key
    b.x = b.v = 0.0;
    b.finger = -0.3e-3 - 0.3e-3 * rng.uniform(); // fingertip just above the key
    b.fingerSpeed = fingerPress * (0.8 + 0.4 * rng.uniform());
    b.domeScale = 0.85 + 0.3 * rng.uniform();
    b.target = vary({ { { { Key, 0.5 }, { Panel, 0.15 }, { Base, 0.3 }, { Base, 0.0 } } }, 3 });
}

// One sub-step of a button; returns the force the key puts into the deck (N, down positive).
double Mechanics::stepButton(Button& b)
{
    b.time += dtHigh;
    // The finger pushes until it bears its holding force, waits, then lifts away.
    double fingerVelocity = 0.0;
    if (b.time < b.hold) {
        if (b.finger < keyBottom + fingerHoldForce / fingerStiffness) fingerVelocity = b.fingerSpeed;
    } else if (b.finger > -1.5e-3) {
        fingerVelocity = -fingerRelease;
    }
    b.finger += fingerVelocity * dtHigh;

    const double fingerForce = b.finger > b.x
        ? std::max(0.0, fingerStiffness * (b.finger - b.x) + fingerDamping * (fingerVelocity - b.v))
        : 0.0;
    const double dome = b.domeScale * domeForce(b.x);
    const double bottom = huntCrossley(hertz(0.8e9, 0.5e-3), 0.6, b.x - keyBottom, b.v);
    const double top = huntCrossley(hertz(0.3e9, 1e-3), 2.0, -b.x, -b.v);
    b.v += (fingerForce - dome - bottom + top) / keyMass * dtHigh;
    b.x += b.v * dtHigh;

    if (b.time > b.hold + 0.25 && b.finger <= -1.5e-3) b.active = false;
    return dome + bottom - top;
}

// One sub-step of the follower lever on the cam track; returns the net force
// it puts into the gearbox frame (N).
double Mechanics::stepFollower(double track, double trackSpeed)
{
    const double contact = huntCrossley(hertz(0.3e9, 2e-3), 3.0, track - follower.y, trackSpeed - follower.v);
    if (contact > 0.0) {
        follower.contactTime += dtHigh;
    } else if (follower.contactTime > 0.05 && follower.y - track > 2e-5) {
        // The lever leaves the track at the step: this landing will be a little different from the last.
        follower.contactTime = 0.0;
        leverTarget = vary(leverNominal);
    }
    const double spring = leverPreload + leverSpring * follower.y;
    follower.v += (contact - spring) / leverMass * dtHigh;
    follower.y += follower.v * dtHigh;
    return contact - spring;
}

double Mechanics::roughness(double camPhase) const
{
    const double p = (camPhase - std::floor(camPhase)) * roughnessPoints;
    const int i = static_cast<int>(p);
    const double f = p - i;
    const double a = camRoughness[static_cast<size_t>(i % roughnessPoints)];
    const double b = camRoughness[static_cast<size_t>((i + 1) % roughnessPoints)];
    return a + (b - a) * f;
}

void Mechanics::follow(double trackHeight, double camPhase)
{
    pendingTrack = trackHeight + roughness(camPhase);
    pendingPhase = camPhase;
    if (!trackValid) {
        // Sitting on the track under its spring load.
        follower.y = pendingTrack - std::pow(leverPreload / hertz(0.3e9, 2e-3), 2.0 / 3.0);
        follower.v = 0.0;
        follower.lastTrack = pendingTrack;
        trackValid = true;
    }
}

void Mechanics::addImpulse(Contact contact, double impulse)
{
    pendingContact[contact == Contact::Lifter ? 0 : 1] += impulse;
}

void Mechanics::drive(const CamMotor& motor)
{
    const double dt = 1.0 / sampleRate;
    const Excitation mount { { { { Motor, 1.0 }, { Gearbox, 0.0 }, { Base, 0.0 }, { Base, 0.0 } } }, 1 };
    const Excitation grommets { { { { Base, 1.0 }, { Gearbox, 0.0 }, { Base, 0.0 }, { Base, 0.0 } } }, 1 };
    const Excitation mesh { { { { Gearbox, 1.0 }, { Base, 1.0 }, { Motor, 0.0 }, { Base, 0.0 } } }, 2 };
    // Each tooth takes up its load quickly as it enters the mesh (moulded teeth
    // with profile errors: within a twentieth of the mesh period) and sheds it
    // gradually as it rolls out. How hard it bites depends on that tooth's own error.
    // The load is zero between teeth, so a tooth's own error never makes it jump.
    const auto toothError = [](double x, double error) {
        const double f = x - std::floor(x);
        const double load = f < 0.05 ? 0.5 - 0.5 * std::cos(pi * f / 0.05) : 1.0 - (f - 0.05) / 0.95;
        return load * std::max(0.0, 1.0 + 0.6 * error) - 0.5;
    };
    const auto tooth = [](double x, int teeth) {
        const int i = static_cast<int>(std::floor(x)) % teeth;
        return static_cast<size_t>(i < 0 ? i + teeth : i);
    };

    // Reaction of the stator to the rotor torque and the brush friction, taken
    // up at the 10 mm mounting radius, plus the rotor unbalance of an
    // inexpensive motor (1.5 g mm).
    const double w = motor.speed();
    const double reaction = -(motor.electromagneticTorque() + motor.brushFriction()) / 0.01;
    const double unbalance = 1.5e-6 * w * w * std::cos(motor.motorAngle());
    const double motorForce = reaction + unbalance;
    excite(mount, motorForce * dt);
    // The rubber grommets pass the motor's shaking to the chassis only below
    // their bounce resonance (second-order roll-off above it).
    excite(grommets, grommetFilter.process(motorForce) * dt);

    // Tooth loads fluctuate with each tooth engagement (transmission error).
    const double load = motor.gearTorque();
    const double wormForce = load / 3e-3;                          // 3 mm worm radius
    const double spurForce = load * CamMotor::wormRatio * 0.4 / 4e-3; // 40 % worm efficiency, 4 mm pinion
    const double worm = toothError(motor.wormMesh(), wheelError[tooth(motor.wormMesh(), CamMotor::wheelTeeth)]);
    const double spur = toothError(motor.spurMesh(),
        pinionError[tooth(motor.spurMesh(), CamMotor::pinionTeeth)] + camGearError[tooth(motor.spurMesh(), CamMotor::camTeeth)]);
    excite(mesh, 2.2 * (wormForce * worm + spurForce * spur) * dt);
}

void Mechanics::excite(const Excitation& e, double impulse)
{
    for (int k = 0; k < e.count; ++k)
        for (auto& m : parts[e.to[k].part].modes) m.re += m.input * e.to[k].gain * impulse;
}

Mechanics::Excitation Mechanics::vary(const Excitation& e)
{
    Excitation out = e;
    for (int k = 0; k < out.count; ++k) {
        out.to[k].gain *= 0.8 + 0.4 * rng.uniform();              // about +-2 dB
        retune(out.to[k].part, std::exp(0.02 * rng.gauss()));    // about +-35 cents
    }
    return out;
}

void Mechanics::retune(Part part, double factor)
{
    for (auto& m : parts[part].modes) {
        const double w = m.nominal * factor;
        const double decay = std::exp(-w / (2.0 * m.q * sampleRate));
        m.rotRe = decay * std::cos(w / sampleRate);
        m.rotIm = decay * std::sin(w / sampleRate);
        m.omega = w;
    }
}

void Mechanics::render(float& left, float& right)
{
    const Excitation lifterPath { { { { ArmTube, 1.0 }, { LifterPost, 0.5 }, { Base, 1.0 }, { Base, 0.0 } } }, 3 };
    const Excitation cradlePath { { { { ArmTube, 1.0 }, { RestPost, 0.6 }, { Base, 1.0 }, { Base, 0.0 } } }, 3 };
    // A steadily carried weight only deflects the parts; what radiates is the
    // change in contact force, so the slow part of it is removed.
    for (int k = 0; k < 2; ++k) {
        if (!baselinePrimed) baseline[k] = pendingContact[k]; // the weight was already resting there
        baseline[k] += contactBaseline * (pendingContact[k] - baseline[k]);
        pendingContact[k] -= baseline[k];
    }
    baselinePrimed = true;
    excite(lifterPath, pendingContact[0]);
    excite(cradlePath, pendingContact[1]);
    pendingContact = { 0.0, 0.0 };

    // Follower lever on the cam track (the track height is interpolated across the sub-steps).
    if (trackValid) {
        const double trackSpeed = (pendingTrack - follower.lastTrack) * sampleRate;
        double impulse = 0.0;
        for (int k = 0; k < oversampling; ++k) {
            const double track = follower.lastTrack + (pendingTrack - follower.lastTrack) * (k + 1) / oversampling;
            impulse += stepFollower(track, trackSpeed) * dtHigh;
        }
        follower.lastTrack = pendingTrack;
        excite(leverTarget, impulse);
    }

    for (auto& b : buttons) {
        if (!b.active) continue;
        double impulse = 0.0;
        for (int k = 0; k < oversampling; ++k) impulse += stepButton(b) * dtHigh;
        excite(b.target, impulse);
    }

    for (auto& i : impactors) {
        if (!i.active) continue;
        if (i.delay > 0) {
            --i.delay;
            continue;
        }
        double impulse = 0.0;
        for (int k = 0; k < oversampling; ++k) {
            const double force = huntCrossley(i.stiffness, i.damping, i.penetration, i.velocity);
            i.velocity += (i.push - force) / i.mass * dtHigh;
            i.penetration += i.velocity * dtHigh;
            // The push comes from inside the deck (spring, clutch), so only the
            // part of the contact force beyond it shakes the frame. Before the
            // throw, the same force was carried by whatever released the part (a
            // cam edge it rolled off, the clutch taking up): that support gives
            // way over a few milliseconds, not at once.
            i.release = std::min(1.0, i.release + dtHigh / 0.003);
            impulse += (force - i.push * i.release) * dtHigh;
        }
        excite(i.target, impulse);
        if (--i.remaining <= 0 || (i.push <= 0.0 && i.penetration < 0.0 && i.velocity < 0.0))
            i.active = false; // bounced off, or settled against its stop
    }

    double l = 0.0, r = 0.0;
    for (size_t k = 0; k < parts.size(); ++k) {
        auto& p = parts[k];
        double velocity = 0.0;
        for (auto& m : p.modes) {
            const double re = m.re * m.rotRe - m.im * m.rotIm;
            const double im = m.re * m.rotIm + m.im * m.rotRe;
            m.re = re; // modal velocity
            m.im = im;
            velocity += m.output * re;
        }
        const double pressure = (velocity - lastVelocity[k]) * sampleRate;
        lastVelocity[k] = velocity;
        l += pressure * p.gainL;
        r += pressure * p.gainR;
    }
    l *= pascalToFullScale;
    r *= pascalToFullScale;
    // The static weight carried by the lifter and rest is not sound.
    dcL += dcCoeff * (l - dcL);
    dcR += dcCoeff * (r - dcR);
    left = static_cast<float>(l - dcL);
    right = static_cast<float>(r - dcR);
}

} // namespace dustytable
