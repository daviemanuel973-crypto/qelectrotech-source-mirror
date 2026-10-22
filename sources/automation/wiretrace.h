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
struct Snapshot {
    int folios = 0;
    int elements = 0;
    int terminals = 0;
    int wires = 0;
    int openTerminals = 0;
    int invalidEndpoints = 0;
    int multiFolioPotentials = 0;
    QList<PotentialGroup> groups;
    QHash<Conductor *,int> groupByConductor;
    QStringList elementInventory;
    QStringList folioInventory;
    QStringList warnings;
    bool complete = false;
    QString report() const {
        QStringList out;
        out << QStringLiteral("RASTREABILIDADE DO PROJETO — LEITURA SEM ALTERAÇÕES");
        out << QStringLiteral("Páginas: %1 | Elementos: %2 | Terminais: %3 | Fios: %4")
                   .arg(folios).arg(elements).arg(terminals).arg(wires);
        out << QStringLiteral("Potenciais distintos: %1 | Potenciais entre páginas: %2")
                   .arg(groups.size()).arg(multiFolioPotentials);
        out << QStringLiteral("Terminais sem fio: %1 | Ligações com terminal inválido: %2")
                   .arg(openTerminals).arg(invalidEndpoints);
        out << QStringLiteral("Cobertura do grafo: %1")
                   .arg(complete ? QStringLiteral("todos os fios foram indexados")
                                 : QStringLiteral("INCOMPLETA — NÃO DIMENSIONAR"));
        out << QStringLiteral(
            "ATENÇÃO: continuidade elétrica não prova corrente, tensão, potência, "
            "queda admissível ou seção. Dispositivos de manobra e proteção "
            "não são tratados como simples fios contínuos.");
        out << QStringLiteral("===== PÁGINAS =====");
        out << folioInventory;
        out << QStringLiteral("===== ELEMENTOS E TERMINAIS =====");
        out << elementInventory;
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
        if (!folio) continue;
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
        }
        for (Conductor *c : conductors) {
            if (!c) continue;
            allWires << c;
            available.insert(c);
            if (!c->terminal1 || !c->terminal2)
                ++snap.invalidEndpoints;
        }
    }
    QSet<Conductor *> visited;
    for (Conductor *leader : std::as_const(allWires)) {
        if (!leader || visited.contains(leader)) continue;
        QSet<Conductor *> connected=leader->relatedPotentialConductors(true);
        connected.insert(leader);
        connected.intersect(available);
        QList<Conductor *> members=connected.values();
        std::sort(members.begin(),members.end(),[](Conductor *a,Conductor *b){
            return a->uuid().toString()<b->uuid().toString();
        });
        PotentialGroup group;
        const int groupId=snap.groups.size();
        for (Conductor *c : members) {
            visited.insert(c);
            snap.groupByConductor.insert(c,groupId);
            group.conductors << c;
            group.folios.insert(c->diagram());
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
        snap.groups << group;
    }
    snap.complete=(snap.invalidEndpoints==0 && snap.groupByConductor.size()==available.size());
    if (!snap.complete)
        snap.warnings << QStringLiteral(
            "INCONSISTÊNCIA: há fios não rastreados ou com extremidades ausentes.");
    if (snap.openTerminals)
        snap.warnings << QStringLiteral(
            "OBSERVAÇÃO: terminais livres podem ser intencionais, mas precisam de revisão.");
    return snap;
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
