#pragma once
#include <cmath>
namespace threebody { namespace stellar {
// IAU 2012 AU and IAU 2015 B3 nominal solar constants, SI.
constexpr double auMeters = 149597870700.0;
constexpr double solarGM = 1.3271244e20;
constexpr double solarRadiusMeters = 6.957e8;
inline double timeSeconds() { return std::sqrt(auMeters * auMeters * auMeters / solarGM); }
inline double timeDays() { return timeSeconds() / 86400.; }
inline double velocityKmS() { return auMeters / timeSeconds() / 1000.; }
// Deliberately approximate near-solar main-sequence prescription, not evolution.
inline double radiusSolar(double mass) { return std::pow(mass, .8); }
inline double radiusAU(double mass) { return radiusSolar(mass) * solarRadiusMeters / auMeters; }
inline double contactAU(double m1, double m2) { return radiusAU(m1) + radiusAU(m2); }
} }
