#include "../../sources/automation/wireprecalc.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace QetWirePrecalc;
static bool near(double x,double y){return std::abs(x-y)<1e-6;}
int main() {
    Input q; q.supply=Supply::TwoWire;q.voltage=220.;q.current=10.;q.powerFactor=1.;
    auto a=calculate(q);assert(a.valid && near(a.power,2200.) && a.withinTable);
    q.known=KnownPair::VoltagePower;q.power=2200.;a=calculate(q);assert(near(a.current,10.));
    q.known=KnownPair::CurrentPower;q.current=10.;a=calculate(q);assert(near(a.voltage,220.));
    q.supply=Supply::ThreePhase;q.known=KnownPair::VoltageCurrent;q.powerFactor=.85;q.voltage=380.;q.current=7.;a=calculate(q);
    assert(near(a.power,std::sqrt(3.)*380.*7.*.85));
    const double p=a.power;q.known=KnownPair::VoltagePower;q.power=p;a=calculate(q);assert(near(a.current,7.));
    q.known=KnownPair::CurrentPower;a=calculate(q);assert(near(a.voltage,380.));
    q.powerFactor=0.;a=calculate(q);assert(!a.valid);
    q.powerFactor=0.85;q.current=0.;a=calculate(q);assert(!a.valid);
    q.current=5000.;q.known=KnownPair::VoltageCurrent;a=calculate(q);assert(a.valid&&!a.withinTable);
    assert(near(peSection(4.),4.)&&near(peSection(25.),16.)&&near(peSection(50.),25.));
    q.current=10.;q.lengthMeters=-1.;a=calculate(q);assert(!a.valid);
    q.lengthMeters=0.;a=calculate(q);assert(!a.valid);
    q.lengthMeters=0.01;a=calculate(q);assert(a.valid);
    // 24 V DC is not AC: 24 V x 10 A = 240 W regardless of cos(phi).
    q.supply=Supply::DC;q.known=KnownPair::VoltageCurrent;
    q.voltage=24.;q.current=10.;q.powerFactor=.5;q.lengthMeters=5.;
    a=calculate(q);assert(a.valid && a.withinTable && near(a.power,240.));
    q.known=KnownPair::VoltagePower;q.power=240.;a=calculate(q);
    assert(a.valid && near(a.current,10.));
    q.known=KnownPair::CurrentPower;q.current=10.;a=calculate(q);
    assert(a.valid && near(a.voltage,24.));
    q.powerFactor=0.;a=calculate(q);assert(a.valid && near(a.voltage,24.));
    std::cout<<"Testes do dimensionamento: OK\n";
}
