/* QElectroTech - read-only electrical graph audit.
 * Copyright 2026. GPL-2.0-or-later.
 *
 * A graph connection does NOT prove an operating voltage, load current or
 * cable size. The report is an inventory and never changes the project.
 */
#ifndef QET_WIRETRACE_H
#define QET_WIRETRACE_H
#include "../qetproject.h"
#include "../diagram.h"
#include "../diagramcontent.h"
#include "../diagramcontext.h"
#include "../qetgraphicsitem/element.h"
#include "../qetgraphicsitem/terminal.h"
#include "../qetgraphicsitem/conductor.h"
#include <QHash>
#include <QSet>
#include <QList>
#include <QStringList>
#include <QMessageBox>
#include <QInputDialog>
#include <QRegularExpression>
#include <QQueue>
#include <algorithm>
#include <utility>

namespace WireTrace {

inline QString shortUuid(const QUuid &id) {
    return id.toString(QUuid::WithoutBraces).left(10);
}
inline QString safeText(const QString &s) {
    QString t=s.simplified();
    if (t.isEmpty()) return QStringLiteral("(não informado)");
    return t.left(110);
}
struct PotentialGroup {
    QList<Conductor *> conductors;
    QSet<Diagram *> folios;
    QSet<Element *> adjacentElements;
    QSet<Terminal *> terminals;
    QStringList declaredData;
    QStringList endpoints;
    QStringList wireIds;
    bool hasUnconnectedEndpoint = false;
    bool spansFolios() const { return folios.size() > 1; }
};
struct DevicePassage {
    int a=-1, b=-1;
    QString elementId, label, pinA, pinB, behaviour;
    QString page;
    bool traversable=true;
};
struct ReportHop {
    int a=-1, b=-1;
    QString source, destination, fromPage, toPage;
};
struct Snapshot {
    int folios = 0;
    int elements = 0;
    int terminals = 0;
    int wires = 0;
    int openTerminals = 0;
    int invalidEndpoints = 0;
    int multiFolioPotentials = 0;
    int elementsLinkingPotentials = 0;
    int unmatchedReports = 0;
    int unsupportedMultiTerminal = 0;
    int recoveredThreePolePassages = 0;
    int recoveredChangeoverPassages = 0;
    QList<DevicePassage> passages;
    QList<ReportHop> reportHops;
    QSet<int> unsafeGroups; // broken folio links / missing wire endpoints
    QList<PotentialGroup> groups;
    QHash<Conductor *,int> groupByConductor;
    QStringList elementInventory;
    QStringList componentConnections;
    QStringList folioInventory;
    QStringList warnings;
    bool complete = false;
    bool canSizeGroup(int id) const {
        return id>=0 && id<groups.size() && !unsafeGroups.contains(id) &&
               !groups.at(id).hasUnconnectedEndpoint;
    }
    // A physically plausible route through terminal pairs is not proof that
    // contacts are closed. We document every crossing and its uncertainty.
    QString routeBetween(int from, int to) const {
        if (from<0 || to<0 || from>=groups.size() || to>=groups.size())
            return QStringLiteral("Origem ou destino fora do grafo rastreado.");
        QStringList out;
        out << QStringLiteral("ROTA ENTRE POTENCIAIS %1 e %2")
                    .arg(from+1).arg(to+1);
        out << QStringLiteral("Arestas entre dispositivos não são emendas; "
                              "contatos e conversores podem interromper energia.");
        if (from==to) {
            out << QStringLiteral("Os dois fios pertencem ao MESMO potencial (%1).")
                        .arg(from+1);
            for (const ReportHop &hop : reportHops)
                if (hop.a==from || hop.b==from)
                    out << QStringLiteral("  CONTINUAÇÃO DE FOLHA: %1 (pág.%2) → %3 (pág.%4)")
                        .arg(hop.source,hop.fromPage,hop.destination,hop.toPage);
            return out.join(QLatin1Char('\n'));
        }
        QQueue<int> queue;
        QHash<int,int> previousNode;
        QHash<int,int> previousArc;
        queue.enqueue(from);
        previousNode.insert(from,-1);
        while(!queue.isEmpty() && !previousNode.contains(to)) {
            const int node=queue.dequeue();
            for(int idx=0;idx<passages.size();++idx) {
                const DevicePassage &arc=passages.at(idx);
                const int next=arc.a==node ? arc.b : arc.b==node ? arc.a : -1;
                if(next<0 || previousNode.contains(next) || !arc.traversable)
                    continue;
                previousNode.insert(next,node);
                previousArc.insert(next,idx);
                queue.enqueue(next);
            }
        }
        if(!previousNode.contains(to)) {
            out << QStringLiteral(
                "SEM ROTA: nenhum encadeamento de terminais identificados "
                "liga esses potenciais. Verifique pares de terminais, bornes "
                "sem nome, setas sem vínculo e componentes multipolares.");
            return out.join(QLatin1Char('\n'));
        }
        QList<int> steps;
        for(int node=to;node!=from;node=previousNode.value(node))
            steps.prepend(previousArc.value(node));
        int cursor=from;
        out << QStringLiteral("PARTIDA: %1").arg(potentialSummary(from));
        bool acrossLoad=false,acrossConverter=false,conditional=false,unknown=false;
        bool exclusiveConflict=false;
        QSet<QString> usedChangeovers;
        for(int idx:steps) {
            const DevicePassage &arc=passages.at(idx);
            const int next=arc.a==cursor ? arc.b : arc.a;
            const QString inputPin=arc.a==cursor ? arc.pinA : arc.pinB;
            const QString outputPin=arc.a==cursor ? arc.pinB : arc.pinA;
            out << QStringLiteral(
                "P%1 → pág.%2 %3 [%4] (%5 → %6) → P%7 | %8")
                .arg(cursor+1).arg(arc.page,arc.label,arc.elementId,
                     inputPin,outputPin).arg(next+1).arg(arc.behaviour);
            if(arc.behaviour.startsWith(QStringLiteral("COMUTADOR"))) {
                conditional=true;
                if(usedChangeovers.contains(arc.elementId)) exclusiveConflict=true;
                usedChangeovers.insert(arc.elementId);
            }
            conditional |= arc.behaviour.startsWith(QStringLiteral("MANOBRA"));
            acrossLoad |= arc.behaviour.startsWith(QStringLiteral("CARGA"));
            acrossConverter |= arc.behaviour.startsWith(QStringLiteral("CONVERSÃO"));
            unknown |= arc.behaviour.startsWith(QStringLiteral("DISPOSITIVO"));
            for(const ReportHop &hop:reportHops)
                if((hop.a==next || hop.b==next) &&
                   (hop.fromPage!=hop.toPage))
                    out << QStringLiteral("  ↳ SETAS VINCULADAS: pág.%1 %2 → pág.%3 %4")
                        .arg(hop.fromPage,hop.source,hop.toPage,hop.destination);
            cursor=next;
        }
        out << QStringLiteral("DESTINO: %1").arg(potentialSummary(to));
        if(conditional)
            out << QStringLiteral("ATENÇÃO: passa por contatos de estado desconhecido.");
        if(exclusiveConflict)
            out << QStringLiteral(
                "ROTA NÃO REALIZÁVEL NUM ÚNICO ESTADO: seria necessário "
                "atravessar duas vias mutuamente exclusivas do mesmo comutador. "
                "Esta sequência só demonstra associação no diagrama.");
        if(acrossLoad)
            out << QStringLiteral("ATENÇÃO: passa por uma carga; a corrente pode mudar.");
        if(acrossConverter)
            out << QStringLiteral("ATENÇÃO: passa por conversor/fonte; tensão e corrente não são transferidas.");
        if(unknown)
            out << QStringLiteral("ATENÇÃO: passa por elementos cuja conexão interna não foi confirmada.");
        if(unsafeGroups.contains(from) || unsafeGroups.contains(to))
            out << QStringLiteral("ATENÇÃO: pelo menos uma extremidade contém conexões inconsistentes.");
        out << QStringLiteral(
            "ROTA ESQUEMÁTICA, NÃO É PROVA DE ENERGIZAÇÃO OU DIMENSIONAMENTO.");
        return out.join(QLatin1Char('\n'));
    }
    // 'complete' refers to topology index coverage, not certified electric design.
    QString pathReport(int start) const {
        QStringList out;
        if (start<0 || start>=groups.size())
            return QStringLiteral("Selecione um potencial rastreado válido.");
        out << QStringLiteral("PERCURSO ELÉTRICO DESDE O POTENCIAL %1")
                   .arg(start+1);
        out << QStringLiteral("Setas entre folhas: ligações nativas NextReport/PreviousReport.");
        out << QStringLiteral(
            "ATENÇÃO: atravessar um dispositivo mostra um CAMINHO POSSÍVEL, "
            "não confirma que contatos estão fechados ou que todas as "
            "cargas conduzem corrente simultaneamente.");
        QSet<int> visited;
        QQueue<int> pending;
        visited.insert(start);
        pending.enqueue(start);
        while(!pending.isEmpty()) {
            const int groupId=pending.dequeue();
            out << QStringLiteral("NÓ %1: %2").arg(groupId+1)
                       .arg(potentialSummary(groupId));
            for(const ReportHop &hop:reportHops) {
                if(hop.a==groupId || hop.b==groupId)
                    out << QStringLiteral("  SETA ENTRE FOLHAS: %1 (pág. %2) -> %3 (pág. %4)")
                        .arg(hop.source,hop.fromPage,hop.destination,hop.toPage);
            }
            for(const DevicePassage &arc:passages) {
                const int other=arc.a==groupId ? arc.b :
                                arc.b==groupId ? arc.a : -1;
                if(other<0) continue;
                out << QStringLiteral("  PELO ELEMENTO %1 [%2], pág. %3: %4 -> %5; "
                                      "potencial %6 -> %7; %8")
                       .arg(arc.label,arc.elementId,arc.page,
                            arc.a==groupId ? arc.pinA : arc.pinB,
                            arc.a==groupId ? arc.pinB : arc.pinA)
                       .arg(groupId+1).arg(other+1).arg(arc.behaviour);
                if(arc.traversable && !visited.contains(other)) {
                    visited.insert(other);
                    pending.enqueue(other);
                }
            }
        }
        out << QStringLiteral("NÓS ALCANÇADOS (CAMINHOS POSSÍVEIS): %1 de %2")
                   .arg(visited.size()).arg(groups.size());
        if(!warnings.isEmpty()) {
            out << QStringLiteral("===== AVISOS DE CONEXÃO/TERMINAIS NÃO RESOLVIDOS =====");
            out << warnings;
        }
        out << QStringLiteral(
            "Regra: passagem por dispositivo não é emenda e não transfere "
            "automaticamente valores de tensão, corrente ou potência.");
        out << QStringLiteral("Não foi calculada nem aplicada bitola por este rastreamento.");
        return out.join(QLatin1Char('\n'));
    }
    // A trace potential is one equipotential node, NOT a complete load circuit.
    // This short description helps select the corresponding wires without
    // requiring users to manually find every segment in the drawing.
    QString potentialSummary(int groupIndex) const {
        if (groupIndex < 0 || groupIndex >= groups.size()) return {};
        const PotentialGroup &g = groups.at(groupIndex);
        QStringList pages, devices;
        for (Diagram *folio : g.folios)
            if (folio) pages << QStringLiteral("%1:%2")
                .arg(folio->folioIndex()+1).arg(safeText(folio->title()));
        for (Element *element : g.adjacentElements)
            if (element) devices << QStringLiteral("%1 [%2]")
                .arg(safeText(element->actualLabel()),
                     safeText(element->name()));
        pages.removeDuplicates(); pages.sort();
        devices.removeDuplicates();devices.sort();
        if (devices.size()>4) {
            devices = devices.mid(0,4);
            devices << QStringLiteral("...");
        }
        const QString evidence=g.declaredData.isEmpty() ?
            QStringLiteral("dados elétricos nominais não localizados") :
            QStringLiteral("dados declarados: %1")
                .arg(g.declaredData.mid(0,2).join(QStringLiteral("; ")));
        return QStringLiteral("Potencial %1 | %2 fio(s) | Página(s) %3 | %4 | %5")
            .arg(groupIndex+1).arg(g.conductors.size())
            .arg(pages.join(QStringLiteral(", ")))
            .arg(devices.join(QStringLiteral("; ")))
            .arg(evidence);
    }
    QString report() const {
        QStringList out;
        out << QStringLiteral("RASTREABILIDADE DO PROJETO — LEITURA SEM ALTERAÇÕES");
        out << QStringLiteral("Páginas: %1 | Elementos: %2 | Terminais: %3 | Fios: %4")
                   .arg(folios).arg(elements).arg(terminals).arg(wires);
        out << QStringLiteral("Potenciais distintos: %1 | Potenciais entre páginas: %2")
                   .arg(groups.size()).arg(multiFolioPotentials);
        out << QStringLiteral("Elementos ligados a 2+ potenciais: %1")
                   .arg(elementsLinkingPotentials);
        out << QStringLiteral("Passagens por dispositivos: %1 | Setas vinculadas: %2 | Setas sem par: %3")
                   .arg(passages.size()).arg(reportHops.size()).arg(unmatchedReports);
        out << QStringLiteral("Potenciais com conexão inconsistente: %1")
                   .arg(unsafeGroups.size());
        out << QStringLiteral("Passagens de polos reconhecidas por geometria: %1")
                   .arg(recoveredThreePolePassages);
        out << QStringLiteral("Passagens alternativas de comutadores: %1")
                   .arg(recoveredChangeoverPassages);
        out << QStringLiteral("Terminais sem fio: %1 | Ligações com terminal inválido: %2")
                   .arg(openTerminals).arg(invalidEndpoints);
        out << QStringLiteral("Cobertura do grafo: %1")
                   .arg(complete ? QStringLiteral("todos os fios foram indexados")
                                 : QStringLiteral("INCOMPLETA — verificar terminais"));
        out << QStringLiteral("Conexões entre folhas: %1")
                   .arg(unmatchedReports ?
                       QStringLiteral("%1 vínculo(s) não resolvido(s); circuitos afetados isolados")
                           .arg(unmatchedReports) :
                       QStringLiteral("sem inconsistências detectadas"));
        out << QStringLiteral(
            "ATENÇÃO: continuidade elétrica não prova corrente, tensão, potência, "
            "queda admissível ou seção. Dispositivos de manobra e proteção "
            "não são tratados como simples fios contínuos.");
        out << QStringLiteral("===== PÁGINAS =====");
        out << folioInventory;
        out << QStringLiteral("===== ELEMENTOS E TERMINAIS =====");
        out << elementInventory;
        out << QStringLiteral("===== ELEMENTOS ENTRE POTENCIAIS (SEM ASSUMIR CONTATO FECHADO) =====");
        out << componentConnections;
        out << QStringLiteral("===== PERCURSO ATRAVÉS DE DISPOSITIVOS (NÃO É UMA EMENDA) =====");
        for(const DevicePassage &arc:passages)
            out << QStringLiteral("pág. %1 | %2 [%3] | %4 (P%5) => %6 (P%7) | %8")
                .arg(arc.page,arc.label,arc.elementId,arc.pinA)
                .arg(arc.a+1).arg(arc.pinB).arg(arc.b+1).arg(arc.behaviour);
        out << QStringLiteral("===== CONTINUIDADE ENTRE FOLHAS PELAS SETAS =====");
        for(const ReportHop &hop:reportHops)
            out << QStringLiteral("%1 [pág. %2] => %3 [pág. %4] | grupos: %5 / %6")
                .arg(hop.source,hop.fromPage,hop.destination,hop.toPage)
                .arg(hop.a+1).arg(hop.b+1);
        out << QStringLiteral("===== POTENCIAIS ELÉTRICOS =====");
        for (int i=0;i<groups.size();++i) {
            const PotentialGroup &g=groups.at(i);
            QStringList pages;
            for (Diagram *folio : g.folios)
                pages << QStringLiteral("%1: %2")
                    .arg(folio->folioIndex()+1).arg(safeText(folio->title()));
            pages.sort();
            out << QStringLiteral("POTENCIAL %1 | fios=%2 | páginas=%3 | elementos=%4")
                        .arg(i+1).arg(g.conductors.size()).arg(pages.size())
                        .arg(g.adjacentElements.size());
            out << QStringLiteral(" Páginas: %1").arg(pages.join(QStringLiteral(" | ")));
            out << QStringLiteral(" Fios (UUID): %1")
                        .arg(g.wireIds.join(QStringLiteral(", ")));
            for (const QString &end : g.endpoints) out << QStringLiteral("  ") + end;
            if (!g.declaredData.isEmpty()) {
                out << QStringLiteral(" Dados informados nos símbolos (NÃO validados):");
                out << g.declaredData;
            }
            out << QStringLiteral(" PENDENTE: identificar fonte, carga, corrente de projeto, "
                                 "proteção, método de instalação e comprimento deste circuito");
            if (g.hasUnconnectedEndpoint)
                out << QStringLiteral(" ALERTA: fio com terminal de extremidade inválido");
        }
        out << warnings;
        out << QStringLiteral("FINAL: Nenhum dimensionamento foi realizado pelo rastreador.");
        return out.join(QLatin1Char('\n'));
    }
};
inline QString terminalDescription(const Terminal *terminal) {
    if (!terminal) return QStringLiteral("(terminal ausente)");
    Element *e=terminal->parentElement();
    const QString elem=e ? QStringLiteral("%1 [%2]")
        .arg(safeText(e->actualLabel()), safeText(e->name()))
        : QStringLiteral("(elemento ausente)");
    return QStringLiteral("%1 / borne %2 / id %3")
        .arg(elem,safeText(terminal->name()),shortUuid(terminal->uuid()));
}
inline bool isElectricalDataKey(const QString &key) {
    const QString k=key.toLower();
    return k.contains(QStringLiteral("tens")) ||
           k.contains(QStringLiteral("volt")) ||
           k.contains(QStringLiteral("curr")) ||
           k.contains(QStringLiteral("corr")) ||
           k.contains(QStringLiteral("poten")) ||
           k.contains(QStringLiteral("power")) ||
           k.contains(QStringLiteral("nominal")) ||
           k.contains(QStringLiteral("rated")) ||
           k.contains(QStringLiteral("length")) ||
           k.contains(QStringLiteral("comprimento")) ||
           k.contains(QStringLiteral("rendimento")) ||
           k.contains(QStringLiteral("efficien")) ||
           k.contains(QStringLiteral("cosphi"));
}
// Report folio arrows are linked as project symbols; never pair them
// merely because their labels look alike.
inline int groupAtTerminal(const Snapshot &snap, const Terminal *terminal) {
    if (!terminal) return -1;
    for (Conductor *wire : terminal->conductors()) {
        const int id=snap.groupByConductor.value(wire,-1);
        if(id>=0) return id;
    }
    return -1;
}
inline QString normalizedPin(const QString &label) {
    QString v=label.trimmed().toUpper();
    v.remove(QRegularExpression(QStringLiteral("[\\s._-]")));
    return v;
}
// Named pin pairs: main poles, auxiliary/thermal contacts, windings.
// No fallback "connect every terminal": multi-pole components would short.
inline QList<QPair<QString,QString>> namedPairs() {
    return {
        {QStringLiteral("1"),QStringLiteral("2")},
        {QStringLiteral("3"),QStringLiteral("4")},
        {QStringLiteral("5"),QStringLiteral("6")},
        {QStringLiteral("7"),QStringLiteral("8")},
        {QStringLiteral("1L1"),QStringLiteral("2T1")},
        {QStringLiteral("3L2"),QStringLiteral("4T2")},
        {QStringLiteral("5L3"),QStringLiteral("6T3")},
        {QStringLiteral("L1"),QStringLiteral("T1")},
        {QStringLiteral("L2"),QStringLiteral("T2")},
        {QStringLiteral("L3"),QStringLiteral("T3")},
        {QStringLiteral("13"),QStringLiteral("14")},
        {QStringLiteral("21"),QStringLiteral("22")},
        {QStringLiteral("23"),QStringLiteral("24")},
        {QStringLiteral("31"),QStringLiteral("32")},
        {QStringLiteral("33"),QStringLiteral("34")},
        {QStringLiteral("41"),QStringLiteral("42")},
        {QStringLiteral("43"),QStringLiteral("44")},
        {QStringLiteral("53"),QStringLiteral("54")},
        {QStringLiteral("61"),QStringLiteral("62")},
        {QStringLiteral("71"),QStringLiteral("72")},
        {QStringLiteral("81"),QStringLiteral("82")},
        {QStringLiteral("95"),QStringLiteral("96")},
        {QStringLiteral("97"),QStringLiteral("98")},
        {QStringLiteral("A1"),QStringLiteral("A2")},
        {QStringLiteral("U1"),QStringLiteral("U2")},
        {QStringLiteral("V1"),QStringLiteral("V2")},
        {QStringLiteral("W1"),QStringLiteral("W2")}
    };
}
// QET libraries often contain six unnamed terminals on a three-pole
// power contactor/thermal relay. Derive three separate pole routes only
// for recognized THREE-POLE COMPONENTS and only for a strict 3+3 opposing
// geometric arrangement. This is a schematic passage, not an electrical
// short between phases or confirmation that the contact is closed.
inline QList<QPair<Terminal *,Terminal *>> recoverThreePoleGeometry(Element *e) {
    QList<QPair<Terminal *,Terminal *>> empty;
    if (!e) return empty;
    const QList<Terminal *> pins=e->terminals();
    if(pins.size()!=6) return empty;
    QString description=(e->name()+QStringLiteral(" ")+
        e->location().baseName()).toLower();
    const bool expectedDevice=
        description.contains(QStringLiteral("com_puiss")) ||
        description.contains(QStringLiteral("power_contact")) ||
        description.contains(QStringLiteral("contact_puiss")) ||
        description.contains(QStringLiteral("relais_therm")) ||
        description.contains(QStringLiteral("thermal_relay")) ||
        description.contains(QStringLiteral("relé térmico")) ||
        description.contains(QStringLiteral("rele termico")) ||
        description.contains(QStringLiteral("contacteur")) ||
        description.contains(QStringLiteral("contactor")) ||
        description.contains(QStringLiteral("contator")) ||
        description.contains(QStringLiteral("contato de potência")) ||
        description.contains(QStringLiteral("power contacts"));
    // A contactor or thermal relay may correctly be described as
    // "proteção de motor"; the word motor must NOT hide its three poles.
    // Actual six-terminal motors without contactor/relay markers remain
    // excluded by expectedDevice above.
    if(!expectedDevice ||
       description.contains(QStringLiteral("bobina")) ||
       description.contains(QStringLiteral("coil")))
        return empty;
    // The projected terminal positions are already rotated/mirrored in
    // scene coordinates. Try dividing along both axes and require
    // three well-aligned matching lanes on the opposite side.
    auto grouping=[&](bool splitY)->QList<QPair<Terminal *,Terminal *>> {
        QList<Terminal *> sorted=pins;
        auto ax=[&](Terminal *t)->qreal {
            const QPointF p=t->dockConductor(); return splitY ? p.y() : p.x();
        };
        auto other=[&](Terminal *t)->qreal {
            const QPointF p=t->dockConductor(); return splitY ? p.x() : p.y();
        };
        std::sort(sorted.begin(),sorted.end(),[&](Terminal *a,Terminal *b) {
            return ax(a)<ax(b);
        });
        const qreal gap=ax(sorted.at(3))-ax(sorted.at(2));
        if(gap<5.0) return {};
        QList<Terminal *> left=sorted.mid(0,3),right=sorted.mid(3,3);
        const qreal spreadL=ax(left.last())-ax(left.first());
        const qreal spreadR=ax(right.last())-ax(right.first());
        if(spreadL>gap*0.20 || spreadR>gap*0.20) return {};
        std::sort(left.begin(),left.end(),[&](Terminal *a,Terminal *b) {
            return other(a)<other(b);
        });
        std::sort(right.begin(),right.end(),[&](Terminal *a,Terminal *b) {
            return other(a)<other(b);
        });
        const qreal span=other(left.last())-other(left.first());
        if(span<5.0 || other(right.last())-other(right.first())<5.0) return {};
        const qreal tolerance=std::max(qreal(3.0),span*0.12);
        QList<QPair<Terminal *,Terminal *>> result;
        for(int i=0;i<3;++i) {
            if(qAbs(other(left.at(i))-other(right.at(i)))>tolerance)
                return {};
            result << qMakePair(left.at(i),right.at(i));
        }
        return result;
    };
    const auto y=grouping(true);
    const auto x=grouping(false);
    // If both axes look valid the symbol is ambiguous: don't guess.
    if(!y.isEmpty() && !x.isEmpty())return empty;
    return !y.isEmpty()?y:x;
}

inline QString deviceBehaviour(const Element *element,const QString &pa,
                               const QString &pb) {
    const QString n=element ? element->name().toLower() : QString();
    if(n.contains(QStringLiteral("transform")) ||
       n.contains(QStringLiteral("alimentação")) ||
       n.contains(QStringLiteral("power supply")) ||
       n.contains(QStringLiteral("fonte")) ||
       n.contains(QStringLiteral("rectifier")) ||
       n.contains(QStringLiteral("retificador")))
        return QStringLiteral("CONVERSÃO: não transmitir tensão/corrente entre domínios");
    if((pa==QStringLiteral("A1") && pb==QStringLiteral("A2")) ||
       n.contains(QStringLiteral("bobina")) ||
       n.contains(QStringLiteral("coil")) ||
       n.contains(QStringLiteral("motor")) ||
       n.contains(QStringLiteral("lamp")) ||
       n.contains(QStringLiteral("lâmpada")) ||
       n.contains(QStringLiteral("resistor")))
        return QStringLiteral("CARGA: entrada/saída do dispositivo, não continuidade de fio");
    if(n.contains(QStringLiteral("contact")) ||
       n.contains(QStringLiteral("contato")) ||
       n.contains(QStringLiteral("disjuntor")) ||
       n.contains(QStringLiteral("breaker")) ||
       n.contains(QStringLiteral("fusível")) ||
       n.contains(QStringLiteral("fuse")) ||
       n.contains(QStringLiteral("relay")) ||
       n.contains(QStringLiteral("relé")) ||
       n.contains(QStringLiteral("selector")) ||
       n.contains(QStringLiteral("switch")) ||
       n.contains(QStringLiteral("interruptor")) ||
       n.contains(QStringLiteral("comutador")) ||
       n.contains(QStringLiteral("selector")) ||
       n.contains(QStringLiteral("contator")) ||
       n.contains(QStringLiteral("contactor")))
        return QStringLiteral("MANOBRA/PROTEÇÃO: estado do contato não verificado");
    return QStringLiteral("DISPOSITIVO: conexão interna não verificada");
}

inline Snapshot scan(QETProject *project) {
    Snapshot snap;
    if (!project) {
        snap.warnings << QStringLiteral("Erro: projeto não informado.");
        return snap;
    }
    const QList<Diagram *> allFolios=project->diagrams(); // Builds lazy folios.
    QSet<Conductor *> available;
    QList<Conductor *> allWires;
    for (Diagram *folio : allFolios) {
        if (!folio || folio->border_and_titleblock.additionalFields()
                .value(QStringLiteral("qet_generated_documentation")).toBool()) continue;
        ++snap.folios;
        const QList<Element *> elements=folio->elements();
        const QList<Conductor *> conductors=folio->conductors();
        snap.wires+=conductors.size();
        snap.elements+=elements.size();
        snap.folioInventory << QStringLiteral("Página %1 [%2]: %3 elementos, %4 fios")
            .arg(folio->folioIndex()+1)
            .arg(safeText(folio->title()))
            .arg(elements.size())
            .arg(conductors.size());
        for (Element *e : elements) {
            if (!e) continue;
            const QList<Terminal *> terminals=e->terminals();
            snap.terminals+=terminals.size();
            int open=0;
            for (Terminal *t : terminals)
                if (t && t->conductors().isEmpty()) { ++open; ++snap.openTerminals; }
            snap.elementInventory << QStringLiteral("Página %1 | %2 | símbolo %3 | terminais %4, livres %5")
                .arg(folio->folioIndex()+1)
                .arg(safeText(e->actualLabel()))
                .arg(safeText(e->name()))
                .arg(terminals.size())
                .arg(open);
            for (Terminal *t : terminals) {
                if (!t) { snap.warnings << QStringLiteral("Elemento com terminal nulo."); continue; }
                snap.elementInventory << QStringLiteral("  borne %1 | id %2 | %3 fio(s)")
                    .arg(safeText(t->name()),shortUuid(t->uuid()))
                    .arg(t->conductorsCount());
            }
        }
        for (Conductor *c : conductors) {
            if (!c) continue;
            allWires << c;
            available.insert(c);
            if (!c->terminal1 || !c->terminal2)
                ++snap.invalidEndpoints;
        }
    }
    // Stable grouping/order is essential when comparing trace reports.
    std::sort(allWires.begin(),allWires.end(),[](Conductor *a,Conductor *b){
        return a->uuid().toString()<b->uuid().toString();
    });
    // Native queries can overlap (notably with directional report links).
    // Partition their equivalence closure once; never insert a wire in two
    // groups or let a later query overwrite its groupByConductor entry.
    QHash<Conductor *, Conductor *> roots;
    for (Conductor *wire : std::as_const(allWires)) roots.insert(wire,wire);
    auto root = [&](Conductor *wire) {
        Conductor *r=wire;
        while (roots.value(r)!=r) r=roots.value(r);
        while (wire!=r) {
            Conductor *next=roots.value(wire);
            roots[wire]=r;
            wire=next;
        }
        return r;
    };
    for (Conductor *wire : std::as_const(allWires)) {
        if(!wire->terminal1 || !wire->terminal2) continue;
        const auto connected=wire->relatedPotentialConductors(true);
        for (Conductor *member : connected) {
            if(!available.contains(member)) continue;
            Conductor *a=root(wire),*b=root(member);
            if(a!=b) roots[b]=a;
        }
    }
    QHash<Conductor *,QList<Conductor *>> partitions;
    for (Conductor *wire : std::as_const(allWires))
        partitions[root(wire)].append(wire);
    QSet<Conductor *> visited;
    for (Conductor *leader : std::as_const(allWires)) {
        if (visited.contains(leader)) continue;
        const QList<Conductor *> members=partitions.value(root(leader));
        PotentialGroup group;
        const int groupId=snap.groups.size();
        for (Conductor *c : members) {
            visited.insert(c);
            snap.groupByConductor.insert(c,groupId);
            group.conductors << c;
            if (c->diagram()) group.folios.insert(c->diagram());
            else { group.hasUnconnectedEndpoint=true; snap.warnings <<
                       QStringLiteral("Condutor sem página associada: %1")
                           .arg(shortUuid(c->uuid())); }
            group.wireIds << shortUuid(c->uuid());
            for (Terminal *t : {c->terminal1,c->terminal2}) {
                if (!t) {
                    group.hasUnconnectedEndpoint=true;
                    continue;
                }
                group.terminals.insert(t);
                if (Element *e=t->parentElement()) group.adjacentElements.insert(e);
            }
            group.endpoints << QStringLiteral("Fio %1 | pág. %2 | %3  -->  %4")
                .arg(shortUuid(c->uuid()))
                .arg(c->diagram() ? c->diagram()->folioIndex()+1 : -1)
                .arg(terminalDescription(c->terminal1))
                .arg(terminalDescription(c->terminal2));
        }
        for (Element *e : group.adjacentElements) {
            if (!e) continue;
            DiagramContext info=e->elementInformations();
            for (const QString &key : info.keys()) {
                if (!isElectricalDataKey(key)) continue;
                const QString value=info.value(key).toString().trimmed();
                if (value.isEmpty()) continue;
                group.declaredData << QStringLiteral("   %1 | %2: %3")
                   .arg(safeText(e->actualLabel()),key,safeText(value));
            }
        }
        group.declaredData.removeDuplicates();
        group.declaredData.sort();
        if (group.spansFolios()) ++snap.multiFolioPotentials;
        if(group.hasUnconnectedEndpoint) snap.unsafeGroups.insert(groupId);
        snap.groups << group;
    }
    // Build the second layer: which electrically distinct potentials touch
    // the terminals of each component? This is an incidence graph, NEVER
    // a claim that a switch/contact is closed or a transformer is a wire.
    for (Diagram *folio : allFolios) {
        if (!folio || folio->border_and_titleblock.additionalFields()
                .value(QStringLiteral("qet_generated_documentation")).toBool()) continue;
        for (Element *element : folio->elements()) {
            if (!element) continue;
            QSet<int> adjacent;
            QStringList pins;
            for (Terminal *terminal : element->terminals()) {
                if (!terminal) continue;
                QSet<int> terminalPotentials;
                for (Conductor *wire : terminal->conductors()) {
                    const int groupId=snap.groupByConductor.value(wire,-1);
                    if (groupId < 0) continue;
                    terminalPotentials.insert(groupId);
                    adjacent.insert(groupId);
                }
                QStringList refs;
                for (int id : terminalPotentials) refs << QString::number(id+1);
                refs.sort();
                pins << QStringLiteral("%1=>potencial %2")
                    .arg(safeText(terminal->name()),refs.isEmpty() ?
                          QStringLiteral("sem fio") : refs.join(QStringLiteral(",")));
            }
            if (adjacent.size() > 1) ++snap.elementsLinkingPotentials;
            QStringList refs;
            for (int id : adjacent) refs << QString::number(id+1);
            refs.sort();
            snap.componentConnections << QStringLiteral(
                "Pág.%1 | %2 [%3] | potenciais em terminais: %4 | %5")
                .arg(folio->folioIndex()+1)
                .arg(safeText(element->actualLabel()))
                .arg(safeText(element->name()))
                .arg(refs.isEmpty() ? QStringLiteral("nenhum") :
                       refs.join(QStringLiteral(",")))
                .arg(pins.join(QStringLiteral(" | ")));
        }
    }
    // Third graph layer: follow port-to-port passage through recognized
    // component pin pairs WITHOUT equating their potentials. This allows
    // tracing past contactors, protectors and loads on subsequent sheets.
    QSet<QString> processedArrows;
    for(Diagram *folio:allFolios) {
        if(!folio || folio->border_and_titleblock.additionalFields()
                .value(QStringLiteral("qet_generated_documentation")).toBool()) continue;
        for(Element *e:folio->elements()) {
            if(!e) continue;
            const QList<Terminal *> pins=e->terminals();
            if(e->linkType() & Element::AllReport) {
                const QList<Element *> partners=e->linkedElements();
                if(partners.isEmpty()) {
                    if(!e->conductors().isEmpty()) {
                        ++snap.unmatchedReports;
                        for(Terminal *pin:pins) {
                            const int affected=groupAtTerminal(snap,pin);
                            if(affected>=0) snap.unsafeGroups.insert(affected);
                        }
                        snap.warnings << QStringLiteral(
                            "SETA SEM VÍNCULO: pág.%1 %2 (%3)")
                            .arg(folio->folioIndex()+1)
                            .arg(safeText(e->actualLabel()),shortUuid(e->uuid()));
                    }
                    continue;
                }
                for(Element *other:partners) {
                    if(!other || !(other->linkType() & Element::AllReport)) continue;
                    const QString left=e->uuid().toString(), right=other->uuid().toString();
                    const QString key=left<right ? left+right : right+left;
                    if(processedArrows.contains(key)) continue;
                    processedArrows.insert(key);
                    const int from=pins.isEmpty() ? -1 : groupAtTerminal(snap,pins.first());
                    const QList<Terminal *> peerPins=other->terminals();
                    const int dest=peerPins.isEmpty() ? -1 :
                                   groupAtTerminal(snap,peerPins.first());
                    if(from<0 || dest<0) continue;
                    const bool nextToPrev =
                        ((e->linkType() & Element::NextReport) &&
                         (other->linkType() & Element::PreviousReport)) ||
                        ((e->linkType() & Element::PreviousReport) &&
                         (other->linkType() & Element::NextReport));
                    if(!nextToPrev) {
                        ++snap.unmatchedReports;
                        snap.unsafeGroups.insert(from);
                        snap.unsafeGroups.insert(dest);
                        snap.warnings << QStringLiteral(
                            "SETAS INCOMPATÍVEIS: %1 (pág.%2) e %3 (pág.%4) "
                            "não são um par PreviousReport/NextReport.")
                            .arg(safeText(e->actualLabel()))
                            .arg(folio->folioIndex()+1)
                            .arg(safeText(other->actualLabel()))
                            .arg(other->diagram() ? other->diagram()->folioIndex()+1 : -1);
                        continue;
                    }
                    ReportHop hop;
                    hop.a=from;hop.b=dest;
                    hop.source=safeText(e->actualLabel());
                    hop.destination=safeText(other->actualLabel());
                    hop.fromPage=QString::number(folio->folioIndex()+1);
                    hop.toPage=other->diagram() ?
                        QString::number(other->diagram()->folioIndex()+1) :
                        QStringLiteral("?");
                    snap.reportHops << hop;
                    if(from!=dest) {
                        snap.warnings << QStringLiteral(
                            "ERRO DE CONTINUIDADE: seta vinculada %1 / %2 "
                            "aparece em dois potenciais separados (%3/%4).")
                            .arg(hop.source,hop.destination)
                            .arg(from+1).arg(dest+1);
                        ++snap.unmatchedReports;
                        snap.unsafeGroups.insert(from);
                        snap.unsafeGroups.insert(dest);
                    }
                }
                continue;
            }
            if(e->linkType() & Element::Terminale) continue;
            QHash<QString,Terminal *> pinsByName;
            QSet<QString> duplicates;
            for(Terminal *pin:pins) {
                if(!pin) continue;
                const QString name=normalizedPin(pin->name());
                if(name.isEmpty()) continue;
                if(pinsByName.contains(name)) duplicates.insert(name);
                else pinsByName.insert(name,pin);
            }
            auto addPassage=[&](Terminal *a,Terminal *b) {
                if(!a || !b || a==b) return;
                const int ga=groupAtTerminal(snap,a),gb=groupAtTerminal(snap,b);
                if(ga<0 || gb<0 || ga==gb) return;
                const QString pa=normalizedPin(a->name()),pb=normalizedPin(b->name());
                DevicePassage arc;
                arc.a=ga;arc.b=gb;arc.elementId=shortUuid(e->uuid());
                arc.label=safeText(e->actualLabel())+QStringLiteral(" ")+safeText(e->name());
                arc.page=QString::number(folio->folioIndex()+1);
                arc.pinA=safeText(a->name());arc.pinB=safeText(b->name());
                arc.behaviour=deviceBehaviour(e,pa,pb);
                arc.traversable=arc.behaviour.startsWith(QStringLiteral("MANOBRA"));
                snap.passages << arc;
            };
            int identifiedPairs=0;
            QSet<QString> usedPairs;
            for(const auto &pr:namedPairs()) {
                if(duplicates.contains(pr.first) || duplicates.contains(pr.second))
                    continue;
                Terminal *a=pinsByName.value(pr.first,nullptr);
                Terminal *b=pinsByName.value(pr.second,nullptr);
                if(!a || !b || a==b) continue;
                // Don't pair a bare '1/2' if a more specific name also
                // identifies these pins; each pair represents one port path.
                const QString key=a->uuid().toString()+b->uuid().toString();
                if(usedPairs.contains(key)) continue;
                usedPairs.insert(key);
                ++identifiedPairs;
                addPassage(a,b);
            }
            if(pins.size()==3 && identifiedPairs==0) {
                // A changeover switch has ONE common terminal and TWO
                // mutually-exclusive throws. Infer the common only for
                // known switch elements and only when the two throws share
                // an orientation different from the common.
                const QString type=(e->name()+QStringLiteral(" ")+
                    e->location().baseName()).toLower();
                const bool selector=
                    type.contains(QStringLiteral("interruptor")) ||
                    type.contains(QStringLiteral("comutador")) ||
                    type.contains(QStringLiteral("changeover")) ||
                    type.contains(QStringLiteral("selector")) ||
                    type.contains(QStringLiteral("switch")) ||
                    type.contains(QStringLiteral("spdt"));
                if(selector) {
                    QHash<int,QList<Terminal *>> byOrientation;
                    for(Terminal *pin:pins)
                        if(pin) byOrientation[static_cast<int>(pin->orientation())] << pin;
                    if(byOrientation.size()==2) {
                        Terminal *common=nullptr;
                        QList<Terminal *> throws;
                        for(const auto &by:std::as_const(byOrientation)) {
                            if(by.size()==1) common=by.first();
                            else if(by.size()==2) throws=by;
                        }
                        if(common && throws.size()==2) {
                            // Never connect the two throws to each other as
                            // a permanent wire; each branch is an
                            // alternative state of the same selector.
                            for(Terminal *pin:throws) {
                                const int before=snap.passages.size();
                                addPassage(common,pin);
                                if(snap.passages.size()>before) {
                                    snap.passages.last().behaviour=QStringLiteral(
                                        "COMUTADOR: via alternativa; posição do contato desconhecida");
                                    snap.passages.last().traversable=false;
                                    snap.passages.last().behaviour.prepend(QStringLiteral("INCERTO (geometria): "));
                                    ++snap.recoveredChangeoverPassages;
                                }
                                ++identifiedPairs;
                            }
                        }
                    }
                }
            }
            if(pins.size()==6 && identifiedPairs==0) {
                const auto geometricPairs=recoverThreePoleGeometry(e);
                for(const auto &pr:geometricPairs) {
                    const int before=snap.passages.size();
                    addPassage(pr.first,pr.second);
                    if(snap.passages.size()>before) {
                        snap.passages.last().traversable=false;
                        snap.passages.last().behaviour.prepend(QStringLiteral("INCERTO (geometria): "));
                    }
                    ++identifiedPairs;
                    ++snap.recoveredThreePolePassages;
                }
            }
            // A two-pin symbol has a single visible path. The report
            // explicitly marks its electrical behaviour as unverified.
            if(pins.size()==2 && !identifiedPairs) {
                ++identifiedPairs;
                addPassage(pins.at(0),pins.at(1));
                // Two terminals alone do not identify a contact or prove
                // continuity: leave unknown devices/loads as boundaries.
            }
            if(pins.size()>2 && identifiedPairs==0 &&
               e->conductors().size()>1) {
                ++snap.unsupportedMultiTerminal;
                snap.warnings << QStringLiteral(
                    "TERMINAIS SEM PARES DEFINIDOS: pág.%1 %2 [%3]. "
                    "Não é seguro ligar todos os bornes do símbolo.")
                    .arg(folio->folioIndex()+1)
                    .arg(safeText(e->actualLabel()),safeText(e->name()));
            }
        }
    }
    // Coverage means every physical wire was indexed with valid endpoints.
    // An unpaired inter-folio arrow is a LOCAL electrical uncertainty, not
    // evidence that unrelated folios/branches failed to be traced.
    // Such groups are already marked in unsafeGroups and independently
    // rejected by canSizeGroup() before any sizing can be applied.
    snap.complete=(snap.invalidEndpoints==0 &&
                   snap.groupByConductor.size()==available.size());
    if (!snap.complete)
        snap.warnings << QStringLiteral(
            "RASTREAMENTO INCOMPLETO: faltam fios ou terminais válidos. "
            "Os potenciais afetados não podem ser dimensionados.");
    if (snap.unmatchedReports)
        snap.warnings << QStringLiteral(
            "SETAS ENTRE FOLHAS COM PROBLEMAS: %1 vínculo(s) não confirmado(s); "
            "o rastreamento dos demais fios continua válido, mas os potenciais "
            "das setas envolvidas estão bloqueados para dimensionamento.")
            .arg(snap.unmatchedReports);
    if (snap.openTerminals)
        snap.warnings << QStringLiteral(
            "OBSERVAÇÃO: terminais livres podem ser intencionais, mas precisam de revisão.");
    return snap;
}
inline void showPaths(QWidget *parent, QETProject *project) {
    const Snapshot graph=scan(project);
    if(graph.groups.isEmpty()) {
        QMessageBox::information(parent,QObject::tr("Percorrer circuito"),
            QObject::tr("Nenhum fio foi encontrado."));return;
    }
    int start=-1, destination=-1;
    for(int i=0;i<graph.groups.size();++i) {
        for(Conductor *wire:graph.groups.at(i).conductors) {
            if(!wire || !wire->isSelected()) continue;
            if(start<0) start=i;
            else if(i!=start) {destination=i;break;}
        }
        if(destination>=0)break;
    }
    if(start<0) {
        QStringList choices;
        for(int i=0;i<graph.groups.size();++i)
            choices<<graph.potentialSummary(i);
        bool ok=false;
        const QString choice=QInputDialog::getItem(
            parent,QObject::tr("Percorrer elementos e setas entre folhas"),
            QObject::tr("Selecione o potencial de partida:"),choices,0,false,&ok);
        if(!ok || choice.isEmpty())return;
        start=choices.indexOf(choice);
    }
    if(destination<0) {
        // A selected wire on another QET folio may not remain selected
        // when the editor changes sheets. Provide a destination picker,
        // so multi-folio routes work with only the source wire selected.
        QStringList destinations;
        destinations << QObject::tr("Mostrar todos os caminhos possíveis");
        for(int i=0;i<graph.groups.size();++i)
            destinations << graph.potentialSummary(i);
        bool chosen=false;
        const QString item=QInputDialog::getItem(
            parent,QObject::tr("Destino do percurso entre folhas"),
            QObject::tr("Até qual potencial deve seguir o circuito? "
                        "Ou selecione todos os caminhos possíveis."),
            destinations,0,false,&chosen);
        if(!chosen) return;
        const int index=destinations.indexOf(item);
        if(index>0) destination=index-1;
    }
    QMessageBox box(QMessageBox::Information,
        QObject::tr("Percurso do circuito entre elementos e folhas"),
        destination>=0 ?
            QObject::tr("Rota entre potenciais %1 e %2. Clique em 'Mostrar detalhes'.")
                .arg(start+1).arg(destination+1) :
            QObject::tr("Partida no potencial %1. A rota inclui elementos "
                        "intermediários e referências vinculadas entre folhas. "
                        "Clique em 'Mostrar detalhes'.")
                .arg(start+1),
        QMessageBox::Ok,parent);
    box.setDetailedText(destination>=0 ?
        graph.routeBetween(start,destination) : graph.pathReport(start));
    box.exec();
}
inline void showReport(QWidget *parent, QETProject *project) {
    const Snapshot result=scan(project);
    QMessageBox box(result.complete ? QMessageBox::Information : QMessageBox::Warning,
        QObject::tr("Rastreamento elétrico do projeto"),
        QObject::tr("%1 páginas, %2 símbolos, %3 terminais, %4 fios, %5 potenciais. "
                    "%6 grupos cruzam páginas. %7 terminais livres.\n\n"
                    "Nenhuma seção, tensão ou função foi alterada. "
                    "Clique em 'Mostrar detalhes' para conferir TODOS os fios "
                    "e as ligações de cada potencial.")
           .arg(result.folios).arg(result.elements).arg(result.terminals)
           .arg(result.wires).arg(result.groups.size())
           .arg(result.multiFolioPotentials).arg(result.openTerminals),
        QMessageBox::Ok,parent);
    box.setDetailedText(result.report());
    box.exec();
}
} // namespace WireTrace
#endif
