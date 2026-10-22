// GPL-2.0-or-later - integration check against the real QET object library.
// The fixture is an upstream QET example, never a user's proprietary diagram.
#include <QApplication>
#include <QDir>
#include <QFileInfo>
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
#include <stdexcept>
#include <cstdio>
#include <cmath>
#include <utility>
#include "../../sources/QPropertyUndoCommand/qpropertyundocommand.h"
#include "../../sources/automation/autowireproperties.h"
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
            &project,invalidSizing,false,false,true);
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
        require(selectedPower!=nullptr,"fixture contains a recognized power conductor");
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
        // Exercise the actual Qt dialog with an event-loop-driven test user.
        // No external GUI automation or manual installation is needed.
        bool sawDialog=false, sawResult=false, sawGlobalWarning=false,
             dialogTimedOut=false;
        int dialogPolls=0;
        QStringList guiHistory;
        QTimer poll;
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
            ++dialogPolls;
            for (QWidget *window : QApplication::topLevelWidgets()) {
                if (!window->isVisible()) continue;
                if (auto *d=qobject_cast<QDialog*>(window)) {
                    if (d->windowTitle().contains(QStringLiteral("Dimensionamento preliminar"))) {
                        if (sawDialog) continue;
                        sawDialog=true;
                        guiHistory << QStringLiteral("found-input-dialog");
                        const auto combos=d->findChildren<QComboBox*>();
                        const auto fields=d->findChildren<QDoubleSpinBox*>();
                        require(combos.size()==3,"supply, pair and safe sizing scope shown");
                        require(combos.at(2)->currentIndex()==1,
                                "with no selection the default is classification only");
                        // Exercise the explicit all-circuits option, including
                        // its required electrical safety confirmation.
                        combos.at(2)->setCurrentIndex(2);
                        require(fields.size()==6,"all six electrical quantities shown");
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
                        const auto buttons=d->findChild<QDialogButtonBox*>();
                        require(buttons!=nullptr &&
                                buttons->button(QDialogButtonBox::Ok)->isEnabled(),
                                "valid voltage+power enables applying");
                        const auto labels=d->findChildren<QLabel*>();
                        bool previewFound=false;
                        for (auto *label : labels)
                            if (label->text().contains(QStringLiteral("Resultado:")))
                                previewFound=true;
                        require(previewFound,"live calculation displayed");
                        guiHistory << QStringLiteral("click-OK enabled=%1 scope=%2")
                            .arg(buttons->button(QDialogButtonBox::Ok)->isEnabled())
                            .arg(combos.at(2)->currentIndex());
                        buttons->button(QDialogButtonBox::Ok)->click();
                    }
                }
                if (auto *message=qobject_cast<QMessageBox*>(window)) {
                    if (message->windowTitle().contains(QStringLiteral("Dimensionamento global não verificado"))) {
                        sawGlobalWarning=true;
                        guiHistory << QStringLiteral("global-safety-warning");
                        message->done(QMessageBox::Yes);
                    }
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
        std::printf("GUI trace: sawDialog=%d, warning=%d, result=%d, timedOut=%d, polls=%d, events=%s\n",
            int(sawDialog),int(sawGlobalWarning),int(sawResult),
            int(dialogTimedOut),dialogPolls,
            qPrintable(guiHistory.join(QStringLiteral(";"))));
        std::fflush(stdout);
        require(!dialogTimedOut && sawDialog && sawResult && sawGlobalWarning,
                "Qt inputs, advanced scope warning and completion succeeded");
        std::printf("PASS QET Qt dialog: voltage+power pair, preview, apply, confirmation\n");

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
