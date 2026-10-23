/* GPL-2.0-or-later. Educational preliminary sizing only. */
#ifndef QET_WIRE_PRECALC_H
#define QET_WIRE_PRECALC_H
#include <cmath>
#include <array>
#include <algorithm>
namespace QetWirePrecalc {
enum class KnownPair { VoltageCurrent, VoltagePower, CurrentPower };
enum class Supply { TwoWire, ThreePhase, DC };
struct Input {
    KnownPair known = KnownPair::VoltageCurrent;
    Supply supply = Supply::ThreePhase;
    double voltage = 220.;
    double current = 10.;
    double power = 1870.; // active input electrical power, W
    double powerFactor = 0.85;
    double lengthMeters = 20.;
    double maxDropPercent = 4.;
    double temperatureFactor = 1.; // User-declared correction; not inferred.
    double groupingFactor = 1.;
    int loadedConductors = 0; // 0: two for DC/two-wire, three for balanced 3-phase.
    double minimumSection = 1.5;
    bool mechanicalPower = false;
    double efficiency = 0.; // Required only for mechanical nameplate power.
    double protectionCurrent = 0.; // 0: protection coordination pending.
    double conventionalTripCurrent = 0.; // I2 from the device data.
    double faultCurrent = 0., clearingSeconds = 0., adiabaticK = 0.;
    double reactanceOhmPerKm = 0.; // 0: resistive approximation only.
};
struct Output {
    Input parameters;
    bool valid = false;
    double voltage = 0., current = 0., power = 0.;
    double sectionMm2 = 0., dropPercent = 0.;
    bool withinTable = false;
    double correctedAmpacity = 0., shortCircuitMinimum = 0.;
    bool protectionChecked = false, shortCircuitChecked = false;
    bool conditional = true; // Never declares whole-installation compliance.
};
inline Output calculate(const Input &p) {
    Output o;
    o.parameters=p;
    const double factor = p.supply == Supply::ThreePhase ? std::sqrt(3.0) : 1.0;
    // DC circuits have no displacement power factor. Never apply cos(phi)
    // to a 24 VDC supply output; the provided PF is ignored for DC.
    const double pf = p.supply == Supply::DC ? 1.0 : p.powerFactor;
    if (!(std::isfinite(pf) && pf > 0. && pf <= 1.
          && std::isfinite(p.lengthMeters) && p.lengthMeters > 0.
          && std::isfinite(p.maxDropPercent) && p.maxDropPercent > 0. && p.maxDropPercent <= 15.))
        return o;
    if (!(std::isfinite(p.temperatureFactor) && p.temperatureFactor > 0. &&
          p.temperatureFactor <= 2. && std::isfinite(p.groupingFactor) &&
          p.groupingFactor > 0. && p.groupingFactor <= 1. &&
          std::isfinite(p.minimumSection) && p.minimumSection > 0. &&
          std::isfinite(p.reactanceOhmPerKm) && p.reactanceOhmPerKm >= 0. &&
          (p.loadedConductors==0 || p.loadedConductors==2 || p.loadedConductors==3)))return o;
    for(double v:{p.protectionCurrent,p.conventionalTripCurrent,p.faultCurrent,
                  p.clearingSeconds,p.adiabaticK})
        if(!std::isfinite(v) || v<0.)return o;
    if(p.conventionalTripCurrent>0. && p.protectionCurrent<=0.)return o;
    const bool anyFault=p.faultCurrent>0. || p.clearingSeconds>0. || p.adiabaticK>0.;
    if(anyFault && !(p.faultCurrent>0. && p.clearingSeconds>0. &&
                     p.clearingSeconds<=5. && p.adiabaticK>0.))return o;
    double electricalPower=p.power;
    if(p.mechanicalPower) {
        if(p.known!=KnownPair::VoltagePower || !(std::isfinite(p.efficiency) &&
           p.efficiency>0. && p.efficiency<=1.))return o;
        electricalPower/=p.efficiency;
    }
    switch (p.known) {
    case KnownPair::VoltageCurrent:
        o.voltage = p.voltage; o.current = p.current;
        o.power = factor * o.voltage * o.current * pf;
        break;
    case KnownPair::VoltagePower:
        o.voltage = p.voltage; o.power = electricalPower;
        o.current = o.power / (factor * o.voltage * pf);
        break;
    case KnownPair::CurrentPower:
        o.current = p.current; o.power = electricalPower;
        o.voltage = o.power / (factor * o.current * pf);
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
    const int loaded=p.loadedConductors ? p.loadedConductors :
                     (p.supply==Supply::ThreePhase ? 3 : 2);
    const auto &limit = loaded==3 ? limit3 : limit2;
    o.protectionChecked=p.protectionCurrent>0. && p.conventionalTripCurrent>0.;
    o.shortCircuitChecked=anyFault;
    if(anyFault)o.shortCircuitMinimum=p.faultCurrent*std::sqrt(p.clearingSeconds)/p.adiabaticK;
    if(p.protectionCurrent>0. && p.protectionCurrent<o.current)return o;
    constexpr double rhoCopperHot = 0.0225; // ohm mm2 / meter
    for (unsigned i = 0; i < sections.size(); ++i) {
        const double section = sections[i];
        const double iz=limit[i]*p.temperatureFactor*p.groupingFactor;
        const double sinPhi=std::sqrt(std::max(0.,1.-pf*pf));
        const double dropV = (p.supply == Supply::ThreePhase ? std::sqrt(3.0) : 2.0)
                           * p.lengthMeters * o.current *
                           (rhoCopperHot*pf/section + p.reactanceOhmPerKm*sinPhi/1000.);
        const double pct = 100. * dropV / o.voltage;
        if (section>=p.minimumSection && o.current <= iz && pct <= p.maxDropPercent &&
            (p.protectionCurrent<=0. || p.protectionCurrent<=iz) &&
            (p.conventionalTripCurrent<=0. || p.conventionalTripCurrent<=1.45*iz) &&
            (!anyFault || section>=o.shortCircuitMinimum)) {
            o.correctedAmpacity=iz;
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
