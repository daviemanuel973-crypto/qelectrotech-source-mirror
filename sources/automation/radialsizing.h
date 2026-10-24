/* GPL-2.0-or-later. Radial LV network sizing, independent of the GUI.
 * Every result is conditional on the declared installation and device data.
 * No geometric distance, diversity factor or manufacturer curve is invented.
 */
#ifndef QET_RADIAL_SIZING_H
#define QET_RADIAL_SIZING_H
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QMap>
#include <array>
#include <complex>
#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>

namespace RadialSizing {
using Complex=std::complex<double>;
constexpr double pi=3.14159265358979323846;
constexpr double infinity=std::numeric_limits<double>::infinity();
enum class Supply { SinglePhase, TwoPhase, ThreePhase, DC };
enum Role { L1, L2, L3, N, PE, Plus, Minus, RoleCount };
enum class EdgeKind { Wire, Device, Join };
struct Ampacity { double section=0, two=0, three=0, four=0; };
struct Profile {
    QString material,insulation,method,reference;
    double resistivity=0,reactancePerKm=0,k=0,harmonicFactor=0;
    double minimumPhase=0,minimumNeutral=0,minimumPE=0;
    bool confirmed=false;
    QVector<Ampacity> table;
};
struct CurvePoint { double current=0,minimum=0,maximum=0; };
struct Protection {
    QString id,curve,reference;
    double fixedIn=0,breakingA=0,i2Multiple=0,maxDisconnect=0;
    QVector<double> ratings{2,4,6,10,13,16,20,25,32,40,50,63,80,100,125,160,200,250,315,400,500,630};
    QVector<CurvePoint> points; // Absolute A and seconds, manufacturer envelope.
    double rcdA=0,rcdMaxSeconds=0,rcdRatedA=0,earthResistance=0,touchLimit=0;
    bool neutralProtected=false,dcRated=false;
    QString rcdReference;
};
struct Selectivity {
    QString upstream,downstream,reference;
    double limitA=0; // Manufacturer's tested discrimination limit.
};
struct Edge {
    QString id,cable,protection,profile; // protection is populated on Device edges.
    int a=-1,b=-1;
    EdgeKind kind=EdgeKind::Wire;
    bool peContinuous=false; // Explicitly verified unswitched connector path.
    double length=0,temperatureFactor=0,groupingFactor=0,minimum=0;
    double fixedSection=0; // Manual sections remain fixed unless overwrite is explicit.
    int loaded=0;
};
struct Load {
    QString id;
    QVector<int> ports; // Active terminals only; PE is separate.
    int pe=-1;
    bool requiresPE=true,mechanical=false,motor=false;
    double voltage=0,current=0,power=0,pf=0,efficiency=0,harmonic3=-1;
    QMap<int,double> spectrum; // Other harmonic RMS amplitudes / fundamental.
    double demand=1; // Explicit utilisation/simultaneity, never inferred.
    double startMultiple=0,startSeconds=0,startPF=0;
    std::array<double,3> phaseCurrents{{-1,-1,-1}}; // Optional unbalanced star load.
};
struct Network {
    Supply supply=Supply::ThreePhase;
    QString earthing="TN",sourceProtection;
    int nodes=0;
    std::array<int,RoleCount> roots{{-1,-1,-1,-1,-1,-1,-1}};
    double voltage=0,maxDrop=0,maxStartDrop=0,sourceR=0,sourceX=0;
    double sourceFaultMax=0,minVoltageFactor=0;
    double frequencyHz=0;
    bool stateConfirmed=false;
    Profile profile;
    QHash<QString,Profile> profiles; // Optional cable-specific installation profiles.
    QVector<Edge> edges;
    QVector<Load> loads;
    QHash<QString,Protection> protections;
    QVector<Selectivity> selectivity;
};
struct WireResult {
    QString id,protection;
    Role role=L1;
    double current=0,section=0,ampacity=0,neutralFundamental=0,harmonic=0;
    double shortCircuitMinimum=0,voltageDrop=0,voltage=0;
    QStringList loads;
};
struct LoadResult { QString id; double current=0,power=0,drop=0,startDrop=0,faultMin=0; };
struct ProtectionResult { QString id; double current=0,nominal=0,i2=0; bool breaking=false,thermal=false,disconnection=false; };
struct Result {
    bool calculated=false,ready=false;
    QVector<WireResult> wires;
    QVector<LoadResult> loads;
    QHash<QString,ProtectionResult> protections;
    QStringList errors,pending,coordination;
};
inline bool positive(double x) { return std::isfinite(x) && x>0; }
inline bool nonnegative(double x) { return std::isfinite(x) && x>=0; }
inline bool phase(Role r) { return r==L1 || r==L2 || r==L3 || r==Plus; }
inline QString roleName(Role r) { return QStringList{"L1","L2","L3","N","PE","+","−"}.value(r); }
inline bool validProfile(const Profile &p) {
    if(!positive(p.resistivity) || !nonnegative(p.reactancePerKm) || !positive(p.k) ||
       !positive(p.minimumPhase) || !positive(p.minimumNeutral) || !positive(p.minimumPE) ||
       !nonnegative(p.harmonicFactor) || p.harmonicFactor>1 ||
       p.material.isEmpty() || p.insulation.isEmpty() || p.method.isEmpty() ||
       p.reference.isEmpty() || p.table.isEmpty())return false;
    double last=0;
    for(const auto &r:p.table) {
        if(!positive(r.section) || r.section<=last || !positive(r.two) || !positive(r.three) ||
           !nonnegative(r.four))return false;
        last=r.section;
    }
    return true;
}
inline bool validCurve(const QVector<CurvePoint> &points) {
    double last=0,lo=infinity,hi=infinity;
    for(const auto &p:points) {
        if(!positive(p.current) || p.current<=last || !nonnegative(p.minimum) ||
           !positive(p.maximum) || p.minimum>p.maximum || p.minimum>lo || p.maximum>hi)return false;
        last=p.current;lo=p.minimum;hi=p.maximum;
    }
    return points.size()>=2;
}
inline QPair<double,double> trip(const Protection &p,double nominal,double current) {
    if(!positive(nominal) || !nonnegative(current))return {0,infinity};
    if(!p.points.isEmpty()) {
        if(!validCurve(p.points) || current<p.points.first().current || current>p.points.last().current)
            return {0,infinity};
        for(int i=1;i<p.points.size();++i)if(current<=p.points[i].current) {
            const auto &a=p.points[i-1],&b=p.points[i];
            const double t=std::log(current/a.current)/std::log(b.current/a.current);
            const double lo=(a.minimum==0 || b.minimum==0)?0:
                std::exp(std::log(a.minimum)*(1-t)+std::log(b.minimum)*t);
            return {lo,std::exp(std::log(a.maximum)*(1-t)+std::log(b.maximum)*t)};
        }
    }
    // IEC 60898-1 guaranteed test boundaries, deliberately not a fabricated
    // smooth manufacturer curve. Gaps have no guaranteed clearing time.
    const double lower=p.curve=="B"?3:p.curve=="C"?5:p.curve=="D"?10:0;
    const double upper=p.curve=="B"?5:p.curve=="C"?10:p.curve=="D"?20:0;
    if(lower==0)return {0,infinity};
    const double m=current/nominal;
    if(m>=upper)return {0,0.1};
    if(m>=lower)return {0,nominal<=32?60.:120.};
    if(m>=2.55)return {0.1,nominal<=32?60.:120.};
    if(m>=1.45)return {1,nominal<=63?3600.:7200.};
    if(m<=1.13)return {3600,infinity};
    return {0,infinity};
}
inline bool curveDiscrimination(const Protection &up,double upIn,const Protection &down,
                                double downIn,double maximum) {
    if(up.points.isEmpty() || down.points.isEmpty() || !validCurve(up.points) || !validCurve(down.points))return false;
    const double start=std::min(upIn,downIn)*1.45;
    if(!(maximum>=start) || up.points.first().current>start || down.points.first().current>start ||
       up.points.last().current<maximum || down.points.last().current<maximum)return false;
    QVector<double> at{start,maximum};
    for(const auto &p:up.points)if(p.current>start && p.current<maximum)at<<p.current;
    for(const auto &p:down.points)if(p.current>start && p.current<maximum)at<<p.current;
    // Ratios of log-linear envelopes are monotone between their breakpoints.
    for(double a:at)if(!(trip(up,upIn,a).first>trip(down,downIn,a).second))return false;
    return true;
}
struct UnionFind {
    QVector<int> parent;
    explicit UnionFind(int n):parent(n) {for(int i=0;i<n;++i)parent[i]=i;}
    int find(int x) {int root=x;while(parent[root]!=root)root=parent[root];while(parent[x]!=x){int next=parent[x];parent[x]=root;x=next;}return root;}
    void join(int a,int b) {a=find(a);b=find(b);if(a!=b)parent[b]=a;}
};
inline Result calculate(const Network &net) {
    Result out;
    auto fail=[&](const QString &s){out.errors<<s;};
    auto pending=[&](const QString &s){if(!out.pending.contains(s))out.pending<<s;};
    if(net.nodes<=0 || !positive(net.voltage) || !positive(net.maxDrop) || net.maxDrop>15 ||
       !positive(net.maxStartDrop) || net.maxStartDrop>30) {fail("Informe tensão e limites de queda de regime/partida.");return out;}
    if(net.voltage>(net.supply==Supply::DC?1500:1000)) {fail("Modelo de baixa tensão: limite de 1000 V CA / 1500 V CC.");return out;}
    if(net.supply!=Supply::DC && !positive(net.frequencyHz)){fail("Declare a frequência da alimentação CA.");return out;}
    if(!validProfile(net.profile)) {fail("Perfil de cabo incompleto ou tabela de capacidade inválida.");return out;}
    if(!net.profile.confirmed)pending("Confirmar a referência da tabela e o perfil real de material/isolação/instalação.");
    if(!net.stateConfirmed)pending("Confirmar o estado de cálculo dos contatos de manobra.");
    if(net.earthing!="TN" && net.earthing!="TT")pending("Esquema de aterramento requer estudo específico (suportados TN e TT).");
    if(!positive(net.sourceFaultMax) || !positive(net.sourceR) || !nonnegative(net.sourceX) ||
       !positive(net.minVoltageFactor) || net.minVoltageFactor>1)
        pending("Informe curto máximo na origem, impedância de laço R/X e fator mínimo de tensão.");
    auto nodeOK=[&](int n){return n>=0 && n<net.nodes;};
    UnionFind uf(net.nodes);
    QSet<QString> ids;
    for(const auto &e:net.edges) {
        if(e.id.isEmpty() || ids.contains(e.id) || !nodeOK(e.a) || !nodeOK(e.b))fail("Aresta inválida/duplicada: "+e.id);
        ids.insert(e.id);
        if(e.kind==EdgeKind::Join && nodeOK(e.a) && nodeOK(e.b))uf.join(e.a,e.b);
    }
    if(!out.errors.isEmpty())return out;
    QVector<QVector<int>> adjacency(net.nodes);
    for(int i=0;i<net.edges.size();++i) {
        const auto &e=net.edges[i];if(e.kind==EdgeKind::Join)continue;
        const int a=uf.find(e.a),b=uf.find(e.b);
        if(a==b)fail("Laço/condutor em paralelo sem repartição conhecida: "+e.id);
        adjacency[a]<<i;adjacency[b]<<i;
    }
    QVector<int> root(net.nodes,-1),parent(net.nodes,-1),parentEdge(net.nodes,-1),order;
    QVector<QString> protectionAt(net.nodes);
    QHash<QString,QString> upstream;
    for(int r=0;r<RoleCount;++r)if(nodeOK(net.roots[r])) {
        const int n=uf.find(net.roots[r]);
        if(root[n]>=0) {fail("Duas funções da fonte no mesmo nó: "+roleName(Role(r)));continue;}
        root[n]=r;protectionAt[n]=net.sourceProtection;order<<n;
    }
    const auto required=net.supply==Supply::DC?QVector<int>{Plus,Minus}:
        net.supply==Supply::ThreePhase?QVector<int>{L1,L2,L3}:net.supply==Supply::TwoPhase?QVector<int>{L1,L2}:QVector<int>{L1,N};
    for(int r:required)if(!nodeOK(net.roots[r]))fail("Terminal da fonte não identificado: "+roleName(Role(r)));
    const QVector<int> excluded=net.supply==Supply::DC?QVector<int>{L1,L2,L3,N}:
        net.supply==Supply::SinglePhase?QVector<int>{L2,L3,Plus,Minus}:
        net.supply==Supply::TwoPhase?QVector<int>{L3,Plus,Minus}:QVector<int>{Plus,Minus};
    for(int r:excluded)if(net.roots[r]>=0)fail("Função de origem incompatível com a alimentação: "+roleName(Role(r)));
    for(int at=0;at<order.size();++at) {
        const int n=order[at];
        for(int ei:adjacency[n]) {
            if(ei==parentEdge[n])continue;
            const auto &e=net.edges[ei];const int a=uf.find(e.a),b=uf.find(e.b),next=a==n?b:a;
            if(root[n]==PE && e.kind==EdgeKind::Device && !e.peContinuous) {
                fail("PE não pode passar por contato de manobra/proteção: "+e.id);continue;
            }
            if(root[next]>=0) {fail("Malha, paralelo ou conexão entre funções/domínios: "+e.id);continue;}
            root[next]=root[n];parent[next]=n;parentEdge[next]=ei;protectionAt[next]=protectionAt[n];
            if(e.kind==EdgeKind::Device && !e.protection.isEmpty() && root[next]!=PE) {
                if(upstream.contains(e.protection) && upstream[e.protection]!=protectionAt[n])
                    fail("Proteção associada a origens diferentes: "+e.protection);
                upstream[e.protection]=protectionAt[n];protectionAt[next]=e.protection;
            }
            order<<next;
        }
    }
    if(!out.errors.isEmpty())return out;
    auto voltage=[&](int role)->Complex {
        if(role==N || role==PE || role==Minus)return {};
        if(net.supply==Supply::ThreePhase || net.supply==Supply::TwoPhase)return std::polar(net.voltage/std::sqrt(3.),
            role==L2?-2*pi/3:role==L3?2*pi/3:0.);
        return {net.voltage,0};
    };
    struct Flow { QVector<Complex> current; QVector<double> h3; };
    Flow injection{QVector<Complex>(net.nodes),QVector<double>(net.nodes,0)};
    QVector<Flow> byLoad;
    QVector<QSet<int>> downstream(net.edges.size());
    QVector<QVector<int>> loadPaths;
    auto path=[&](int n){QVector<int> p;for(n=uf.find(n);parentEdge[n]>=0;n=parent[n])p<<parentEdge[n];return p;};
    QSet<QString> loadIds;
    QVector<double> runningCurrent;
    int maximumHarmonicOrder=3;
    for(int li=0;li<net.loads.size();++li) {
        const auto &load=net.loads[li];LoadResult lr;lr.id=load.id;
        if(load.id.isEmpty() || loadIds.contains(load.id))fail("Carga com identificador vazio/duplicado: "+load.id);
        loadIds.insert(load.id);
        Flow own{QVector<Complex>(net.nodes),QVector<double>(net.nodes,0)};
        QVector<int> pins;QSet<int> roles;
        for(int p:load.ports) {
            if(!nodeOK(p) || root[uf.find(p)]<0 || root[uf.find(p)]==PE) {fail("Carga sem percurso completo até a fonte: "+load.id);continue;}
            pins<<uf.find(p);roles.insert(root[uf.find(p)]);
        }
        if(pins.size()!=load.ports.size() || pins.size()<2 || pins.size()>4 || roles.size()!=pins.size()) {
            fail("Terminais de alimentação ambíguos/incompletos: "+load.id);continue;
        }
        const bool three=roles.contains(L1) && roles.contains(L2) && roles.contains(L3);
        if(!three && pins.size()!=2) {fail("Carga multipolar não resolvida: "+load.id);continue;}
        const double v=three?net.voltage:std::abs(voltage(root[pins[0]])-voltage(root[pins[1]]));
        if(!positive(v) || !positive(load.voltage) || std::abs(load.voltage-v)>0.02*v) {
            fail("Tensão da carga incompatível com sua alimentação: "+load.id);continue;
        }
        const double pf=net.supply==Supply::DC?1.:load.pf;
        if(!positive(pf) || pf>1 || !positive(load.demand) || load.demand>1 ||
           !nonnegative(load.harmonic3) || load.harmonic3>3 || (net.supply==Supply::DC && load.harmonic3!=0)) {
            fail("Informe cos φ, demanda e harmônica de 3ª ordem (zero se ausente): "+load.id);continue;
        }
        double power=load.power;
        if(load.mechanical) {
            if(!positive(load.efficiency) || load.efficiency>1) {fail("Rendimento ausente/inválido: "+load.id);continue;}
            power/=load.efficiency;
        }
        const double multiplier=three?std::sqrt(3.):1.;
        double harmonicsSquared=load.harmonic3*load.harmonic3;
        for(auto it=load.spectrum.cbegin();it!=load.spectrum.cend();++it) {
            if(it.key()<2 || it.key()>99 || it.key()==3 || !nonnegative(it.value()) || it.value()>100)
                fail("Espectro harmônico inválido (ordens 2–99, 3ª em campo próprio): "+load.id);
            harmonicsSquared+=it.value()*it.value();if(it.value()>0)maximumHarmonicOrder=std::max(maximumHarmonicOrder,it.key());
        }
        if(net.supply==Supply::DC && harmonicsSquared!=0)fail("Espectro CA não se aplica à alimentação CC: "+load.id);
        const double harmonicRatio=std::sqrt(harmonicsSquared);
        const double harmonicScale=std::sqrt(1+harmonicsSquared);
        const bool unbalanced=std::any_of(load.phaseCurrents.begin(),load.phaseCurrents.end(),[](double a){return a>=0;});
        if(unbalanced && (!three || !roles.contains(N) ||
           !std::all_of(load.phaseCurrents.begin(),load.phaseCurrents.end(),[](double a){return nonnegative(a);}))) {
            fail("Correntes por fase completas requerem estrela com neutro: "+load.id);continue;
        }
        double fundamental=0;
        if(unbalanced) {
            const double peak=*std::max_element(load.phaseCurrents.begin(),load.phaseCurrents.end());
            const double computed=(load.phaseCurrents[0]+load.phaseCurrents[1]+load.phaseCurrents[2])*
                net.voltage/std::sqrt(3.)*pf/harmonicScale;
            if((positive(load.current) && std::abs(load.current-peak)>.05*peak) ||
               (positive(power) && std::abs(power-computed)>std::max(1.,computed*.05)))
                fail("Dados nominais divergem das correntes por fase: "+load.id);
            fundamental=peak/harmonicScale;power=computed;
        } else if(positive(load.current)) {
            fundamental=load.current/harmonicScale;
            const double computed=multiplier*v*fundamental*pf;
            if(positive(power) && std::abs(power-computed)>std::max(1.,computed*.05))
                fail("Corrente/potência declaradas divergem mais de 5%: "+load.id);
            power=computed;
        } else if(positive(power))fundamental=power/(multiplier*v*pf);
        else {fail("Corrente/potência da carga não informada: "+load.id);continue;}
        if(!positive(fundamental) || !positive(power)) {fail("Dados elétricos fora de escala: "+load.id);continue;}
        Complex sum;double harmonicSum=0;
        for(int n:pins)if(root[n]!=N && root[n]!=Minus) {
            double amp=fundamental;
            if(unbalanced) {
                const double declared=load.phaseCurrents[root[n]];
                if(!nonnegative(declared)) {fail("Correntes por fase incompletas: "+load.id);continue;}
                amp=declared/harmonicScale;
            }
            Complex current;
            if(three)current=std::polar(amp*load.demand,std::arg(voltage(root[n]))-std::acos(pf));
            else {
                // For a line-line two-wire load the current follows Vab,
                // not the phase-to-neutral angle; reverse the other port.
                const Complex dv=voltage(root[pins[0]])-voltage(root[pins[1]]);
                const double sign=n==pins[0]?1.:-1.;
                current=sign*std::polar(amp*load.demand,std::arg(dv)-std::acos(pf));
            }
            own.current[n]+=current;sum+=current;
            const double h=amp*load.demand*harmonicRatio;
            own.h3[n]+=h;harmonicSum+=h;
        }
        if(three) {
            for(int n:pins)if(root[n]==N) {own.current[n]-=sum;own.h3[n]+=harmonicSum;}
        } else {
            const bool returnPresent=roles.contains(N) || roles.contains(Minus);
            if(returnPresent)for(int n:pins)if(root[n]==N || root[n]==Minus) {
                own.current[n]-=sum;own.h3[n]+=harmonicSum;
            }
        }
        if(load.requiresPE && (!nodeOK(load.pe) || root[uf.find(load.pe)]!=PE))
            fail("Carga classe I sem continuidade de PE até a fonte: "+load.id);
        if(load.motor && (!positive(load.startMultiple) || !positive(load.startSeconds) ||
                          !positive(load.startPF) || load.startPF>1))
            pending("Dados de partida do motor incompletos: "+load.id);
        QSet<int> used;
        for(int n:pins)for(int ei:path(n))used.insert(ei);
        if(load.requiresPE && nodeOK(load.pe) && root[uf.find(load.pe)]==PE)
            for(int ei:path(load.pe))used.insert(ei);
        QVector<int> paths=used.values();std::sort(paths.begin(),paths.end());loadPaths<<paths;
        for(int ei:paths)downstream[ei].insert(li);
        for(int n=0;n<net.nodes;++n){injection.current[n]+=own.current[n];injection.h3[n]+=own.h3[n];}
        byLoad<<own;lr.current=fundamental*harmonicScale;lr.power=power;out.loads<<lr;runningCurrent<<lr.current;
    }
    if(net.loads.isEmpty())fail("Nenhuma carga associada à fonte.");
    if(!out.errors.isEmpty())return out;
    auto accumulated=[&](Flow f){
        for(int i=order.size()-1;i>=0;--i){const int n=order[i];if(parent[n]>=0) {
            f.current[parent[n]]+=f.current[n];f.h3[parent[n]]+=f.h3[n];}}
        return f;
    };
    const Flow flow=accumulated(injection);
    QVector<int> child(net.edges.size(),-1),index(net.edges.size(),-1);
    QVector<double> amps(net.edges.size()),need(net.edges.size());
    QHash<QString,QVector<int>> cables;
    auto profile=[&](int ei)->const Profile & {
        const auto it=net.profiles.constFind(net.edges[ei].profile);
        return it==net.profiles.cend()?net.profile:it.value();
    };
    for(int n:order)if(parentEdge[n]>=0) {
        const int ei=parentEdge[n];child[ei]=n;
        amps[ei]=std::hypot(std::abs(flow.current[n]),flow.h3[n]);
        if(!nonnegative(amps[ei]))fail("Corrente agregada fora de escala: "+net.edges[ei].id);
        const auto &e=net.edges[ei];if(e.kind!=EdgeKind::Wire || downstream[ei].isEmpty())continue;
        if(!e.profile.isEmpty() && !net.profiles.contains(e.profile))fail("Perfil de instalação desconhecido: "+e.profile);
        if(!validProfile(profile(ei)))fail("Perfil inválido no trecho: "+e.id);
        if(!profile(ei).confirmed)pending("Confirmar tabela/material/instalação do trecho: "+e.id);
        if(flow.h3[n]>0 && !positive(profile(ei).harmonicFactor))pending("Declare fator térmico para o espectro harmônico no perfil do trecho: "+e.id);
        if(!positive(e.length) || !positive(e.temperatureFactor) || e.temperatureFactor>2 ||
           !positive(e.groupingFactor) || e.groupingFactor>1 ||
           !nonnegative(e.minimum) || !nonnegative(e.fixedSection) ||
           (e.loaded!=2 && e.loaded!=3 && e.loaded!=4))
            fail("Comprimento físico/fatores/condutores carregados pendentes: "+e.id);
        if(e.cable.isEmpty())fail("Associe o trecho a um cabo/circuito físico: "+e.id);
        else cables[e.cable]<<ei;
        const Role role=Role(root[n]);
        need[ei]=std::max(e.minimum,role==PE?profile(ei).minimumPE:
            role==N?profile(ei).minimumNeutral:profile(ei).minimumPhase);
    }
    if(!out.errors.isEmpty())return out;
    // An unswitched neutral inherits the protective device of its physical
    // cable's phase cores, not the last device encountered on the N drawing.
    for(auto it=cables.cbegin();it!=cables.cend();++it) {
        QSet<QString> phaseProtection;
        for(int ei:it.value())if(phase(Role(root[child[ei]])))phaseProtection.insert(protectionAt[child[ei]]);
        if(phaseProtection.size()!=1) {fail("Cabo sem proteção comum inequívoca para suas fases: "+it.key());continue;}
        const QString id=*phaseProtection.cbegin();
        for(int ei:it.value())if(root[child[ei]]==N || root[child[ei]]==Minus || root[child[ei]]==PE)protectionAt[child[ei]]=id;
    }
    for(int ei=0;ei<net.edges.size();++ei)if(index[ei]<0 && child[ei]>=0 &&
        net.edges[ei].kind==EdgeKind::Wire && !downstream[ei].isEmpty() && root[child[ei]]!=PE) {
        const int n=child[ei];const QString id=protectionAt[n];
        if(!net.protections.contains(id)) {pending("Proteção não cadastrada: "+id);continue;}
        auto &pr=out.protections[id];pr.id=id;pr.current=std::max(pr.current,amps[ei]);
    }
    if(!out.errors.isEmpty())return out;
    // Devices carry current even if their first following edge is another
    // protective device. Include every pole in their common nominal rating.
    for(int ei=0;ei<net.edges.size();++ei)if(child[ei]>=0 && net.edges[ei].kind==EdgeKind::Device &&
       !net.edges[ei].protection.isEmpty() && root[child[ei]]!=PE) {
        const QString id=net.edges[ei].protection;auto &pr=out.protections[id];
        pr.id=id;pr.current=std::max(pr.current,amps[ei]);
    }
    auto effectiveProtection=[&](const QString &id) {
        Protection p=net.protections.value(id);QString ancestor=upstream.value(id);QSet<QString> seen{id};
        while(p.rcdA==0 && !ancestor.isEmpty() && !seen.contains(ancestor)) {
            seen.insert(ancestor);const auto r=net.protections.value(ancestor);
            if(r.rcdA>0) {p.rcdA=r.rcdA;p.rcdMaxSeconds=r.rcdMaxSeconds;p.rcdRatedA=r.rcdRatedA;
                p.earthResistance=r.earthResistance;p.touchLimit=r.touchLimit;p.rcdReference=r.rcdReference;}
            ancestor=upstream.value(ancestor);
        }return p;
    };
    for(auto it=out.protections.begin();it!=out.protections.end();++it) {
        const auto p=effectiveProtection(it.key());auto &pr=it.value();
        if(!nonnegative(p.fixedIn) || !nonnegative(p.breakingA) || !nonnegative(p.maxDisconnect) ||
           !nonnegative(p.rcdA) || !nonnegative(p.rcdMaxSeconds) || !nonnegative(p.rcdRatedA) ||
           !nonnegative(p.earthResistance) || !nonnegative(p.touchLimit))fail("Dados numéricos da proteção inválidos: "+it.key());
        double previous=0;for(double rating:p.ratings){if(!positive(rating) || rating<=previous)fail("Série de calibres inválida: "+it.key());previous=rating;}
        if(p.fixedIn>0)pr.nominal=p.fixedIn;
        else for(double rating:p.ratings)if(positive(rating) && rating>=pr.current) {pr.nominal=rating;break;}
        if(!positive(pr.nominal) || pr.nominal<pr.current || !positive(p.i2Multiple) || p.i2Multiple<1)
            fail("Proteção não suporta a corrente de projeto: "+it.key());
        if(p.points.isEmpty() && QStringList{"B","C","D"}.contains(p.curve) && p.i2Multiple<1.45)
            fail("I2/In abaixo do limite garantido do modelo genérico IEC 60898-1: "+it.key());
        pr.i2=pr.nominal*p.i2Multiple;
        pr.breaking=positive(p.breakingA) && positive(net.sourceFaultMax) && p.breakingA>=net.sourceFaultMax;
        if(!pr.breaking)pending("Capacidade de interrupção insuficiente/não declarada: "+it.key());
        if(!positive(p.maxDisconnect))pending("Tempo máximo de seccionamento não declarado: "+it.key());
        if(!p.points.isEmpty() && (!validCurve(p.points) || p.reference.isEmpty()))
            fail("Envelope/documento da proteção inválido: "+it.key());
        if(!p.points.isEmpty() && !positive(p.fixedIn))
            pending("Envelope em ampères absolutos exige In do modelo documentado: "+it.key());
        if(!p.points.isEmpty() && positive(net.sourceFaultMax) && p.points.last().current<net.sourceFaultMax)
            pending("Envelope do fabricante não cobre o curto máximo declarado: "+it.key());
        if(p.points.isEmpty() && p.curve!="B" && p.curve!="C" && p.curve!="D")
            pending("Declare curva B/C/D IEC 60898-1 ou envelope do fabricante: "+it.key());
        if(p.points.isEmpty() && (pr.nominal>125 || net.voltage>440 ||
           (net.supply!=Supply::DC && net.frequencyHz!=50 && net.frequencyHz!=60)))
            pending("Condições fora do modelo genérico IEC 60898-1: forneça envelope do fabricante: "+it.key());
        if(net.supply==Supply::DC && (!p.dcRated || p.points.isEmpty() || p.reference.isEmpty()))
            pending("CC requer capacidade/curva declaradas para uso em corrente contínua: "+it.key());
        if(net.earthing=="TT" && !(positive(p.rcdA) && positive(p.rcdMaxSeconds) &&
           positive(p.earthResistance) && positive(p.touchLimit) && p.earthResistance*p.rcdA<=p.touchLimit &&
           p.rcdRatedA>=pr.nominal && p.touchLimit<=(net.supply==Supply::DC?120:50) && !p.rcdReference.isEmpty()))
            pending("TT: conferir DR, RA × IΔn ≤ UL, corrente nominal e documento: "+it.key());
    }
    if(!out.errors.isEmpty())return out;
    auto capacity=[&](int ei,int ix){const auto &e=net.edges[ei];const auto &r=profile(ei).table[ix];
        const double harmonic=child[ei]>=0 && flow.h3[child[ei]]>0 && profile(ei).harmonicFactor>0?profile(ei).harmonicFactor:1.;
        return (e.loaded==2?r.two:e.loaded==3?r.three:r.four)*e.temperatureFactor*e.groupingFactor*harmonic;};
    auto select=[&](int ei,double minimum)->bool {
        const auto &e=net.edges[ei];if(child[ei]<0 || e.kind!=EdgeKind::Wire || downstream[ei].isEmpty())return true;
        const Role role=Role(root[child[ei]]);const auto pr=out.protections.value(protectionAt[child[ei]]);
        for(int ix=std::max(0,index[ei]);ix<profile(ei).table.size();++ix) {
            const double s=profile(ei).table[ix].section,iz=capacity(ei,ix);
            if(e.fixedSection>0 && std::abs(s-e.fixedSection)>1e-8)continue;
            if(s+1e-8<minimum || (role!=PE && (amps[ei]>iz || pr.nominal>iz || pr.i2>1.45*iz)))continue;
            if(role!=PE && !positive(iz))continue;
            index[ei]=ix;return true;
        }
        return false;
    };
    for(int ei=0;ei<net.edges.size();++ei)if(!select(ei,need[ei]))fail("Seção manual/tabela insuficiente: "+net.edges[ei].id);
    if(!out.errors.isEmpty())return out;
    auto impedance=[&](int ei){const auto &e=net.edges[ei];if(index[ei]<0)return Complex();
        return Complex(profile(ei).resistivity*e.length/profile(ei).table[index[ei]].section,
            net.supply==Supply::DC?0:profile(ei).reactancePerKm*e.length/1000.);};
    auto nodeDrops=[&](const Flow &f){QVector<Complex> d(net.nodes);
        for(int n:order)if(parent[n]>=0)d[n]=d[parent[n]]+impedance(parentEdge[n])*f.current[n];return d;};
    auto harmonicDrops=[&](const Flow &f){QVector<double> d(net.nodes);
        for(int n:order)if(parent[n]>=0) {
            const auto z=impedance(parentEdge[n]);
            d[n]=d[parent[n]]+std::abs(Complex(z.real(),maximumHarmonicOrder*z.imag()))*f.h3[n];
        }return d;};
    auto loadDrop=[&](int li,const QVector<Complex> &d,const QVector<double> &h){const auto &load=net.loads[li];double worst=0;
        for(int a:load.ports)for(int b:load.ports)if(a!=b) {
            a=uf.find(a);b=uf.find(b);const double v=std::abs(voltage(root[a])-voltage(root[b]));
            if(v>0)worst=std::max(worst,100*(std::abs(d[a]-d[b])+h[a]+h[b])/v);
        }return worst;};
    int limit=1;for(int ei=0;ei<net.edges.size();++ei)limit+=profile(ei).table.size();
    bool solved=false;
    for(int iteration=0;iteration<limit;++iteration) {
        bool changed=false,failed=false;
        QStringList iterationPending;
        auto conditioned=[&](const QString &s){if(!iterationPending.contains(s))iterationPending<<s;};
        for(auto it=out.protections.begin();it!=out.protections.end();++it) {
            it->disconnection=true;it->thermal=true;
        }
        for(auto &load:out.loads)load.faultMin=0;
        for(auto it=cables.cbegin();it!=cables.cend();++it) {
            double phaseSection=0;bool nCharged=false;QSet<QString> phaseMaterials;
            for(int ei:it.value())if(phase(Role(root[child[ei]]))) {
                phaseSection=std::max(phaseSection,profile(ei).table[index[ei]].section);
                phaseMaterials.insert(profile(ei).material.trimmed().toCaseFolded());
            } else if(root[child[ei]]==N && amps[ei]>1e-7)nCharged=true;
            // A cable may show several successive schematic wire pieces of
            // a core; count distinct functions rather than those occurrences.
            QSet<int> liveRoles;for(int ei:it.value())if(phase(Role(root[child[ei]])))liveRoles.insert(root[child[ei]]);
            const int minimumLoaded=std::max(2,int(liveRoles.size())+(nCharged?1:0));
            auto ancestor=[&](int a,int b){for(;b>=0;b=parent[b])if(a==b)return true;return false;};
            for(int a:it.value())for(int b:it.value())if(a<b) {
                if(root[child[a]]==root[child[b]] && !ancestor(child[a],child[b]) && !ancestor(child[b],child[a])) {
                    fail("Identificador de cabo reutilizado em derivações distintas: "+it.key());failed=true;
                }
            }
            double phaseCurrent=0;
            for(int ei:it.value())if(phase(Role(root[child[ei]])))phaseCurrent=std::max(phaseCurrent,amps[ei]);
            for(int ei:it.value())if(root[child[ei]]==N && amps[ei]>phaseCurrent+1e-7 &&
                !net.protections.value(protectionAt[child[ei]]).neutralProtected)
                conditioned("Neutro supera as fases: declarar proteção adequada do neutro no cabo "+it.key());
            for(int ei:it.value()) {
                const auto role=Role(root[child[ei]]);const int old=index[ei];
                if(net.edges[ei].loaded<minimumLoaded && role!=PE) {fail("Número de condutores carregados subestimado no cabo: "+it.key());failed=true;break;}
                double minimum=need[ei];
                if(role==N || role==Minus)minimum=std::max(minimum,phaseSection); // No reduction by assumption.
                if(role==PE) {
                    if(!positive(phaseSection)) {fail("PE sem fases associadas ao mesmo cabo: "+it.key());failed=true;break;}
                    if(phaseMaterials.size()==1 && phaseMaterials.contains(profile(ei).material.trimmed().toCaseFolded()))
                        minimum=std::max(minimum,phaseSection<=16?phaseSection:phaseSection<=35?16:phaseSection/2);
                    // Different materials use their own k, fault-clearing
                    // energy and declared mechanical minimum, not the
                    // same-material phase/PE shortcut.
                }
                if(!select(ei,minimum)) {fail("Neutro/PE ou cabo excede tabela/seção manual: "+net.edges[ei].id);failed=true;break;}
                changed|=old!=index[ei];
            }
        }
        if(failed)break;
        const auto drops=nodeDrops(flow);
        const auto hDrops=harmonicDrops(flow);
        QSet<int> grow;
        for(int li=0;li<net.loads.size();++li) {
            out.loads[li].drop=loadDrop(li,drops,hDrops);
            const auto &load=net.loads[li];
            bool excess=out.loads[li].drop>net.maxDrop;
            if(load.motor && positive(load.startMultiple) && positive(load.startPF) && positive(load.startSeconds)) {
                Flow start=injection;
                for(int n=0;n<net.nodes;++n)if(std::abs(byLoad[li].current[n])>0 || byLoad[li].h3[n]>0) {
                    const Complex current=byLoad[li].current[n];
                    const double shift=std::acos(net.supply==Supply::DC?1:load.pf)-std::acos(load.startPF);
                    const double multiple=load.startMultiple/load.demand;
                    start.current[n]+=current*(std::polar(multiple,shift)-Complex(1,0));
                    start.h3[n]+=byLoad[li].h3[n]*(multiple-1);
                }
                const auto starting=accumulated(start);
                out.loads[li].startDrop=loadDrop(li,nodeDrops(starting),harmonicDrops(starting));
                excess|=out.loads[li].startDrop>net.maxStartDrop;
                QSet<QString> checked;
                for(int ei:loadPaths[li])if(child[ei]>=0 && root[child[ei]]!=PE) {
                    const QString id=protectionAt[child[ei]];if(checked.contains(id))continue;checked.insert(id);
                    const auto pr=out.protections.value(id);const auto p=net.protections.value(id);
                    double peak=0;for(int n:order)if(protectionAt[n]==id)peak=std::max(peak,std::abs(starting.current[n]));
                    if(trip(p,pr.nominal,peak).first<=load.startSeconds)
                        conditioned("Partida pode atuar a proteção; exige curva/ajuste coordenado: "+load.id+" / "+id);
                }
            }
            if(excess) {
                int best=-1;double score=-1;
                for(int ei:loadPaths[li])if(index[ei]>=0 && root[child[ei]]!=PE &&
                   net.edges[ei].fixedSection==0 && index[ei]+1<profile(ei).table.size()) {
                    const double effect=std::abs(impedance(ei))*amps[ei];
                    if(effect>score){score=effect;best=ei;}
                }
                if(best<0){fail("Queda acumulada excedida e seções sem margem: "+load.id);failed=true;break;}
                grow.insert(best);
            }
        }
        // Check each physical segment against its OWN upstream protection.
        // A branch breaker must never be used to protect its incoming feeder.
        for(int ei=0;ei<net.edges.size();++ei)if(index[ei]>=0 && phase(Role(root[child[ei]]))) {
            const int n=child[ei];const auto &edge=net.edges[ei];const QString id=protectionAt[n];
            auto &pr=out.protections[id];const auto p=effectiveProtection(id);
            if(!positive(net.sourceR) || !positive(net.minVoltageFactor) || !positive(net.sourceFaultMax))continue;
            bool checked=false;
            for(int ret:cables.value(edge.cable))if(index[ret]>=0 && root[child[ret]]!=root[n]) {
                const Role returnRole=Role(root[child[ret]]);
                const bool earth=returnRole==PE;
                Complex loop(net.sourceR,net.sourceX);
                for(int pathEdge:path(n))loop+=impedance(pathEdge);
                for(int pathEdge:path(child[ret]))loop+=impedance(pathEdge);
                if(earth && net.earthing=="TT")loop+=Complex(p.earthResistance,0);
                const double v=std::abs(voltage(root[n])-voltage(returnRole));
                const double ik=v*net.minVoltageFactor/std::abs(loop);
                if(!positive(ik))continue;
                checked=true;
                if(ik>net.sourceFaultMax*(1+1e-6))conditioned("Curto máximo declarado menor que o mínimo calculado: "+edge.id);
                for(int li:downstream[ei])if(out.loads[li].faultMin==0 || ik<out.loads[li].faultMin)out.loads[li].faultMin=ik;
                double time=trip(p,pr.nominal,ik).second;
                const bool rcd=earth && net.earthing=="TT" && positive(p.rcdA) && ik>=p.rcdA &&
                    p.rcdRatedA>=pr.nominal && !p.rcdReference.isEmpty() && positive(p.earthResistance) &&
                    positive(p.touchLimit) && p.earthResistance*p.rcdA<=p.touchLimit && positive(p.rcdMaxSeconds);
                if(rcd)time=std::min(time,p.rcdMaxSeconds);
                const bool ads=positive(p.maxDisconnect) && time<=p.maxDisconnect;
                pr.disconnection&=ads;
                if(!ads) {
                    conditioned("Seccionamento automático não verificado: "+edge.id+" / "+id);
                    for(int candidate:{ei,ret})if(net.edges[candidate].fixedSection==0 &&
                       index[candidate]+1<profile(candidate).table.size())grow.insert(candidate);
                }
                // This is a conservative I²t envelope, not a guessed
                // let-through energy of a current-limiting breaker.
                if(positive(time) && time<=5) {
                    for(int candidate:{ei,ret}) {
                        const double minimum=net.sourceFaultMax*std::sqrt(time)/profile(candidate).k;
                        need[candidate]=std::max(need[candidate],minimum);const int old=index[candidate];
                        if(!select(candidate,need[candidate])) {fail("Critério adiabático excede tabela/seção manual: "+net.edges[candidate].id);failed=true;break;}
                        changed|=old!=index[candidate];
                    }
                } else {pr.thermal=false;conditioned("Curto: eliminação até 5 s não verificada: "+edge.id+" / "+id);}
            }
            if(!checked)conditioned("Retorno N/PE/outra fase não associado ao cabo para verificar falta: "+edge.cable);
        }
        if(failed)break;
        for(int ei:grow)if(index[ei]+1<profile(ei).table.size()){++index[ei];changed=true;}
        if(!changed){for(const auto &s:iterationPending)pending(s);solved=true;break;}
    }
    if(!solved && out.errors.isEmpty())fail("Dimensionamento não convergiu dentro da tabela declarada.");
    for(auto it=upstream.cbegin();it!=upstream.cend();++it)if(!it.value().isEmpty() && it.key()!=it.value()) {
        const auto up=net.protections.value(it.value()),down=net.protections.value(it.key());
        const auto u=out.protections.value(it.value()),d=out.protections.value(it.key());
        bool tested=false;
        for(const auto &pair:net.selectivity)if(pair.upstream==it.value() && pair.downstream==it.key() &&
            positive(net.sourceFaultMax) && pair.limitA>=net.sourceFaultMax && !pair.reference.isEmpty())tested=true;
        const bool curves=curveDiscrimination(up,u.nominal,down,d.nominal,net.sourceFaultMax);
        const QString label=it.value()+" → "+it.key();
        if(tested)out.coordination<<label+": seletividade até o curto máximo, por associação documentada.";
        else if(curves)out.coordination<<label+": envelopes separados na faixa declarada; revisar efeitos de limitação/cascata.";
        else pending("Seletividade não comprovada por curvas/associação: "+label);
    }
    for(int ei=0;ei<net.edges.size();++ei)if(index[ei]>=0) {
        WireResult w;const int n=child[ei];w.id=net.edges[ei].id;w.role=Role(root[n]);
        w.protection=protectionAt[n];w.current=amps[ei];w.section=profile(ei).table[index[ei]].section;
        w.ampacity=capacity(ei,index[ei]);w.neutralFundamental=std::abs(flow.current[n]);w.harmonic=flow.h3[n];
        w.shortCircuitMinimum=need[ei];
        QSet<int> phases;for(int peer:cables.value(net.edges[ei].cable))if(phase(Role(root[child[peer]])))phases.insert(root[child[peer]]);
        w.voltage=(net.supply==Supply::ThreePhase || net.supply==Supply::TwoPhase) && phases.size()==1?net.voltage/std::sqrt(3.):net.voltage;
        for(int li:downstream[ei])w.loads<<net.loads[li].id;
        w.loads.sort();out.wires<<w;
    }
    out.errors.removeDuplicates();out.pending.removeDuplicates();
    out.calculated=solved && out.errors.isEmpty();out.ready=out.calculated && out.pending.isEmpty();
    return out;
}
}
#endif
