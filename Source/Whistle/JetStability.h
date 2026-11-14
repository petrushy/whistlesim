// Linear stability of the jet leaving the windway: how fast a disturbance of angular frequency omega grows
// along the jet and how fast it travels. Spatial theory: the deflection goes as exp(i (alpha x - omega t))
// with complex wavenumber alpha (growth -Im alpha, phase speed omega / Re alpha), sinuous (flapping) mode.
//
// The jet's velocity profile is a flat core with a boundary layer on each side, as it leaves the windway:
// U(y) = U0 [tanh((h/2 - y) / 2 theta) + tanh((h/2 + y) / 2 theta)] / (2 tanh(h / 4 theta)), each shear layer
// of momentum thickness ~theta. Solved with Rayleigh's (inviscid) equation by shooting. No JUCE: used by the
// model when the geometry changes and by WhistleCheck.

#pragma once

#include <complex>
#include <vector>

namespace whistle
{

// alpha h for a disturbance at omega h / U0, with boundary layers theta / h, or NaN if no growing wave was
// found. `guess` is a starting point (alpha h); by default the infinitely thin shear-layer jet's answer.
std::complex<double> jetWavenumber (double omegaH, double thetaOverH, std::complex<double> guess = { 0.0, 0.0 });

// The same for a jet with infinitely thin shear layers (Rayleigh's top-hat jet, two vortex sheets), solved
// from its dispersion relation (omega - alpha U)^2 tanh(alpha h / 2) + omega^2 = 0. For checking.
std::complex<double> vortexSheetJetWavenumber (double omegaH);

// The jet's deflection at the edge for a sound velocity v across the window (Fletcher; de la Cuadra): the
// sound moves the jet sideways by v / (i omega), and the instability wave that keeps it attached at the flue
// grows on the way: eta = (v / (i omega)) (1 - exp(i alpha W)) (exp(-i omega t) convention). In units of
// v h / U, as a function of Omega = omega h / U, for the e^(+i omega t) convention used for filters.
std::complex<double> jetResponse (double omegaH, double windowOverH, double thetaOverH);

// The same response as an impulse response in time measured in h / U (so one set of taps serves every jet speed:
// tap k acts at a delay of k * step * h / U). Taps are the response per unit step: deflection = (h / U) sum_k
// taps[k] v(t - k step h / U). Above the jet's neutral frequency (no growing wave) and below omega h / U ~ 0.02
// (the steady flow) the response is tapered to zero. Linear theory lets short waves grow without limit (+60 dB
// at omega h / U ~ 0.9), which a real jet's do not: they saturate within the window. So above `cutoff` (omega h / U)
// the response fades out, by 1.6 x cutoff. Returns the neutral omega h / U in `neutral` if given.
std::vector<double> jetImpulseResponse (double windowOverH, double thetaOverH, double step, int taps, double cutoff, double* neutral = nullptr);


} // namespace whistle
