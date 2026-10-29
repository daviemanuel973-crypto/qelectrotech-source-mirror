/* GPL-2.0-or-later. Native QET adapter for radial circuit sizing. */
#ifndef QET_PROJECT_CIRCUIT_SIZING_H
#define QET_PROJECT_CIRCUIT_SIZING_H
#include "radialsizing.h"
#include "electricaldata.h"
#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../cable/cable.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QQueue>

namespace ProjectCircuitSizing {
inline QString key() {return QStringLiteral("qet_radial_sizing");}
inline QString id(const QUuid &u){return u.toString(QUuid::WithoutBraces);}
inline QString terminalId(Terminal *t) {
    return t && t->parentElement()?id(t->parentElement()->uuid())+"/"+id(t->stableUuid()):QString();
}
inline double number(const QVariant &v,double fallback=0) {
    if(!v.isValid() || v.toString().trimmed().isEmpty())return fallback;
    bool ok=false;const double x=v.toString().trimmed().replace(',','.').toDouble(&ok);
    return ok && std::isfinite(x)?x:std::numeric_limits<double>::quiet_NaN();
}
inline DiagramContext information(Element *e,const QJsonObject &overrides={}) {
    auto info=e?e->elementInformations():DiagramContext();
    for(auto it=overrides.begin();it!=overrides.end();++it)info.addValue(it.key(),it.value().toVariant(),false);
    const auto rating=[&](const QString &key,const QString &pattern){
        const auto m=QRegularExpression(pattern).match(info.value(key).toString().trimmed());
        return m.hasMatch()?number(m.captured(1)):0.;
    };
    if(info.value("qet_voltage_v").toString().trimmed().isEmpty()) {
        const double v=rating("voltage_rating","^([0-9]+(?:[.,][0-9]+)?)\\s*[Vv](?:\\s*(?:AC|DC|CA|CC|ac|dc|ca|cc))?$");
        if(v>0)info.addValue("qet_voltage_v",QString::number(v,'g',12),false);
    }
    if(info.value("qet_current_a").toString().trimmed().isEmpty()) {
        const double amps=rating("current_rating","^([0-9]+(?:[.,][0-9]+)?)\\s*[Aa]$");
        if(amps>0)info.addValue("qet_current_a",QString::number(amps,'g',12),false);
    }
    if(info.value("qet_supply").toString().isEmpty() &&
       QRegularExpression("(?:DC|CC)\\s*$",QRegularExpression::CaseInsensitiveOption).match(info.value("voltage_rating").toString()).hasMatch())
        info.addValue("qet_supply","CC",false);
    return info;
}
inline QJsonObject settings(QETProject *p) {
    auto j=p?QJsonDocument::fromJson(p->projectProperties().value(key()).toString().toUtf8()).object():QJsonObject();
    if(j.isEmpty())j.insert("schema",1);return j;
}
inline QJsonObject profileJson(const RadialSizing::Profile &p) {
    QJsonArray table;for(const auto &r:p.table)table.append(QJsonArray{r.section,r.two,r.three,r.four});
    return {{"material",p.material},{"insulation",p.insulation},{"method",p.method},{"reference",p.reference},
        {"rho",p.resistivity},{"x_ohm_km",p.reactancePerKm},{"k",p.k},{"harmonic_factor",p.harmonicFactor},{"minimum_phase",p.minimumPhase},
        {"minimum_n",p.minimumNeutral},{"minimum_pe",p.minimumPE},{"confirmed",p.confirmed},{"table",table}};
}
inline RadialSizing::Profile profileFromJson(const QJsonObject &j) {
    RadialSizing::Profile p;p.material=j["material"].toString();p.insulation=j["insulation"].toString();
    p.method=j["method"].toString();p.reference=j["reference"].toString();p.confirmed=j["confirmed"].toBool();
    p.resistivity=j["rho"].toDouble();p.reactancePerKm=j["x_ohm_km"].toDouble();p.k=j["k"].toDouble();p.harmonicFactor=j["harmonic_factor"].toDouble();
    p.minimumPhase=j["minimum_phase"].toDouble();p.minimumNeutral=j["minimum_n"].toDouble();p.minimumPE=j["minimum_pe"].toDouble();
    for(const auto &v:j["table"].toArray()){const auto a=v.toArray();p.table<<RadialSizing::Ampacity{
        a.size()>0?a[0].toDouble():0,a.size()>1?a[1].toDouble():0,a.size()>2?a[2].toDouble():0,a.size()>3?a[3].toDouble():0};}
    return p;
}
inline RadialSizing::Profile preliminaryProfile() {
    RadialSizing::Profile p;p.material="Cu";p.insulation="PVC 70 °C";p.method="B1";
    p.reference="Perfil conservador herdado; conferir tabela aplicável e condições reais antes de confirmar";
    p.resistivity=.0225;p.k=115;p.minimumPhase=1.5;p.minimumNeutral=1.5;p.minimumPE=1.5;
    p.table={{1.5,17,15,0},{2.5,23,20,0},{4,30,27,0},{6,38,34,0},{10,52,46,0},{16,70,62,0},
        {25,90,80,0},{35,112,99,0},{50,134,118,0},{70,171,149,0},{95,207,179,0},{120,238,206,0}};
    return p;
}
inline RadialSizing::Protection protectionFromJson(const QString &id,const QJsonObject &j,const DiagramContext &info={}) {
    RadialSizing::Protection p;p.id=id;p.curve=j["curve"].toString();
    p.fixedIn=j.contains("in")?j["in"].toDouble():number(info.value("qet_protection_a"));
    p.breakingA=j["breaking_a"].toDouble();p.maxDisconnect=j["max_seconds"].toDouble();
    p.i2Multiple=j.contains("i2_multiple")?j["i2_multiple"].toDouble():
        (p.fixedIn>0?number(info.value("qet_trip_i2_a"))/p.fixedIn:0);p.reference=j["reference"].toString();
    p.rcdA=j["rcd_a"].toDouble();p.rcdMaxSeconds=j["rcd_seconds"].toDouble();p.rcdRatedA=j["rcd_rated_a"].toDouble();
    p.earthResistance=j["ra_ohm"].toDouble();p.touchLimit=j["ul_v"].toDouble();p.rcdReference=j["rcd_reference"].toString();
    p.neutralProtected=j["neutral_protected"].toBool();p.dcRated=j["dc_rated"].toBool();
    if(j.contains("ratings")){p.ratings.clear();for(const auto &v:j["ratings"].toArray())p.ratings<<v.toDouble();}
    for(const auto &v:j["points"].toArray()){const auto a=v.toArray();if(a.size()==3)p.points<<RadialSizing::CurvePoint{a[0].toDouble(),a[1].toDouble(),a[2].toDouble()};}
    return p;
}
inline bool protective(Element *e) {
    if(!e)return false;const auto declared=e->elementInformations().value("qet_electrical_role").toString();
    if(!declared.isEmpty())return declared=="protection";
    const QString s=(e->name()+" "+e->location().baseName()).toLower();
    return s.contains("breaker") || s.contains("disjuntor") || s.contains("fuse") || s.contains("fusível");
}
inline QString label(Element *e) {
    return e?QStringLiteral("%1 — %2 / pág. %3").arg(SymbolLegend::documentedDesignator(e),e->name())
        .arg(e->diagram()?e->diagram()->folioIndex()+1:0):QString();
}
inline QString roleKey(int role) {return QStringList{"L1","L2","L3","N","PE","+","-"}.value(role);}
inline QString passageId(const WireTrace::DevicePassage &p) {
    const auto ta=terminalId(p.terminalA),tb=terminalId(p.terminalB);
    return ta<tb?ta+"|"+tb:tb+"|"+ta;
}
struct Analysis {
    RadialSizing::Network network;
    RadialSizing::Result result;
    QHash<QString,QPointer<Conductor>> wires;
    QHash<QString,QPointer<Element>> elements;
    QHash<QString,QString> labels;
    QString fingerprint;
    QSet<QString> domainWires,domainLoads;
    int omittedLoads=0;
};
inline Analysis analyze(QETProject *project,const QJsonObject &data,bool overwrite=false) {
    using namespace RadialSizing;
    Analysis a;auto &net=a.network;
    if(!project || data.value("schema").toInt()!=1){a.result.errors<<"Configuração de dimensionamento inválida.";return a;}
    const auto trace=WireTrace::scan(project);
    QList<Element*> elements;QList<Conductor*> wires;
    QMap<QString,Terminal*> sortedNodes;
    for(Diagram *d:project->diagrams()) {
        if(!d || d->border_and_titleblock.additionalFields().value("qet_generated_documentation").toBool())continue;
        elements<<d->elements();wires<<d->conductors();
        for(Element *e:d->elements()) {
            const QString eid=id(e->uuid());a.elements.insert(eid,e);a.labels.insert(eid,label(e));
            for(Terminal *t:e->terminals()) {
                const auto tid=terminalId(t);
                if(sortedNodes.contains(tid))a.result.errors<<"Identidade de terminal duplicada: "+tid;
                sortedNodes.insert(tid,t);
            }
        }
    }
    QHash<Terminal*,int> node;QHash<QString,int> indexById;QVector<QString> nodeIds;
    for(auto it=sortedNodes.cbegin();it!=sortedNodes.cend();++it) {
        node.insert(it.value(),nodeIds.size());indexById.insert(it.key(),nodeIds.size());nodeIds<<it.key();
    }
    net.nodes=nodeIds.size();
    const QString sourceId=data["source"].toString();Element *source=a.elements.value(sourceId);
    if(!source){a.result.errors<<"Escolha a origem da alimentação e seus terminais de saída.";return a;}
    const auto cfg=data["sources"].toObject()[sourceId].toObject();
    const auto si=information(source);
    const QString supply=cfg.value("supply").toString(si.value("qet_supply").toString());
    net.supply=supply=="1F"?Supply::SinglePhase:supply=="2F"?Supply::TwoPhase:supply=="CC"?Supply::DC:Supply::ThreePhase;
    if(supply!="1F" && supply!="2F" && supply!="3F" && supply!="CC")a.result.errors<<"Declare alimentação 1F, 2F (120°), 3F ou CC na origem.";
    net.voltage=cfg.contains("voltage")?cfg["voltage"].toDouble():number(si.value("qet_voltage_v"));
    net.maxDrop=cfg["drop"].toDouble();net.maxStartDrop=cfg["start_drop"].toDouble();
    net.frequencyHz=cfg["frequency_hz"].toDouble();
    net.sourceR=cfg["source_r"].toDouble();net.sourceX=cfg["source_x"].toDouble();
    net.sourceFaultMax=cfg["fault_max"].toDouble();net.minVoltageFactor=cfg["cmin"].toDouble();
    net.earthing=cfg.value("earthing").toString("TN");net.stateConfirmed=cfg["contacts_confirmed"].toBool();
    const auto roots=cfg["roots"].toObject();
    for(int r=0;r<RoleCount;++r) {
        const QString tid=roots[roleKey(r)].toString();
        if(!tid.isEmpty() && (!indexById.contains(tid) || !source->terminals().contains(sortedNodes.value(tid))))
            a.result.errors<<"Terminal de origem não pertence ao componente escolhido: "+roleKey(r);
        net.roots[r]=indexById.value(tid,-1);
        if(sortedNodes.contains(tid)) {
            QString mark=sortedNodes[tid]->name().trimmed().toUpper();if(mark=="−")mark="-";
            if(QStringList{"L1","L2","L3","N","PE","+","-"}.contains(mark) && mark!=roleKey(r))
                a.result.errors<<"Função escolhida contradiz a marcação do terminal da origem: "+mark;
        }
    }
    const auto profiles=data["profiles"].toObject();
    net.profile=profiles.contains("default")?profileFromJson(profiles["default"].toObject()):preliminaryProfile();
    for(auto it=profiles.begin();it!=profiles.end();++it)net.profiles.insert(it.key(),profileFromJson(it.value().toObject()));
    const auto prot=data["protections"].toObject();net.sourceProtection="origem:"+sourceId;
    net.protections.insert(net.sourceProtection,protectionFromJson(net.sourceProtection,prot[net.sourceProtection].toObject(),si));
    a.labels.insert(net.sourceProtection,"Proteção da origem — "+label(source));
    for(Element *e:elements)if(e!=source && protective(e)) {
        const QString eid=id(e->uuid());net.protections.insert(eid,protectionFromJson(eid,prot[eid].toObject(),e->elementInformations()));
    }
    for(const auto &v:data["selectivity"].toArray()) {
        const auto p=v.toObject();net.selectivity<<Selectivity{p["upstream"].toString(),p["downstream"].toString(),
            p["reference"].toString(),p["limit_a"].toDouble()};
    }
    QSet<QString> joins;
    for(auto it=node.cbegin();it!=node.cend();++it)for(Terminal *peer:relatedPotentialTerminal(it.key(),true))if(node.contains(peer)) {
        const int lo=std::min(it.value(),node[peer]),hi=std::max(it.value(),node[peer]);
        const QString jid="join:"+nodeIds[lo]+"|"+nodeIds[hi];if(joins.contains(jid))continue;joins.insert(jid);
        Edge e;e.id=jid;e.a=lo;e.b=hi;e.kind=EdgeKind::Join;net.edges<<e;
    }
    QSet<QString> confirmed;
    for(const auto &v:data["confirmed_pairs"].toArray())confirmed.insert(v.toString());
    QHash<Terminal*,int> passageDegree;
    for(const auto &p:trace.passages)if(p.device!=source){++passageDegree[p.terminalA];++passageDegree[p.terminalB];}
    QSet<Element*> passedDevices;
    QHash<Terminal*,int> selectedCommon;
    for(const auto &p:trace.passages) {
        if(p.device==source || !node.contains(p.terminalA) || !node.contains(p.terminalB))continue;
        const bool exclusive=passageDegree.value(p.terminalA)>1 || passageDegree.value(p.terminalB)>1;
        const QString pid=passageId(p);
        if((!p.traversable || exclusive) && !confirmed.contains(pid))continue;
        // A user may confirm an electrical terminal pair, but cannot turn
        // a load, winding or converter into a conducting contact.
        if(ElectricalData::load(p.device) || p.behaviour.startsWith("CARGA") || p.behaviour.startsWith("CONVERSÃO"))continue;
        if(exclusive) {
            for(Terminal *t:{p.terminalA,p.terminalB})if(passageDegree.value(t)>1 && ++selectedCommon[t]>1)
                a.result.errors<<"Contatos mutuamente exclusivos confirmados simultaneamente: "+label(p.device);
        }
        Edge e;e.id="device:"+pid;e.a=node[p.terminalA];e.b=node[p.terminalB];e.kind=EdgeKind::Device;
        e.peContinuous=p.device && p.device->elementInformations().value("qet_electrical_role").toString()=="terminal" && confirmed.contains(pid);
        if(protective(p.device))e.protection=id(p.device->uuid());net.edges<<e;passedDevices.insert(p.device);
    }
    const auto inputWires=data["wires"].toObject();QHash<QString,int> coreOccurrences;
    QHash<QUuid,Cable*> nativeCables;for(Cable *c:project->cables())if(c)nativeCables.insert(c->uuid(),c);
    for(Conductor *c:wires){const auto p=c->properties();if(!p.m_cable_uuid.isNull() && p.m_cable_slot>=0)
        ++coreOccurrences[id(p.m_cable_uuid)+":"+QString::number(p.m_cable_slot)];}
    for(Conductor *c:wires) {
        const QString cid=id(c->uuid());a.wires.insert(cid,c);
        a.labels.insert(cid,QStringLiteral("%1 / pág. %2: %3 → %4").arg(c->properties().text)
            .arg(c->diagram()?c->diagram()->folioIndex()+1:0)
            .arg(WireTrace::terminalDescription(c->terminal1),WireTrace::terminalDescription(c->terminal2)));
        if(!node.contains(c->terminal1) || !node.contains(c->terminal2)) {a.result.errors<<"Extremidade inválida: "+cid;continue;}
        const auto w=inputWires[cid].toObject();const auto p=c->properties();
        Edge e;e.id=cid;e.a=node[c->terminal1];e.b=node[c->terminal2];e.length=w["length"].toDouble();
        e.cable=w["cable"].toString();e.temperatureFactor=w["kt"].toDouble();e.groupingFactor=w["kg"].toDouble();
        e.loaded=w["loaded"].toInt();e.minimum=w["minimum"].toDouble();e.profile=w["profile"].toString();
        if(e.cable.isEmpty() && !p.m_cable_uuid.isNull())e.cable="cabo:"+id(p.m_cable_uuid);
        if(e.length==0 && nativeCables.contains(p.m_cable_uuid) && p.m_cable_slot>=0 &&
           coreOccurrences.value(id(p.m_cable_uuid)+":"+QString::number(p.m_cable_slot))==1) {
            const QString text=nativeCables[p.m_cable_uuid]->length().trimmed();
            const auto match=QRegularExpression("^([0-9]+(?:[.,][0-9]+)?)\\s*(m|km)?$",QRegularExpression::CaseInsensitiveOption).match(text);
            if(match.hasMatch())e.length=number(match.captured(1))*(match.captured(2).compare("km",Qt::CaseInsensitive)==0?1000:1);
        }
        const auto previous=QJsonDocument::fromJson(p.m_sizing_data.toUtf8()).object();
        const bool automatic=previous["schema"].toInt()==2 && previous["source"].toString()==sourceId &&
            previous["section"].toString()==p.m_wire_section && !previous["manual_section"].toBool();
        if(!overwrite && !automatic && !p.m_wire_section.trimmed().isEmpty()) {
            const auto match=QRegularExpression("^\\s*([0-9]+(?:[.,][0-9]+)?)\\s*(?:mm(?:²|2))?\\s*$").match(p.m_wire_section);
            if(match.hasMatch())e.fixedSection=number(match.captured(1));
            else e.fixedSection=std::numeric_limits<double>::quiet_NaN();
        }
        net.edges<<e;
    }
    QVector<QVector<int>> graph(net.nodes);
    for(const auto &e:net.edges)if(e.a>=0 && e.b>=0){graph[e.a]<<e.b;graph[e.b]<<e.a;}
    QSet<int> reachable;QQueue<int> queue;
    for(int n:net.roots)if(n>=0){reachable.insert(n);queue.enqueue(n);}
    while(!queue.isEmpty())for(int other:graph[queue.dequeue()])if(!reachable.contains(other)){reachable.insert(other);queue.enqueue(other);}
    QSet<int> activeReach;for(int r:{int(L1),int(L2),int(L3),int(Plus)})if(net.roots[r]>=0){activeReach.insert(net.roots[r]);queue.enqueue(net.roots[r]);}
    while(!queue.isEmpty())for(int other:graph[queue.dequeue()])if(!activeReach.contains(other)){activeReach.insert(other);queue.enqueue(other);}
    auto reached=[&](Element *e){for(Terminal *t:e->terminals())if(activeReach.contains(node.value(t,-1)))return true;return false;};
    const auto loadOverrides=data["loads"].toObject();
    QJsonObject signatureLoads;
    for(Element *e:elements) {
        if(e==source)continue;const QString eid=id(e->uuid());
        const auto override=loadOverrides[eid].toObject();
        auto info=information(e,override);
        const bool isLoad=ElectricalData::load(e);
        if(!reached(e)){if(isLoad)++a.omittedLoads;continue;}
        if(isLoad) {
            Load l;l.id=eid;l.voltage=number(info.value("qet_voltage_v"));l.current=number(info.value("qet_current_a"));
            l.power=number(info.value("qet_power_w"));l.pf=number(info.value("qet_power_factor"));
            l.efficiency=number(info.value("qet_efficiency"));l.mechanical=info.value("qet_power_kind").toString()=="mechanical";
            l.harmonic3=number(info.value("qet_harmonic3_percent"),-100)/100.;
            const auto spectrumText=info.value("qet_harmonic_spectrum").toString().trimmed();
            if(!spectrumText.isEmpty()) {
                QJsonParseError error;const auto document=QJsonDocument::fromJson(spectrumText.toUtf8(),&error);
                if(error.error!=QJsonParseError::NoError || !document.isArray())a.result.errors<<"Espectro harmônico inválido: "+label(e);
                for(const auto &v:document.array()) {
                    const auto row=v.toArray();const int h=row.size()>0?row[0].toInt():0;
                    if(row.size()!=2 || l.spectrum.contains(h))a.result.errors<<"Ordem harmônica inválida/duplicada: "+label(e);
                    l.spectrum[h]=row.size()>1?row[1].toDouble()/100.:0;
                }
            }
            l.demand=number(info.value("qet_demand_factor"),1);
            const QString deviceName=e->name().toLower();l.motor=deviceName.contains("motor") || info.value("qet_load_kind").toString()=="motor";
            l.startMultiple=number(info.value("qet_start_multiple"));l.startSeconds=number(info.value("qet_start_seconds"));
            l.startPF=number(info.value("qet_start_pf"));
            const QString protectionClass=info.value("qet_protection_class").toString();
            l.requiresPE=protectionClass!="II" && protectionClass!="III";
            bool markedPE=false;
            for(Terminal *t:e->terminals()) {
                const QString mark=t->name().trimmed().toUpper();
                if(mark=="PE" || mark=="P.E."){l.pe=node[t];markedPE=true;}
                else l.ports<<node[t];
            }
            if((info.value("qet_supply").toString()=="3F" && l.ports.size()<3) ||
               ((info.value("qet_supply").toString()=="1F" || info.value("qet_supply").toString()=="CC") && l.ports.size()!=2))
                a.result.errors<<"Número de terminais não corresponde à alimentação declarada: "+label(e);
            if(protectionClass.isEmpty() && !markedPE)a.result.errors<<"Declare classe de proteção I/II/III da carga: "+label(e);
            const QStringList currentKeys{"qet_phase_l1_a","qet_phase_l2_a","qet_phase_l3_a"};
            for(int k=0;k<3;++k)l.phaseCurrents[k]=number(info.value(currentKeys[k]),-1);
            if(info.value("qet_supply").toString()=="CC" && net.supply!=Supply::DC)
                a.result.errors<<"Carga CC ligada ao domínio CA: "+label(e);
            if(info.value("qet_supply").toString()!="CC" && !info.value("qet_supply").toString().isEmpty() && net.supply==Supply::DC)
                a.result.errors<<"Carga CA ligada ao domínio CC: "+label(e);
            net.loads<<l;QJsonObject raw;
            a.domainLoads.insert(eid);
            for(const QString &k:QStringList{"qet_voltage_v","qet_current_a","qet_power_w","qet_supply","qet_power_factor", "qet_efficiency",
                "qet_harmonic3_percent","qet_power_kind","qet_demand_factor","qet_load_kind","qet_protection_class", "qet_start_multiple",
                "qet_start_seconds","qet_start_pf","qet_phase_l1_a","qet_phase_l2_a","qet_phase_l3_a","qet_harmonic_spectrum"})raw[k]=info.value(k).toString();
            signatureLoads.insert(eid,raw);
        } else if(!(e->linkType() & (Element::AllReport|Element::Terminale)) && !passedDevices.contains(e))
            a.result.errors<<"Dispositivo intermediário/fonte sem modelo elétrico confirmado: "+label(e);
    }
    for(const auto &e:net.edges)if(e.kind==EdgeKind::Wire && (reachable.contains(e.a) || reachable.contains(e.b))) {
        a.domainWires.insert(e.id);
        Conductor *c=a.wires.value(e.id);const int group=trace.groupByConductor.value(c,-1);
        if(!trace.canSizeGroup(group))a.result.errors<<"Conectividade inconsistente no trecho: "+a.labels.value(e.id);
        if(!std::isfinite(e.fixedSection))a.result.errors<<"Seção manual não interpretável: "+a.labels.value(e.id);
    }
    if(!a.result.errors.isEmpty()){a.result.errors.removeDuplicates();return a;}
    a.result=calculate(net);
    // A return shared with another, uncalculated source cannot inherit only
    // this domain's current. A common PE alone does not merge supply domains.
    for(int returnRole:{int(N),int(Minus)})if(net.roots[returnRole]>=0) {
        QVector<int> prev(net.nodes,-1),previousEdge(net.nodes,-1);QQueue<int> q;
        const int start=net.roots[returnRole];prev[start]=start;q.enqueue(start);
        while(!q.isEmpty()) {
            const int n=q.dequeue();for(int ei=0;ei<net.edges.size();++ei) {
                const auto &e=net.edges[ei];const int next=e.a==n?e.b:e.b==n?e.a:-1;
                if(next<0 || prev[next]>=0)continue;prev[next]=n;previousEdge[next]=ei;q.enqueue(next);
            }
        }
        QSet<QString> usedReturns;
        for(const auto &w:a.result.wires)if(w.role==returnRole)usedReturns.insert(w.id);
        for(Element *e:elements)if(ElectricalData::load(e) && !a.domainLoads.contains(id(e->uuid())))
            for(Terminal *t:e->terminals()) {
                int n=node.value(t,-1);QSet<int> visited;
                while(n>=0 && n!=start && prev[n]>=0 && !visited.contains(n)) {
                    visited.insert(n);const int ei=previousEdge[n];if(ei>=0 && usedReturns.contains(net.edges[ei].id)) {
                        a.result.errors<<"Retorno compartilhado com carga fora da origem selecionada; use a origem comum: "+label(e);break;
                    }n=prev[n];
                }
            }
    }
    for(const auto &w:a.result.wires) {
        const auto c=a.wires.value(w.id);if(!c)continue;
        const QString mark=c->properties().m_function.trimmed().toUpper();
        if(QStringList{"L1","L2","L3","N","PE","+","-","−"}.contains(mark) &&
           mark!=roleName(w.role) && !(mark=="-" && w.role==Minus))
            a.result.errors<<"Função declarada do fio contradiz a origem rastreada: "+a.labels.value(w.id);
    }
    if(!a.result.errors.isEmpty())a.result.ready=false;
    QJsonObject input=data;input.remove("loads");input.remove("audit");input.remove("results");input.remove("last_auto_issues");
    QJsonObject topology;
    for(const auto &e:net.edges)topology.insert(e.id,QJsonArray{nodeIds[e.a],nodeIds[e.b],int(e.kind),e.peContinuous});
    input["topology"]=topology;input["load_data"]=signatureLoads;
    input["algorithm_version"]=2;
    a.fingerprint=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(input).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    return a;
}
inline QString friendly(const Analysis &a,QString text) {
    auto keys=a.labels.keys();std::sort(keys.begin(),keys.end(),[](const QString &x,const QString &y){return x.size()>y.size();});
    for(const auto &k:keys)if(!k.isEmpty())text.replace(k,a.labels.value(k));return text;
}
class StoreSettings:public QUndoCommand {
    QPointer<QETProject> project;QVariant before;bool hadBefore;QString after;
    void put(const QVariant &value,bool present){if(project){auto p=project->projectProperties();
        if(present)p.addValue(key(),value,false);else p.remove(key());project->setProjectProperties(p);project->setModified(true);}}
public:
    StoreSettings(QETProject *p,const QJsonObject &data,QUndoCommand *parent=nullptr):QUndoCommand(parent),project(p),
        before(p->projectProperties().value(key())),hadBefore(p->projectProperties().contains(key())),
        after(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))) {setText("Dados de dimensionamento da alimentação");}
    void redo()override{put(after,true);}void undo()override{put(before,hadBefore);}
};
inline QJsonObject persistentData(QJsonObject data){data.remove("loads");return data;}
inline void addLoadEdits(QETProject *p,const QJsonObject &data,QUndoCommand *batch) {
    const auto overrides=data["loads"].toObject();
    for(Diagram *d:p->diagrams())for(Element *e:d->elements())if(overrides.contains(id(e->uuid()))) {
        auto info=e->elementInformations();const auto fields=overrides[id(e->uuid())].toObject();
        for(auto it=fields.begin();it!=fields.end();++it)info.addValue(it.key(),it.value().toVariant(),false);
        if(info!=e->elementInformations())new ElectricalData::SetData(e,info,batch);
    }
}
inline bool saveParameters(QETProject *project,const QJsonObject &data) {
    if(!project || project->isReadOnly() || data.value("schema").toInt()!=1 || !project->undoStack())return false;
    auto *batch=new QUndoCommand("Cadastrar parâmetros da alimentação");addLoadEdits(project,data,batch);
    new StoreSettings(project,persistentData(data),batch);project->undoStack()->push(batch);return true;
}
inline bool currentResult(const Analysis &a,const QString &source,const QString &fingerprint) {
    if(!a.result.ready || a.fingerprint!=fingerprint || fingerprint.isEmpty())return false;
    for(const auto &w:a.result.wires) {
        const auto c=a.wires.value(w.id);if(!c)return false;
        const auto p=c->properties();const auto record=QJsonDocument::fromJson(p.m_sizing_data.toUtf8()).object();
        if(record["schema"].toInt()!=2 || record["source"].toString()!=source ||
           record["fingerprint"].toString()!=fingerprint || record["section"].toString()!=p.m_wire_section ||
           std::abs(number(p.m_wire_section.section(' ',0,0))-w.section)>1e-8)return false;
    }return true;
}
inline void applicationCommands(QETProject *project,const QJsonObject &data,const Analysis &a,
                                bool overwrite,QUndoCommand *batch,QJsonObject &saved) {
    using namespace RadialSizing;
    auto audits=saved["audit"].toObject();audits[data["source"].toString()]=a.fingerprint;saved["audit"]=audits;
    QJsonObject summary;QJsonArray wireRows,loadRows,protectionRows;
    for(const auto &w:a.result.wires)wireRows<<QJsonObject{{"id",w.id},{"reference",a.labels.value(w.id)},
        {"role",RadialSizing::roleName(w.role)},{"current",w.current},{"section",w.section},{"iz",w.ampacity}};
    for(const auto &l:a.result.loads)loadRows<<QJsonObject{{"id",l.id},{"reference",a.labels.value(l.id)},
        {"power",l.power},{"drop",l.drop},{"start_drop",l.startDrop},{"fault_min",l.faultMin}};
    for(const auto &p:a.result.protections)protectionRows<<QJsonObject{{"id",p.id},{"reference",a.labels.value(p.id)},
        {"current",p.current},{"in",p.nominal},{"i2",p.i2},{"breaking",p.breaking},{"thermal",p.thermal},{"ads",p.disconnection}};
    summary={{"fingerprint",a.fingerprint},{"source",a.labels.value(data["source"].toString())},{"wires",wireRows},
        {"loads",loadRows},{"protections",protectionRows}};
    QJsonArray coordination;for(const auto &line:a.result.coordination)coordination<<friendly(a,line);summary["coordination"]=coordination;
    auto summaries=saved["results"].toObject();summaries[data["source"].toString()]=summary;saved["results"]=summaries;
    const auto &net=a.network;
    for(const auto &w:a.result.wires)if(Conductor *c=a.wires.value(w.id)) {
        const auto before=c->properties();auto after=before;
        after.m_wire_section=QString::number(w.section,'g',12).replace('.',',')+" mm²";
        RadialSizing::Edge edge;for(const auto &e:net.edges)if(e.id==w.id)edge=e;
        const auto p=net.profiles.value(edge.profile,net.profile);
        QJsonObject record{{"schema",2},{"algorithm_version",2},{"source",data["source"]},{"fingerprint",a.fingerprint},
            {"section",after.m_wire_section},{"current",w.current},{"iz_a",w.ampacity},{"role",roleName(w.role)},
            {"protection",w.protection},{"in_a",a.result.protections.value(w.protection).nominal},
            {"short_circuit_minimum_mm2",w.shortCircuitMinimum},{"harmonic_a",w.harmonic},
            {"voltage",w.voltage},
            {"manual_section",edge.fixedSection>0},
            {"profile",p.reference},{"criteria_checked",true}};
        after.m_sizing_data=QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact));
        if(after.m_function.isEmpty() || overwrite)after.m_function=roleName(w.role);
        if(after.m_wire_color.isEmpty() || overwrite)after.m_wire_color=w.role==PE?"Verde/amarelo":w.role==N?"Azul-claro":"Preto";
        if(w.role!=PE && (after.m_tension_protocol.isEmpty() || overwrite))after.m_tension_protocol=QString::number(w.voltage,'g',12)+" V";
        if(before!=after)new QPropertyUndoCommand(c,"properties",QVariant::fromValue(before),QVariant::fromValue(after),batch);
    }
}
inline bool apply(QETProject *project,const QJsonObject &data,bool overwrite,QStringList *issues=nullptr) {
    if(!project || project->isReadOnly() || !project->undoStack())return false;
    const auto a=analyze(project,data,overwrite);
    if(!a.result.ready){if(issues)*issues=a.result.errors+a.result.pending;return false;}
    auto *batch=new QUndoCommand("Dimensionar alimentador, derivações, neutro e PE");
    addLoadEdits(project,data,batch);QJsonObject saved=persistentData(data);
    applicationCommands(project,data,a,overwrite,batch,saved);new StoreSettings(project,saved,batch);
    project->undoStack()->push(batch);return true;
}
inline QString report(const Analysis &a) {
    QStringList out;out<<"DIMENSIONAMENTO DA ALIMENTAÇÃO E DERIVAÇÕES";
    out<<QStringLiteral("%1 cargas calculadas; %2 cargas fora deste domínio de alimentação.")
        .arg(a.result.loads.size()).arg(a.omittedLoads);
    out<<"Resultado condicionado ao perfil declarado; não é certificação integral da instalação.";
    out<<"Calibres automáticos são propostas: confirme os dispositivos/ajustes da instalação antes de usar as seções.";
    for(const auto &e:a.result.errors)out<<"ERRO: "+friendly(a,e);
    for(const auto &e:a.result.pending)out<<"PENDENTE: "+friendly(a,e);
    for(const auto &w:a.result.wires)out<<QStringLiteral("%1 | %2 | Ib=%3 A | S=%4 mm² | Iz=%5 A | proteção %6 | cargas %7")
        .arg(a.labels.value(w.id,w.id),RadialSizing::roleName(w.role)).arg(w.current,0,'g',8)
        .arg(w.section,0,'g',8).arg(w.ampacity,0,'g',8).arg(a.labels.value(w.protection,w.protection)).arg(w.loads.size());
    for(const auto &l:a.result.loads)out<<QStringLiteral("%1 | P=%2 W | queda acumulada=%3% | partida=%4% | Ik mín=%5 A")
        .arg(a.labels.value(l.id,l.id)).arg(l.power,0,'g',8).arg(l.drop,0,'g',8).arg(l.startDrop,0,'g',8).arg(l.faultMin,0,'g',8);
    for(const auto &p:a.result.protections)out<<QStringLiteral("PROTEÇÃO %1 | Ib=%2 A | In=%3 A | I2=%4 A | interrupção=%5 | térmico=%6 | seccionamento=%7")
        .arg(a.labels.value(p.id,p.id)).arg(p.current).arg(p.nominal).arg(p.i2)
        .arg(p.breaking?"verificado":"pendente",p.thermal?"verificado":"pendente",p.disconnection?"verificado":"pendente");
    for(const auto &line:a.result.coordination)out<<friendly(a,line);
    out<< (a.result.ready?"Critérios implementados verificados para este domínio; aplicação disponível.":"Há pendências: seções não serão aplicadas.");
    return out.join('\n');
}
struct AllAnalysis {
    QMap<QString,Analysis> domains;
    QStringList issues;
    bool ready=false;
};
inline AllAnalysis analyzeAll(QETProject *project,const QJsonObject &data,bool overwrite=false) {
    AllAnalysis all;QSet<QString> covered,wires;
    for(const QString &source:data["sources"].toObject().keys()) {
        auto d=data;d["source"]=source;auto a=analyze(project,d,overwrite);
        if(!a.result.ready)all.issues<<"Origem "+a.labels.value(source,source)+": "+(a.result.errors+a.result.pending).join("; ");
        covered.unite(a.domainLoads);
        for(const auto &w:a.result.wires) {
            if(wires.contains(w.id))all.issues<<"Trecho compartilhado por origens: revisar alimentação comum antes de aplicar: "+a.labels.value(w.id,w.id);
            wires.insert(w.id);
        }
        all.domains.insert(source,a);
    }
    if(project)for(Diagram *d:project->diagrams())if(!d->border_and_titleblock.additionalFields().value("qet_generated_documentation").toBool())
        for(Element *e:d->elements())if(ElectricalData::load(e) && !covered.contains(id(e->uuid())))
            all.issues<<"Carga fora das origens cadastradas: "+label(e);
    if(all.domains.isEmpty())all.issues<<"Nenhuma origem cadastrada.";
    all.ready=all.issues.isEmpty();return all;
}
inline bool applyAll(QETProject *project,const QJsonObject &data,bool overwrite,QStringList *issues=nullptr) {
    if(!project || project->isReadOnly() || !project->undoStack())return false;
    const auto all=analyzeAll(project,data,overwrite);
    if(!all.ready){if(issues)*issues=all.issues;return false;}
    auto *batch=new QUndoCommand("Dimensionar todas as alimentações e derivações cadastradas");
    addLoadEdits(project,data,batch);auto saved=persistentData(data);
    for(auto it=all.domains.cbegin();it!=all.domains.cend();++it) {
        auto d=data;d["source"]=it.key();applicationCommands(project,d,it.value(),overwrite,batch,saved);
    }
    new StoreSettings(project,saved,batch);project->undoStack()->push(batch);return true;
}
inline bool refreshBeforeSave(QETProject *project) {
    auto data=settings(project);
    if(!data["auto_recalculate"].toBool() || !project || project->isReadOnly())return false;
    const auto all=analyzeAll(project,data,false);QStringList issues=all.issues;
    if(all.ready) {
        bool fresh=true;
        for(auto it=all.domains.cbegin();it!=all.domains.cend();++it)
            fresh&=currentResult(it.value(),it.key(),data["audit"].toObject()[it.key()].toString());
        if(!fresh) {
            data.remove("last_auto_issues");
            if(applyAll(project,data,false,&issues))return true;
        }
    }
    const auto warnings=QJsonArray::fromStringList(issues);
    if(data["last_auto_issues"].toArray()!=warnings) {
        data["last_auto_issues"]=warnings;
        project->undoStack()->push(new StoreSettings(project,data));
        return true;
    }
    return false;
}
}
#endif
