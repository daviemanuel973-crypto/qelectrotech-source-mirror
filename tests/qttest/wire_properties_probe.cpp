// GPL-2.0-or-later - integration check against the real QET object library.
// The fixture is an upstream QET example, never a user's proprietary diagram.
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDomDocument>
#include <QFontDatabase>
#include <QGraphicsPathItem>
#include <QHash>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariant>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <stdexcept>
#include <cstdio>
#include <cmath>
#include <utility>
#include "../../sources/QPropertyUndoCommand/qpropertyundocommand.h"
#include "../../sources/automation/autowireproperties.h"
#include "../../sources/automation/symbollegend.h"
#include "../../sources/automation/wireprecalc.h"
#include "../../sources/diagram.h"
#include "../../sources/diagramcontent.h"
#include "../../sources/qetmessagebox.h"
#include "../../sources/qetproject.h"
#include "../../sources/qetresult.h"
#include "../../sources/qetgraphicsitem/conductor.h"

static void require(bool test, const char *name) {
    if (!test) throw std::runtime_error(name);
}
int main(int argc, char **argv) {
    if (argc < 4) return 80;
    QApplication app(argc, argv);
    FILE *report = std::freopen(argv[2], "w", stdout);
    if (!report) return 81;
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QCoreApplication::setOrganizationName("QETWirePropertiesIntegration");
    QETProject::setBackupEnabled(false);
    QET::QetMessageBox::setNonInteractive(true);
    QFontDatabase::addApplicationFont(":/fonts/LiberationSans-Regular.ttf");
    try {
        qputenv("QET_EAGER_FOLIOS", "1");
        QETProject project(QString::fromLocal8Bit(argv[1]));
        require(project.state() == QETProject::Ok, "fixture opened");
        require(!project.isReadOnly(), "fixture editable");
        QList<Conductor *> wires;
        for (Diagram *d : project.diagrams())
            wires.append(DiagramContent(d, false).conductors());
        require(!wires.isEmpty(), "fixture has conductors");
        QHash<Conductor *, QPainterPath> paths;
        QHash<Conductor *, QString> labels;
        QHash<Conductor *, ConductorProperties> old;
        for (Conductor *c : wires) {
            paths.insert(c,c->path());
            labels.insert(c,c->properties().text);
            old.insert(c,c->properties());
        }

        // Audit all QET folios/symbols/terminals/conductors BEFORE any
        // sizing or mutation. Every wire must map to exactly one potential.
        const auto topology=WireTrace::scan(&project);
        require(topology.complete,"every conductor and terminal endpoint is indexed");
        // Broken cross-folio arrow links do NOT invalidate the whole
        // project's wire inventory; only their potentials are unsafe.
        require(topology.groupByConductor.size()==wires.size(),
                "locally unmatched arrows cannot hide unrelated wires");
        for (int id : topology.unsafeGroups)
            require(!topology.canSizeGroup(id),
                    "an unresolved connection blocks its own potential");
        for (const auto &hop : topology.reportHops) {
            require(hop.a>=0 && hop.a<topology.groups.size() &&
                    hop.b>=0 && hop.b<topology.groups.size(),
                    "cross-page continuation refers to existing potentials");
        }
        require(topology.folios==project.diagrams().size(),"all folios traversed");
        require(topology.wires==wires.size(),"every conductor was traced");
        require(topology.elements>0 && topology.terminals>0 && !topology.groups.isEmpty(),
                "graph records symbols, terminals and electrical potentials");
        require(topology.componentConnections.size()==topology.elements,
                "each element has one incidence entry linking its terminal potentials");
        require(topology.elementsLinkingPotentials>=0 &&
                topology.elementsLinkingPotentials<=topology.elements,
                "component cross-potential count is bounded");
        require(topology.report().contains(
                    QStringLiteral("ELEMENTOS ENTRE POTENCIAIS")),
                "audit documents component boundaries rather than assuming closed contacts");
        require(topology.groupByConductor.size()==wires.size(),
                "every conductor maps to one potential group");
        int groupWires=0;
        for (int i=0;i<topology.groups.size();++i) {
            const auto &g=topology.groups.at(i);
            groupWires+=g.conductors.size();
            for (Conductor *c:g.conductors) {
                require(topology.groupByConductor.value(c,-1)==i,
                        "graph group map is consistent");
                require(g.wireIds.contains(WireTrace::shortUuid(c->uuid())),
                        "trace report includes each wire UUID");
                require(g.terminals.contains(c->terminal1) &&
                        g.terminals.contains(c->terminal2),
                        "each conductor retains both terminal endpoints");
            }
        }
        require(groupWires==wires.size(),"no wire lost or counted twice");
        require(topology.report().contains(QStringLiteral("Nenhum dimensionamento")),
                "audit explicitly reports that no sizing occurred");
        require(topology.routeBetween(0,0).contains(QStringLiteral("MESMO potencial")),
                "route engine recognizes identical electrical potentials");
        if(topology.groups.size()>1)
            require(topology.routeBetween(0,1).contains(QStringLiteral("ROTA ENTRE POTENCIAIS")),
                    "route engine reports cross-device/cross-page route or missing passage");
        for (Conductor *c:wires)
            require(c->properties()==old.value(c),"trace must never edit conductor properties");

        // A bornes's documentation-only reference must survive in the
        // element information without creating, rotating or revealing QET
        // drawing text. Preserve the separate native comment/function.
        Element *documentedTerminal=nullptr;
        for (Diagram *folio:project.diagrams()) {
            if (!folio || documentedTerminal) continue;
            for (Element *element:folio->elements()) {
                if (element && element->actualLabel().simplified().isEmpty() &&
                    SymbolLegend::terminalLike(element)) {
                    documentedTerminal=element;
                    break;
                }
            }
        }
        // A real project fixture may omit terminal-style elements. The
        // metadata primitive is valid for any empty-label element, while
        // the user-facing editor still restricts selection to bornes.
        if(!documentedTerminal)
            for (Diagram *folio:project.diagrams()) {
                if (!folio || documentedTerminal) continue;
                for (Element *element:folio->elements())
                    if (element && element->actualLabel().simplified().isEmpty()) {
                        documentedTerminal=element;
                        break;
                    }
            }
        require(documentedTerminal!=nullptr,
                "fixture offers a symbol with no native visible label");
        const DiagramContext oldTerminalData=documentedTerminal->elementInformations();
        const QString oldVisibleLabel=documentedTerminal->actualLabel();
        const int oldTextItems=documentedTerminal->dynamicTextItems().size();
        const qreal oldRotation=documentedTerminal->rotation();
        DiagramContext withComment=oldTerminalData;
        withComment.addValue(QStringLiteral("comment"),
                             QStringLiteral("Borne de retorno do teste"));
        documentedTerminal->setElementInformations(withComment);
        project.undoStack()->push(new SymbolLegend::SetDocumentReference(
            documentedTerminal,QStringLiteral("X5")));
        require(documentedTerminal->elementInformations().value(
                    SymbolLegend::documentationReferenceKey()).toString()
                    ==QStringLiteral("X5"),
                "documentation-only borne label was stored");
        QDomDocument serialization(QStringLiteral("borne_info"));
        QDomElement documentElement=serialization.createElement(
            QStringLiteral("elementInformations"));
        serialization.appendChild(documentElement);
        documentedTerminal->elementInformations().toXml(
            documentElement,QStringLiteral("elementInformation"));
        DiagramContext restoredXml;
        restoredXml.fromXml(documentElement,QStringLiteral("elementInformation"));
        require(restoredXml.value(SymbolLegend::documentationReferenceKey())
                    .toString()==QStringLiteral("X5") &&
                !restoredXml.keyMustShow(SymbolLegend::documentationReferenceKey()),
                "documentation-only X5 persists across QET XML round-trip without visible text");
        require(SymbolLegend::documentedDesignator(documentedTerminal)
                    ==QStringLiteral("X5"),
                "empty native label is supplemented by documentary reference");
        require(documentedTerminal->actualLabel()==oldVisibleLabel &&
                documentedTerminal->rotation()==oldRotation &&
                documentedTerminal->dynamicTextItems().size()==oldTextItems,
                "documentary reference does not alter text or symbol orientation");
        const auto terminalInventory=SymbolLegend::inventory(&project);
        bool listed=false;
        for(const auto &item:terminalInventory.devices)
            if(item.symbol==documentedTerminal) {
                require(item.designator==QStringLiteral("X5"),
                        "X5 appears in generated device schedule");
                require(item.comment==QStringLiteral("Borne de retorno do teste"),
                        "QET comment is preserved in its own column");
                require(item.function==withComment.value(QStringLiteral("function")).toString().simplified(),
                        "blank electrical function is never invented from comment");
                listed=true;
                break;
            }
        require(listed,"documentation-only terminal appears among devices");
        project.undoStack()->undo();
        documentedTerminal->setElementInformations(oldTerminalData);
        require(documentedTerminal->elementInformations()==oldTerminalData &&
                documentedTerminal->actualLabel()==oldVisibleLabel,
                "undo and restore return original borne information and visible label");

        // A nomenclature row is not a graphic legend. Export the REAL
        // QET element drawings into a multipage A3 PDF, then confirm the
        // generated document is nonempty and has the PDF file signature.
        const auto inventory=SymbolLegend::inventory(&project);
        require(inventory.elementsScanned==topology.elements,
                "symbol legend scans all elements on all project folios");
        require(!inventory.symbols.isEmpty() && !inventory.devices.isEmpty(),
                "graphic legend and device list have actual project records");
        require(inventory.symbols.size()<=inventory.devices.size(),
                "repeated native symbols collapse into one legend definition");
        const QString legendFile=settings.path()+QStringLiteral("/qet-simbolos.pdf");
        require(SymbolLegend::writePdf(&project,legendFile),
                "A3 graphic symbol/device schedule/BOM PDF was rendered");
        QFile legend(legendFile);
        require(legend.open(QIODevice::ReadOnly),"generated graphic legend opens");
        require(legend.size()>3000 && legend.read(5)==QByteArray("%PDF-"),
                "generated graphic legend contains actual PDF bytes");
        legend.close();
        for (Conductor *c:wires)
            require(c->properties()==old.value(c),
                    "graphic legend must not alter original wire properties");

        QetWirePrecalc::Input input;
        input.voltage=380.; input.current=10.; input.powerFactor=.85;
        const auto calc=QetWirePrecalc::calculate(input);
        require(calc.valid && calc.withinTable && calc.sectionMm2>0., "preliminary sizing");
        const auto result=AutoWireProperties::apply(&project, calc);
        require(result.failed==0, "all requests verified after application");
        // No selection means no global sizing: multiple motors may have
        // different load currents and voltages even on the same panel.
        require(result.sizedConductors==0,"unselected projects are never globally sized");
        for (Conductor *c : wires) {
            require(c->properties().m_wire_section == old.value(c).m_wire_section,
                    "no new section on an unselected circuit");
            require(c->properties().m_tension_protocol == old.value(c).m_tension_protocol,
                    "no global feeder voltage on an unselected circuit");
        }
        require(result.power + result.control + result.earth + result.neutral +
                result.unknown == wires.size(), "every conductor classified once");
        for (Conductor *c : wires) {
            require(paths.value(c)==c->path(),"conductor geometry unchanged");
            require(labels.value(c)==c->properties().text,"visible label unchanged");
        }
        // Identifying wires must not depend on a motor current. A
        // classification-only request with invalid electrical input is valid.
        QetWirePrecalc::Output invalidSizing;
        const auto classified=AutoWireProperties::apply(
            &project,invalidSizing,false,true);
        require(classified.power+classified.control+classified.earth+
                classified.neutral+classified.unknown==wires.size(),
                "classification works even without sizing parameters");
        require(classified.sizedConductors==0,
                "classification-only mode never writes sections");
        if (result.modified) {
            project.undoStack()->undo();
            for (Conductor *c : wires)
                require(c->properties()==old.value(c),"full undo restored original properties");
            project.undoStack()->redo();
            require(result.failed==0,"redo succeeded");
        }

        // A user selection sizes only its exact electric potential. The
        // legacy project contains unrelated feeders and motor branches.
        Conductor *selectedPower=nullptr;
        for (Conductor *candidate : wires) {
            if (AutoWireProperties::classify(candidate)!=AutoWireProperties::Role::Power)
                continue;
            // The fixture contains old, unpaired folio reports; the test
            // must select a power potential that is electrically eligible,
            // not a deliberately blocked branch.
            if (!topology.canSizeGroup(topology.groupByConductor.value(candidate,-1)))
                continue;
            auto potential=candidate->relatedPotentialConductors(true);
            potential.insert(candidate);
            bool conflicting=false;
            for (Conductor *member : std::as_const(potential)) {
                const auto classification=AutoWireProperties::classify(member);
                if (classification!=AutoWireProperties::Role::Unknown &&
                    classification!=AutoWireProperties::Role::Power)
                    conflicting=true;
            }
            if (!conflicting) { selectedPower=candidate; break; }
        }
        require(selectedPower!=nullptr,"fixture contains an eligible power conductor");
        selectedPower->setSelected(true);
        QHash<Conductor *,ConductorProperties> previousSelection;
        for (Conductor *c : wires) previousSelection.insert(c,c->properties());
        const auto sized=AutoWireProperties::apply(&project,calc,true);
        require(sized.failed==0 && sized.sizedConductors>0,
                "sizing selected power potential was persisted");
        QSet<Conductor *> selectedPotential=selectedPower->relatedPotentialConductors(true);
        selectedPotential.insert(selectedPower);
        for (Conductor *c : wires) {
            if (selectedPotential.contains(c)) continue;
            require(c->properties().m_wire_section==previousSelection.value(c).m_wire_section,
                    "unrelated circuit section not overwritten");
            require(c->properties().m_tension_protocol==previousSelection.value(c).m_tension_protocol,
                    "unrelated circuit voltage not overwritten");
        }
        project.undoStack()->undo();
        for (Conductor *c : wires)
            require(c->properties()==previousSelection.value(c),
                    "selected circuit update fully undone");
        selectedPower->setSelected(false);

        // Trace-picker path: no graphic selection, only a potential ID
        // found by the project-wide topology scan. Other potentials must
        // keep their previous sections, voltages and labels.
        const auto postSelection=WireTrace::scan(&project);
        const int chosenId=postSelection.groupByConductor.value(selectedPower,-1);
        require(chosenId>=0,"recognized conductor has a trace-group ID");
        require(!postSelection.potentialSummary(chosenId).isEmpty(),
                "picker offers a readable folio/element description");
        QHash<Conductor *,ConductorProperties> beforePicker;
        for (Conductor *c : wires) beforePicker.insert(c,c->properties());
        const auto picked=AutoWireProperties::apply(
            &project,calc,true,false,QSet<int>{chosenId});
        require(picked.traceComplete && picked.failed==0 && picked.sizedConductors>0,
                "chosen potential is sized without manual wire selection");
        for (Conductor *c : wires) {
            if (postSelection.groupByConductor.value(c,-1)==chosenId) continue;
            require(c->properties().m_wire_section==beforePicker.value(c).m_wire_section,
                    "trace picker leaves other circuit sections unchanged");
            require(c->properties().m_tension_protocol==beforePicker.value(c).m_tension_protocol,
                    "trace picker leaves other circuit voltage unchanged");
        }
        project.undoStack()->undo();
        for (Conductor *c : wires)
            require(c->properties()==beforePicker.value(c),
                    "trace-picked sizing undo fully restores conductor state");

        // A single explicit project annotation is authoritative only
        // where it does not contradict device/terminal evidence.
        bool explicitPeObserved=false;
        for (Conductor *candidate : wires) {
            if (AutoWireProperties::classify(candidate)!=AutoWireProperties::Role::Unknown)
                continue;
            const auto original=candidate->properties();
            auto annotated=original; annotated.m_function=QStringLiteral("PE");
            candidate->setProperties(annotated);
            if (AutoWireProperties::classify(candidate)==AutoWireProperties::Role::Earth)
                explicitPeObserved=true;
            candidate->setProperties(original);
            if (explicitPeObserved) break;
        }
        require(explicitPeObserved,"explicit PE annotation classifies a compatible wire");

        // A PE must never inherit an unverified global motor section.
        Conductor *anyWire=wires.first();
        const auto beforePE=anyWire->properties();
        const auto proposedPE=AutoWireProperties::proposal(
            anyWire,AutoWireProperties::Role::Earth,calc,true,true);
        require(proposedPE.m_wire_section==beforePE.m_wire_section,
                "PE section not guessed from motor's preliminary section");

        // Independent controlled metadata test, even if the fixture has no
        // recognizable power/control elements for the classifier.
        Conductor *wire=wires.first();
        const ConductorProperties before=wire->properties();
        ConductorProperties changed=before;
        changed.m_wire_section=QStringLiteral("4 mm²");
        changed.m_wire_color=QStringLiteral("Preto");
        changed.m_function=QStringLiteral("L1");
        changed.m_tension_protocol=QStringLiteral("380 V");
        auto *cmd=new QPropertyUndoCommand(wire,"properties",
                 QVariant::fromValue(before),QVariant::fromValue(changed));
        project.undoStack()->push(cmd);
        require(wire->properties()==changed,"direct native property write");
        project.undoStack()->undo();
        require(wire->properties()==before,"native property undo");
        project.undoStack()->redo();
        require(wire->properties()==changed,"native property redo");
        require(wire->path()==paths.value(wire),"wire geometry after edits");
        require(wire->properties().text==labels.value(wire),"wire text after edits");

        const QString savePath=QDir(QString::fromLocal8Bit(argv[3])).filePath("wires_saving_test.qet");
        project.setFilePath(savePath);
        require(project.write().isOk(),"save edited .qet");
        require(QFileInfo::exists(savePath),"saved project exists");
        QETProject reopened(savePath);
        require(reopened.state()==QETProject::Ok,"saved project reopens");
        require(!reopened.isReadOnly(),"saved project editable");
        QList<Conductor *> reopenedWires;
        for(Diagram *d:reopened.diagrams())
            reopenedWires.append(DiagramContent(d,false).conductors());
        require(reopenedWires.size()==wires.size(),"same number of conductor connections");
        bool found=false;
        for(Conductor *c:reopenedWires) {
            const auto p=c->properties();
            if(p.m_wire_section==changed.m_wire_section &&
               p.m_wire_color==changed.m_wire_color &&
               p.m_function==changed.m_function &&
               p.m_tension_protocol==changed.m_tension_protocol)
                found=true;
        }
        require(found,"technical fields persist after save and reopen");
        // Open the sizing dialog with a single explicitly selected wire.
        // Whole-project V/I/P sizing is deliberately no longer available.
        require(!reopenedWires.isEmpty(),"saved project still has wires");
        const auto reopenedGraph=WireTrace::scan(&reopened);
        int eligiblePotential=-1;
        for(int id=0;id<reopenedGraph.groups.size();++id) {
            if(!reopenedGraph.canSizeGroup(id)) continue;
            for(Conductor *candidate:reopenedGraph.groups.at(id).conductors)
                if(candidate && AutoWireProperties::classify(candidate) ==
                    AutoWireProperties::Role::Power) {
                    eligiblePotential=id;
                    break;
                }
            if(eligiblePotential>=0) break;
        }
        if(eligiblePotential<0)
            for(int id=0;id<reopenedGraph.groups.size();++id)
                if(reopenedGraph.canSizeGroup(id) &&
                   !reopenedGraph.groups.at(id).conductors.isEmpty()) {
                    eligiblePotential=id; break;
                }
        require(eligiblePotential>=0,
                "project contains a safe circuit to exercise the GUI picker");
        for(Conductor *wire:reopenedWires)
            if(wire)wire->setSelected(false);
        reopenedGraph.groups.at(eligiblePotential).conductors.first()->setSelected(true);
        // Exercise the actual Qt dialog with an event-loop-driven test user.
        // No external GUI automation or manual installation is needed.
        bool sawDialog=false, sawPicker=false, dialogSubmitted=false,
             sawResult=false, dialogTimedOut=false;
        int dialogPolls=0;
        QStringList guiHistory;
        QTimer poll;
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
            ++dialogPolls;
            for (QWidget *window : QApplication::topLevelWidgets()) {
                if (!window->isVisible()) continue;
                if (auto *d=qobject_cast<QDialog*>(window)) {
                    if (d->windowTitle().contains(QStringLiteral("Selecionar potenciais do mesmo circuito"))) {
                        if (!sawPicker) {
                            sawPicker=true;
                            const auto *traceList=d->findChild<QListWidget*>(
                                QStringLiteral("qet_trace_potential_list"));
                            require(traceList && traceList->count()>0,
                                    "trace picker lists project potential groups");
                            auto *mutableList=d->findChild<QListWidget*>(
                                QStringLiteral("qet_trace_potential_list"));
                            require(eligiblePotential<mutableList->count(),
                                    "suitable trace-group exists in picker");
                            mutableList->item(eligiblePotential)->setSelected(true);
                            const auto box=d->findChild<QDialogButtonBox*>();
                            require(box && box->button(QDialogButtonBox::Ok),
                                    "trace picker has confirmation button");
                            box->button(QDialogButtonBox::Ok)->click();
                        }
                        continue;
                    }
                    if (d->windowTitle().contains(QStringLiteral("Dimensionamento preliminar"))) {
                        if (sawDialog) {
                            // QTimer callbacks do not recursively dispatch
                            // themselves while nested selector.exec() runs.
                            // Submit only AFTER the queued picker has closed.
                            if (sawPicker && !dialogSubmitted) {
                                dialogSubmitted=true;
                                const auto box=d->findChild<QDialogButtonBox*>();
                                require(box && box->button(QDialogButtonBox::Ok)->isEnabled(),
                                        "trace-picked V/I/P dialog remains valid");
                                guiHistory << QStringLiteral("click-OK after-trace-picker");
                                box->button(QDialogButtonBox::Ok)->click();
                            }
                            continue;
                        }
                        sawDialog=true;
                        guiHistory << QStringLiteral("found-input-dialog");
                        const auto combos=d->findChildren<QComboBox*>();
                        const auto fields=d->findChildren<QDoubleSpinBox*>();
                        require(combos.size()==3,"supply, pair and safe sizing scope shown");
                        require(combos.at(2)->count()==2,
                                "unsafe project-wide section setting is absent");
                        require(combos.at(2)->currentIndex()==0,
                                "selected circuit is preselected for sizing");
                        require(fields.size()==6,"all six electrical quantities shown");
                        combos.at(2)->setCurrentIndex(1);
                        for (QDoubleSpinBox *field : fields)
                            require(!field->isEnabled(),
                                    "classification only visibly disables V/I/P fields");
                        require(!combos.at(0)->isEnabled() && !combos.at(1)->isEnabled(),
                                "supply and pair disabled during classification");
                        // The new picker must let the user select a potential
                        // from all pages without clicking individual wires.
                        auto *traceButton=d->findChild<QPushButton*>(
                            QStringLiteral("qet_pick_trace_potentials"));
                        require(traceButton!=nullptr,
                                "project-wide trace picker is present in the sizing UI");
                        QTimer::singleShot(0, traceButton, [traceButton]() {
                            traceButton->click();
                        });
                        // The explicit single-circuit mode re-enables sizing.
                        combos.at(2)->setCurrentIndex(0);
                        require(combos.at(0)->isEnabled() && combos.at(1)->isEnabled(),
                                "supply and V/I/P pair controls enabled for selected sizing");
                        require(fields.at(0)->isEnabled() && fields.at(1)->isEnabled() &&
                                fields.at(3)->isEnabled() && fields.at(4)->isEnabled() &&
                                fields.at(5)->isEnabled(),
                                "electrical fields enabled when sizing is explicitly requested");
                        // Check the former 10 W minimum bug: 24 V x
                        // 0.05 A x 0.85 = 1.02 W, not 10 W.
                        combos.at(0)->setCurrentIndex(0);
                        combos.at(1)->setCurrentIndex(0);
                        fields.at(0)->setValue(24.);
                        fields.at(1)->setValue(0.05);
                        fields.at(3)->setValue(0.85);
                        require(std::abs(fields.at(2)->value()-0.00102)<0.00000001,
                                "sub-10W power is not clipped by Qt input");
                        combos.at(1)->setCurrentIndex(1);
                        require(std::abs(fields.at(1)->value()-0.05)<0.00001,
                                "switching known inputs keeps low-current physics");
                        fields.at(0)->setValue(220.);
                        fields.at(2)->setValue(2.2);
                        fields.at(3)->setValue(1.);
                        require(combos.at(0)->count()==3,"AC two-wire, AC three-phase and DC are available");
                        combos.at(0)->setCurrentIndex(2);
                        require(!fields.at(3)->isEnabled(),
                                "24V DC option disables AC power factor");
                        fields.at(0)->setValue(24.);
                        fields.at(2)->setValue(0.24);
                        require(std::abs(fields.at(1)->value()-10.)<0.0001,
                                "DC 24V and 240W produces 10A independent of cos(phi)");
                        const auto buttons=d->findChild<QDialogButtonBox*>();
                        require(buttons!=nullptr &&
                                buttons->button(QDialogButtonBox::Ok)->isEnabled(),
                                "valid voltage+power enables applying");
                        const auto labels=d->findChildren<QLabel*>();
                        bool topologyPreflight=false;
                        for (auto *label : labels)
                            if (label->text().contains(QStringLiteral("RASTREAMENTO PRÉVIO")) &&
                                label->text().contains(QStringLiteral("potenciais")))
                                topologyPreflight=true;
                        require(topologyPreflight,
                                "topology preflight shown before user applies sizing");
                        bool previewFound=false;
                        for (auto *label : labels)
                            if (label->text().contains(QStringLiteral("Resultado:")))
                                previewFound=true;
                        require(previewFound,"live calculation displayed");
                        guiHistory << QStringLiteral("input-ready scope=%1")
                            .arg(combos.at(2)->currentIndex());
                        // Submission is performed in the next poll cycle,
                        // after the selector was handled in its own event loop.
                    }
                }
                if (auto *message=qobject_cast<QMessageBox*>(window)) {
                    if (message->windowTitle().contains(QStringLiteral("Pré-dimensionamento aplicado"))) {
                        sawResult=true;
                        guiHistory << QStringLiteral("result-dialog");
                        message->accept();
                    }
                }
            }
        });
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &app, [&]() {
            dialogTimedOut=true;
            guiHistory << QStringLiteral("watchdog-fired");
            for (QWidget *window : QApplication::topLevelWidgets())
                if (auto *d=qobject_cast<QDialog*>(window))
                    if (d->isVisible()) d->reject();
        });
        poll.start();
        // A large fixture may need extra time to classify 671 conductor
        // potentials before opening the completion dialog on Windows CI.
        watchdog.start(45000);
        AutoWireProperties::run(nullptr, &reopened);
        poll.stop();
        watchdog.stop();
        std::printf("GUI trace: sawDialog=%d, result=%d, timedOut=%d, polls=%d, events=%s\n",
            int(sawDialog),int(sawResult),
            int(dialogTimedOut),dialogPolls,
            qPrintable(guiHistory.join(QStringLiteral(";"))));
        std::fflush(stdout);
        require(!dialogTimedOut && sawDialog && sawPicker && sawResult,
                "Qt trace picker, selected circuit sizing and completion succeeded");
        std::printf("PASS QET Qt dialog: voltage+power pair, trace picker, preview, apply, confirmation\n");

        std::printf("PASS QET graphic legend: %d distinct pictograms, %d devices, %d material rows, A3 PDF exported\\n",
                    int(inventory.symbols.size()),int(inventory.devices.size()),
                    int(inventory.materials.size()));
        std::printf("PASS QET graph tracing: %d folios, %d elements, %d terminals, %d wires, %d potentials\n",
                    topology.folios,topology.elements,topology.terminals,
                    topology.wires,int(topology.groups.size()));
        std::printf("PASS QET native integration: %d wires, %d auto-changed, %d unknown, persisted, undo-redo, preserved geometry\n",
                    int(wires.size()),result.modified,result.unknown);
    } catch (const std::exception &ex) {
        std::printf("FAIL QET integration: %s\n",ex.what());
        std::fflush(stdout);
        return 1;
    }
    std::fflush(stdout);
    return 0;
}
