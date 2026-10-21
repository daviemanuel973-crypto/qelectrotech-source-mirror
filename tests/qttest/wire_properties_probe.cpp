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
#include <stdexcept>
#include <cstdio>
#include "../../sources/QPropertyUndoCommand/qpropertyundocommand.h"
#include "../../sources/automation/autowireproperties.h"
#include "../../sources/automation/wireprecalc.h"
#include "../../sources/diagram.h"
#include "../../sources/diagramcontent.h"
#include "../../sources/qetmessagebox.h"
#include "../../sources/qetproject.h"
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
        require(result.power + result.control + result.earth + result.neutral +
                result.unknown == wires.size(), "every conductor classified once");
        for (Conductor *c : wires) {
            require(paths.value(c)==c->path(),"conductor geometry unchanged");
            require(labels.value(c)==c->properties().text,"visible label unchanged");
        }
        if (result.modified) {
            project.undoStack()->undo();
            for (Conductor *c : wires)
                require(c->properties()==old.value(c),"full undo restored original properties");
            project.undoStack()->redo();
            require(result.failed==0,"redo succeeded");
        }

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
