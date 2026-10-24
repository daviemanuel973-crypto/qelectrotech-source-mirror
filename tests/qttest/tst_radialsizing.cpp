// GPL-2.0-or-later. Analytic radial-network examples; ratings are test data.
#include "automation/radialsizing.h"
#include <QtTest>
using namespace RadialSizing;
static Profile sampleProfile() {
    Profile p;p.material="Cu";p.insulation="PVC";p.method="B1";
    p.reference="Dados didáticos de teste; não são catálogo de fabricante";
    p.resistivity=.0225;p.k=115;p.minimumPhase=1.5;p.minimumNeutral=1.5;p.minimumPE=1.5;p.confirmed=true;p.harmonicFactor=1;
    p.table={{1.5,17,15,12},{2.5,23,20,16},{4,30,27,21.6},{6,38,34,27.2},
        {10,52,46,36.8},{16,70,62,49.6},{25,90,80,64},{35,112,99,79.2},
        {50,134,118,94.4},{70,171,149,119.2},{95,207,179,143.2},{120,238,206,164.8}};
    return p;
}
static Protection protective(const QString &id,double in=0) {
    Protection p;p.id=id;p.curve="C";p.i2Multiple=1.45;p.fixedIn=in;p.breakingA=6000;p.maxDisconnect=.4;return p;
}
static Edge wire(const QString &id,int a,int b,const QString &cable,double length=10) {
    Edge e;e.id=id;e.a=a;e.b=b;e.cable=cable;e.length=length;
    e.temperatureFactor=1;e.groupingFactor=1;e.loaded=2;return e;
}
static Network example() {
    Network n;n.nodes=12;n.supply=Supply::SinglePhase;n.voltage=230;n.frequencyHz=60;n.maxDrop=4;n.maxStartDrop=15;
    n.sourceR=.5;n.sourceX=0;n.sourceFaultMax=500;n.minVoltageFactor=.95;n.profile=sampleProfile();
    n.roots[L1]=0;n.roots[N]=1;n.roots[PE]=2;n.stateConfirmed=true;n.sourceProtection="Q0";
    n.protections.insert("Q0",protective("Q0"));
    // Three physical cores per cable. The trunk feeds 8 A + 12 A;
    // separate outgoing protective devices define the branch boundaries.
    n.edges={wire("F-L",0,3,"F"),wire("F-N",1,4,"F"),wire("F-PE",2,5,"F"),
        wire("A-L",6,8,"A",5),wire("A-N",4,9,"A",5),wire("A-PE",5,10,"A",5),
        wire("B-L",7,11,"B",5),wire("B-N",4,9,"B",5),wire("B-PE",5,10,"B",5)};
    // Give each branch its own N/PE endpoints; sharing the node would
    // create a parallel return that the solver correctly rejects.
    n.nodes=16;n.edges[7].b=12;n.edges[8].b=13;
    Edge qa;qa.id="pole-A";qa.a=3;qa.b=6;qa.kind=EdgeKind::Device;qa.protection="Q1";
    Edge qb=qa;qb.id="pole-B";qb.b=7;qb.protection="Q2";n.edges<<qa<<qb;
    n.protections.insert("Q1",protective("Q1"));n.protections.insert("Q2",protective("Q2"));
    n.selectivity={{"Q0","Q1","Associação didática verificada no teste",500},
        {"Q0","Q2","Associação didática verificada no teste",500}};
    Load a;a.id="H1";a.ports={8,9};a.pe=10;a.voltage=230;a.current=8;a.pf=1;a.harmonic3=0;
    Load b=a;b.id="H2";b.ports={11,12};b.pe=13;b.current=12;n.loads={a,b};return n;
}
static WireResult get(const Result &r,const QString &id) {for(const auto &w:r.wires)if(w.id==id)return w;return {};}
class RadialSizingTest:public QObject {
    Q_OBJECT
private slots:
    void feederAndBranches();void missingAndAmbiguous();void neutralAndHarmonics();
    void cumulativeDrop();void protectionAndFault();void curvesAndStarting();void profilesAndManual();
};
void RadialSizingTest::feederAndBranches() {
    const auto r=calculate(example());
    QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    QCOMPARE(get(r,"F-L").current,20.);QCOMPARE(get(r,"A-L").current,8.);QCOMPARE(get(r,"B-L").current,12.);
    QVERIFY(get(r,"F-L").section>get(r,"A-L").section);
    QCOMPARE(get(r,"F-N").current,20.);QCOMPARE(get(r,"F-PE").current,0.);
    QCOMPARE(get(r,"F-PE").section,get(r,"F-L").section);
    QCOMPARE(get(r,"A-PE").protection,QString("Q1"));QCOMPARE(get(r,"B-PE").protection,QString("Q2"));
    QCOMPARE(get(r,"F-L").loads.size(),2);QCOMPARE(get(r,"A-L").loads,QStringList{"H1"});
    QCOMPARE(r.protections["Q0"].nominal,20.);QCOMPARE(r.protections["Q1"].nominal,10.);
    QCOMPARE(r.protections["Q2"].nominal,13.);
    const double feeder=2*.0225*10*20/get(r,"F-L").section;
    const double branch=2*.0225*5*8/get(r,"A-L").section;
    QVERIFY(std::abs(r.loads[0].drop-100*(feeder+branch)/230)<1e-8);
}
void RadialSizingTest::missingAndAmbiguous() {
    auto n=example();n.loads[1].current=0;auto r=calculate(n);QVERIFY(!r.calculated && !r.ready);
    n=example();n.edges[0].length=0;r=calculate(n);QVERIFY(!r.calculated);
    n=example();n.stateConfirmed=false;r=calculate(n);QVERIFY(r.calculated && !r.ready);
    n=example();n.loads[0].harmonic3=-1;QVERIFY(!calculate(n).calculated);
    n=example();n.edges<<wire("parallel",0,3,"parallel");QVERIFY(!calculate(n).calculated);
    n=example();n.edges<<wire("short-N-PE",1,2,"invalid");QVERIFY(!calculate(n).calculated);
    n=example();n.edges[2].kind=EdgeKind::Device;QVERIFY(!calculate(n).calculated);
    n=example();n.loads[0].voltage=24;QVERIFY(!calculate(n).calculated);
    n=example();n.loads[0].pe=-1;QVERIFY(!calculate(n).calculated);
    n=example();n.edges[6].cable="A";QVERIFY(!calculate(n).calculated);
}
static Network star() {
    Network n;n.nodes=10;n.supply=Supply::ThreePhase;n.voltage=std::sqrt(3.)*230;n.frequencyHz=60;
    n.maxDrop=4;n.maxStartDrop=15;n.sourceR=.5;n.sourceFaultMax=1000;n.minVoltageFactor=.95;
    n.profile=sampleProfile();n.stateConfirmed=true;n.sourceProtection="Q0";n.protections.insert("Q0",protective("Q0"));
    for(int role=0;role<5;++role) {
        n.roots[role]=role;auto e=wire(roleName(Role(role)),role,role+5,"W",2);e.loaded=4;n.edges<<e;
    }
    Load l;l.id="3F";l.ports={5,6,7,8};l.pe=9;l.voltage=n.voltage;l.current=10;l.pf=1;l.harmonic3=0;n.loads={l};return n;
}
void RadialSizingTest::neutralAndHarmonics() {
    auto n=star();auto r=calculate(n);QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    QVERIFY(get(r,"N").current<1e-10);QCOMPARE(get(r,"N").section,get(r,"L1").section);
    n.loads[0].phaseCurrents={{10,5,0}};r=calculate(n);
    QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    QVERIFY(std::abs(get(r,"N").current-std::sqrt(75.))<1e-8);
    n=star();n.loads[0].harmonic3=.8;n.protections["Q0"].neutralProtected=true;r=calculate(n);
    QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    QVERIFY(std::abs(get(r,"N").current-30*.8/std::sqrt(1+.8*.8))<1e-8);
    QVERIFY(get(r,"N").current>get(r,"L1").current);
    n.protections["Q0"].neutralProtected=false;QVERIFY(!calculate(n).ready);
    n.protections["Q0"].neutralProtected=true;n.loads[0].spectrum={{5,.1},{7,.05}};r=calculate(n);
    QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    n.profile.harmonicFactor=0;QVERIFY(!calculate(n).ready);
    n=star();n.loads[0].phaseCurrents={{10,-1,0}};QVERIFY(!calculate(n).calculated);
    n=star();n.supply=Supply::TwoPhase;n.roots[L3]=-1;n.loads[0].ports={5,6};r=calculate(n);
    QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));QCOMPARE(get(r,"L1").current,10.);QCOMPARE(get(r,"L2").current,10.);
    n=example();n.supply=Supply::DC;n.roots[Plus]=n.roots[L1];n.roots[Minus]=n.roots[N];n.roots[L1]=n.roots[N]=-1;
    n.voltage=24;n.sourceR=.05;for(auto &l:n.loads)l.voltage=24;r=calculate(n);
    for(auto it=n.protections.begin();it!=n.protections.end();++it) {
        it->dcRated=true;it->fixedIn=it.key()=="Q0"?20:it.key()=="Q1"?10:13;
        it->reference="Envelope CC didático";it->points={{10,1,2},{100,.02,.1},{1000,.001,.01}};
    }
    r=calculate(n);
    QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    QCOMPARE(get(r,"F-L").current,20.);QCOMPARE(get(r,"F-N").current,20.);
}
void RadialSizingTest::cumulativeDrop() {
    auto n=example();n.maxDrop=1;n.edges[0].length=n.edges[1].length=70;
    auto r=calculate(n);QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    for(const auto &l:r.loads)QVERIFY(l.drop<=1+1e-8);
    QVERIFY(get(r,"F-L").section>calculate(example()).wires.first().section);
    n.edges[0].fixedSection=1.5;QVERIFY(!calculate(n).ready);
}
void RadialSizingTest::protectionAndFault() {
    auto n=example();n.protections["Q0"].fixedIn=10;QVERIFY(!calculate(n).calculated);
    n=example();n.protections["Q1"].i2Multiple=1.3;QVERIFY(!calculate(n).calculated);
    n=example();n.protections["Q1"].breakingA=100;QVERIFY(!calculate(n).ready);
    n=example();n.selectivity.clear();auto r=calculate(n);QVERIFY(r.calculated && !r.ready);
    n=example();n.sourceFaultMax=6000;for(auto &p:n.selectivity)p.limitA=6000;r=calculate(n);
    QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    for(const auto &w:r.wires)QVERIFY(w.section>=6000*std::sqrt(.1)/115.-1e-9);
    n=example();n.sourceR=100;QVERIFY(!calculate(n).ready);
    n=example();n.earthing="TT";
    for(auto it=n.protections.begin();it!=n.protections.end();++it) {
        it->rcdA=.03;it->rcdMaxSeconds=.04;it->rcdRatedA=63;it->earthResistance=50;it->touchLimit=50;it->rcdReference="DR didático";
    }
    r=calculate(n);QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    n.protections["Q1"].earthResistance=2000;QVERIFY(!calculate(n).ready);
}
void RadialSizingTest::curvesAndStarting() {
    Protection p=protective("P");p.points={{10,20,30},{100,2,3},{1000,.2,.3}};
    QVERIFY(validCurve(p.points));const auto point=trip(p,10,std::sqrt(1000.));
    QVERIFY(std::abs(point.first-std::sqrt(40.))<1e-8);QVERIFY(std::isinf(trip(p,10,2000).second));
    auto q=p;q.points={{10,2,3},{100,.2,.3},{1000,.02,.03}};
    QVERIFY(curveDiscrimination(p,10,q,10,500));QVERIFY(!curveDiscrimination(p,10,p,10,500));
    p.points[1].maximum=100;QVERIFY(!validCurve(p.points));
    auto n=example();n.loads[0].motor=true;n.loads[0].startMultiple=4;n.loads[0].startSeconds=.05;n.loads[0].startPF=.5;
    auto r=calculate(n);QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    QVERIFY(r.loads[0].startDrop>r.loads[0].drop);
    n.loads[0].startSeconds=2;QVERIFY(!calculate(n).ready);
    n=example();n.loads[0].motor=true;QVERIFY(!calculate(n).ready);
}
void RadialSizingTest::profilesAndManual() {
    auto n=example();n.profile.confirmed=false;QVERIFY(!calculate(n).ready);
    n=example();auto p=sampleProfile();p.material="Al";p.resistivity=.036;p.k=76;p.method="Método fornecido";
    n.profiles["al"]=p;for(auto &e:n.edges)if(e.cable=="F")e.profile="al";
    auto r=calculate(n);QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));
    auto pe=sampleProfile();pe.minimumPE=4;n.profiles["cu-pe"]=pe;n.edges[2].profile="cu-pe";
    r=calculate(n);QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));QVERIFY(get(r,"F-PE").section>=4);
    n.edges[0].temperatureFactor=.01;r=calculate(n);QVERIFY(!r.ready);
    n=example();n.profile.table[1].section=1.5;QVERIFY(!calculate(n).calculated);
    n=example();n.loads[0].current=0;n.loads[0].power=1840;n.loads[0].mechanical=true;n.loads[0].efficiency=.8;
    r=calculate(n);QVERIFY2(r.ready,qPrintable((r.errors+r.pending).join('\n')));QCOMPARE(get(r,"A-L").current,10.);
}
QTEST_GUILESS_MAIN(RadialSizingTest)
#include "tst_radialsizing.moc"
