#pragma once

#include <algorithm>
#include <cmath>

namespace motion::paths
{
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kTwoPi = kPi * 2.0;

struct Point
{
    double x = 0.0;
    double y = 0.0;
};

inline double clampUnit(double value) noexcept
{
    return std::clamp(value, 0.0, 1.0);
}

inline double wrapPhase(double phase) noexcept
{
    phase = std::fmod(phase, kTwoPi);
    return phase < 0.0 ? phase + kTwoPi : phase;
}

inline double orbitRadius(double radiusControl) noexcept
{
    return 0.22 + clampUnit(radiusControl) * 0.68;
}

inline double orbitAspect(double ellipticityControl) noexcept
{
    return 1.0 - clampUnit(ellipticityControl) * 0.65;
}

inline double orbitRateHz(double speedControl) noexcept
{
    // Make speed a temporal control only. The old force-driven orbit changed shape
    // with speed because centrifugal error grew faster than the radial servo could
    // correct it. A deterministic phase rate keeps one geometric orbit at all speeds.
    return 0.05 + clampUnit(speedControl) * 0.45;
}

inline Point orbitPoint(double radiusControl,
                        double ellipticityControl,
                        double rotationControl,
                        double phase) noexcept
{
    const double radius = orbitRadius(radiusControl);
    const double aspect = orbitAspect(ellipticityControl);
    const double rotation = clampUnit(rotationControl) * kPi;
    const double co = std::cos(rotation);
    const double so = std::sin(rotation);
    const double localX = radius * std::cos(phase);
    const double localY = radius * aspect * std::sin(phase);
    return { co * localX - so * localY,
             so * localX + co * localY };
}

inline Point orbitVelocity(double radiusControl,
                           double ellipticityControl,
                           double rotationControl,
                           double speedControl,
                           double phase) noexcept
{
    const double radius = orbitRadius(radiusControl);
    const double aspect = orbitAspect(ellipticityControl);
    const double rotation = clampUnit(rotationControl) * kPi;
    const double omega = kTwoPi * orbitRateHz(speedControl);
    const double co = std::cos(rotation);
    const double so = std::sin(rotation);
    const double localVX = -radius * std::sin(phase) * omega;
    const double localVY = radius * aspect * std::cos(phase) * omega;
    return { co * localVX - so * localVY,
             so * localVX + co * localVY };
}

inline double lissajousRateHz(double rateControl) noexcept
{
    return 0.08 + clampUnit(rateControl) * 1.35;
}

inline double lissajousRatio(double ratioControl) noexcept
{
    return 1.0 + clampUnit(ratioControl) * 2.5;
}

inline Point lissajousPoint(double ratioControl,
                            double phaseControl,
                            double rotationControl,
                            double basePhase) noexcept
{
    // 0.68 keeps the rotated two-axis figure inside the -1..1 world without the
    // old boundary clamping, which was another source of preview/body divergence.
    constexpr double amplitude = 0.68;
    const double ratio = lissajousRatio(ratioControl);
    const double phaseOffset = clampUnit(phaseControl) * kTwoPi;
    const double rotation = (clampUnit(rotationControl) - 0.5) * kPi;
    const double co = std::cos(rotation);
    const double so = std::sin(rotation);
    const double rawX = amplitude * std::sin(basePhase);
    const double rawY = amplitude * std::sin(basePhase * ratio + phaseOffset);
    return { co * rawX - so * rawY,
             so * rawX + co * rawY };
}

inline Point lissajousVelocity(double rateControl,
                               double ratioControl,
                               double phaseControl,
                               double rotationControl,
                               double basePhase) noexcept
{
    constexpr double amplitude = 0.68;
    const double ratio = lissajousRatio(ratioControl);
    const double phaseOffset = clampUnit(phaseControl) * kTwoPi;
    const double rotation = (clampUnit(rotationControl) - 0.5) * kPi;
    const double omega = kTwoPi * lissajousRateHz(rateControl);
    const double co = std::cos(rotation);
    const double so = std::sin(rotation);
    const double rawVX = amplitude * std::cos(basePhase) * omega;
    const double rawVY = amplitude * std::cos(basePhase * ratio + phaseOffset) * omega * ratio;
    return { co * rawVX - so * rawVY,
             so * rawVX + co * rawVY };
}
} // namespace motion::paths
