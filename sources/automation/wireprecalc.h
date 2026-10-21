/* GPL-2.0-or-later. Educational preliminary sizing only. */
#ifndef QET_WIRE_PRECALC_H
#define QET_WIRE_PRECALC_H
#include <cmath>
#include <array>
namespace QetWirePrecalc {
enum class KnownPair { VoltageCurrent, VoltagePower, CurrentPower };
enum class Supply { TwoWire, ThreePhase };
struct Input {
    KnownPair known = KnownPair::VoltageCurrent;
    Supply supply = Supply::ThreePhase;
    double voltage = 220.;
    double current = 10.;
    double power = 1870.; // active input electrical power, W
    double powerFactor = 0.85;
    double lengthMeters = 20.;
    double maxDropPercent = 4.;
};
struct Output {
    bool valid = false;
    double voltage = 0., current = 0., power = 0.;
    double sectionMm2 = 0., dropPercent = 0.;
    bool withinTable = false;
};
inline Output calculate(const Input &p) {
    Output o;
    const double factor = p.supply == Supply::ThreePhase ? std::sqrt(3.0) : 1.0;
    if (!(std::isfinite(p.powerFactor) && p.powerFactor > 0. && p.powerFactor <= 1.
          && std::isfinite(p.lengthMeters) && p.lengthMeters > 0.
          && std::isfinite(p.maxDropPercent) && p.maxDropPercent > 0. && p.maxDropPercent <= 15.))
        return o;
    switch (p.known) {
    case KnownPair::VoltageCurrent:
        o.voltage = p.voltage; o.current = p.current;
        o.power = factor * o.voltage * o.current * p.powerFactor;
        break;
    case KnownPair::VoltagePower:
        o.voltage = p.voltage; o.power = p.power;
        o.current = o.power / (factor * o.voltage * p.powerFactor);
        break;
    case KnownPair::CurrentPower:
        o.current = p.current; o.power = p.power;
        o.voltage = o.power / (factor * o.current * p.powerFactor);
        break;
    }
    if (!(std::isfinite(o.voltage) && std::isfinite(o.current) && std::isfinite(o.power)
          && o.voltage > 0. && o.current > 0. && o.power > 0.)) return o;
    // Illustrative copper/PVC 70C, installation B1, 30C, no grouping.
    // Simplified conservative ampacities, NOT NBR 5410 certification.
    constexpr std::array<double, 12> sections =
        {1.5, 2.5, 4., 6., 10., 16., 25., 35., 50., 70., 95., 120.};
    constexpr std::array<double, 12> limit2 =
        {17., 23., 30., 38., 52., 70., 90., 112., 134., 171., 207., 238.};
    constexpr std::array<double, 12> limit3 =
        {15., 20., 27., 34., 46., 62., 80., 99., 118., 149., 179., 206.};
    const auto &limit = p.supply == Supply::ThreePhase ? limit3 : limit2;
    constexpr double rhoCopperHot = 0.0225; // ohm mm2 / meter
    for (unsigned i = 0; i < sections.size(); ++i) {
        const double section = sections[i];
        const double dropV = (p.supply == Supply::ThreePhase ? std::sqrt(3.0) : 2.0)
                           * rhoCopperHot * p.lengthMeters * o.current * p.powerFactor / section;
        const double pct = 100. * dropV / o.voltage;
        if (o.current <= limit[i] && pct <= p.maxDropPercent) {
            o.sectionMm2 = section;
            o.dropPercent = pct;
            o.withinTable = true;
            break;
        }
    }
    o.valid = true;
    return o;
}
inline double peSection(double phase) {
    if (!(phase > 0. && std::isfinite(phase))) return 0.;
    if (phase <= 16.) return phase;
    if (phase <= 35.) return 16.;
    const double target = phase / 2.;
    constexpr std::array<double, 12> sizes =
        {1.5,2.5,4.,6.,10.,16.,25.,35.,50.,70.,95.,120.};
    for (auto s : sizes) if (s >= target) return s;
    return 0.;
}
} // namespace QetWirePrecalc
#endif
