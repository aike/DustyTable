#pragma once

#include <cmath>

namespace dustytable {

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    double process(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.0; }

    static Biquad lowpass(double sampleRate, double cutoff, double q)
    {
        const double w = 2.0 * 3.14159265358979323846 * cutoff / sampleRate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        Biquad f;
        f.b0 = (1.0 - c) * 0.5 / a0;
        f.b1 = (1.0 - c) / a0;
        f.b2 = f.b0;
        f.a1 = -2.0 * c / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    // Band-pass with 0 dB peak gain (no DC path).
    static Biquad bandpass(double sampleRate, double centre, double q)
    {
        const double w = 2.0 * 3.14159265358979323846 * centre / sampleRate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad f;
        f.b0 = alpha / a0;
        f.b1 = 0.0;
        f.b2 = -alpha / a0;
        f.a1 = -2.0 * std::cos(w) / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    // RIAA playback curve (3180 / 318 / 75 us), unity gain at 1 kHz.
    static Biquad riaaPlayback(double sampleRate)
    {
        const double t1 = 3180e-6, t2 = 318e-6, t3 = 75e-6;
        const double k = 2.0 * sampleRate;
        const double n0 = 1.0 + t2 * k, n1 = 1.0 - t2 * k;
        const double p0 = 1.0 + t1 * k, p1 = 1.0 - t1 * k;
        const double q0 = 1.0 + t3 * k, q1 = 1.0 - t3 * k;
        const double a0 = p0 * q0;
        const double w = 2.0 * 3.14159265358979323846 * 1000.0;
        const double gain1k = std::sqrt(1.0 + w * w * t2 * t2)
                              / (std::sqrt(1.0 + w * w * t1 * t1) * std::sqrt(1.0 + w * w * t3 * t3));
        const double g = 1.0 / (a0 * gain1k);
        Biquad f;
        f.b0 = n0 * g;
        f.b1 = (n0 + n1) * g;
        f.b2 = n1 * g;
        f.a1 = (p0 * q1 + p1 * q0) / a0;
        f.a2 = p1 * q1 / a0;
        return f;
    }
};

} // namespace dustytable
