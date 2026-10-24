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
#include "../../sources/automation/industrialdocumentation.h"
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
static void checkAutomaticLists(QETProject &project,const QString &outputDir) {
    {
        QETProject empty;
        const int initial=empty.diagrams().size();
        require(IndustrialDocumentation::createMaterials(&empty)==1 &&
                IndustrialDocumentation::createSymbols(&empty)==1,
                "empty project generates explicit empty lists without failure");
        const int complete=IndustrialDocumentation::createSheets(&empty);
        require(complete==11 && empty.diagrams().size()==initial+complete,
                "complete generation replaces selective empty lists");
        require(IndustrialDocumentation::createSheets(&empty)==complete &&
                empty.diagrams().size()==initial+complete,"complete refresh does not accumulate copies");
    }
    const auto baseline=SymbolLegend::inventory(&project);
    require(baseline.materials.size()>=2,"fixture has two physical devices for BOM grouping");
    Element *first=baseline.materials.at(0).symbol,*second=baseline.materials.at(1).symbol;
    const auto firstInfo=first->elementInformations(),secondInfo=second->elementInformations();
    auto info=firstInfo;
    info.addValue("manufacturer","QET BOM regression",false);
    info.addValue("manufacturer_reference","BOM-TEST-42",false);
    info.addValue("model","Model A",false);info.addValue("quantity","1",false);
    info.addValue("exclude_from_bom","false",false);
    first->setElementInformations(info);
    info=secondInfo;
    info.addValue("manufacturer","QET BOM regression",false);
    info.addValue("manufacturer_reference","BOM-TEST-42",false);
    info.addValue("model","Model A",false);info.addValue("quantity","1",false);
    info.addValue("exclude_from_bom","false",false);
    second->setElementInformations(info);
    auto grouped=SymbolLegend::inventory(&project);
    int testRows=0;
    for(const auto &r:grouped.materials)if(r.manufacturer=="QET BOM regression") {
        ++testRows;require(r.quantity==2 && r.references.size()==2,"BOM counts matching commercial parts once per device");
    }
    require(testRows==1,"same manufacturer, code and model produce one material row");
    info.addValue("model","Model B",false);second->setElementInformations(info);
    grouped=SymbolLegend::inventory(&project);testRows=0;
    for(const auto &r:grouped.materials)if(r.manufacturer=="QET BOM regression") {
        ++testRows;require(r.quantity==1,"different models must not merge in BOM");
    }
    require(testRows==2,"different models produce separate material rows");
    info.addValue("manufacturer_reference","",false);second->setElementInformations(info);
    bool pending=false;
    for(const auto &r:IndustrialDocumentation::materialTable(SymbolLegend::inventory(&project)).rows)
        if(r.at(4)=="QET BOM regression" && r.at(2).isEmpty())pending=r.at(6).contains("código comercial");
    require(pending,"missing product code is explicitly pending in BOM output");
    info.addValue("manufacturer_reference","BOM-TEST-42",false);
    info.addValue("model","Model A",false);info.addValue("quantity","2,5",false);
    second->setElementInformations(info);
    auto firstQuantity=first->elementInformations();firstQuantity.addValue("quantity","3",false);
    first->setElementInformations(firstQuantity);
    grouped=SymbolLegend::inventory(&project);testRows=0;
    for(const auto &r:grouped.materials)if(r.manufacturer=="QET BOM regression") {
        ++testRows;require(r.quantityKnown && std::abs(r.quantity-5.5)<1e-9,"BOM sums declared native quantities, including decimal comma");
    }
    require(testRows==1,"declared quantities aggregate by commercial identity");
    info.addValue("exclude_from_bom","yes",false);second->setElementInformations(info);
    grouped=SymbolLegend::inventory(&project);testRows=0;
    for(const auto &r:grouped.materials)if(r.manufacturer=="QET BOM regression") {
        ++testRows;require(r.quantity==3,"excluded component is absent from total quantity");
    }
    require(testRows==1,"native BOM exclusion is honored");
    info.addValue("exclude_from_bom","false",false);info.addValue("quantity","2 m",false);
    second->setElementInformations(info);grouped=SymbolLegend::inventory(&project);bool rawPreserved=false;
    for(const auto &r:grouped.materials)if(r.symbol==second)
        rawPreserved=!r.quantityKnown && SymbolLegend::materialQuantity(r)=="2 m";
    require(rawPreserved,"unit-bearing quantity is preserved without inventing conversion or aggregation");
    first->setElementInformations(firstInfo);second->setElementInformations(secondInfo);
    Element *slave=nullptr,*master=nullptr;
    for(const auto &r:baseline.devices)if(!slave && (r.symbol->linkType() & Element::Slave))
        for(Element *linked:r.symbol->linkedElements())if(linked && (linked->linkType() & Element::Master)) {
            slave=r.symbol;master=linked;break;
        }
    require(slave && master,"fixture contains a linked contact for auxiliary part policy");
    const auto slaveInfo=slave->elementInformations();info=slaveInfo;
    info.addValue("manufacturer","QET auxiliary regression",false);
    info.addValue("manufacturer_reference","AUX-BLOCK-TEST",false);
    info.addValue("quantity","1",false);info.addValue("exclude_from_bom","false",false);
    slave->setElementInformations(info);bool accessoryListed=false;
    for(const auto &r:SymbolLegend::inventory(&project).materials)if(r.symbol==slave)accessoryListed=true;
    require(accessoryListed,"linked contact with its own order code remains a purchasable accessory");
    info.addValue("manufacturer_reference","",false);slave->setElementInformations(info);
    bool repeatedViewListed=false;
    for(const auto &r:SymbolLegend::inventory(&project).materials)if(r.symbol==slave)repeatedViewListed=true;
    require(!repeatedViewListed,"linked contact without own order data does not duplicate its master");
    slave->setElementInformations(slaveInfo);

    const auto originalPages=project.diagrams();
    const int undoIndex=project.undoStack()->index();
    // A title alone is never permission to replace a manually authored folio.
    Diagram *manual=project.addNewDiagram();auto props=manual->border_and_titleblock.exportTitleBlock();
    props.title="RELAÇÃO DE MATERIAIS";manual->border_and_titleblock.importTitleBlock(props);
    IndustrialDocumentation::text(manual,"PRESERVAR FOLHA MANUAL",QRectF(30,30,400,60));
    const int materialPages=IndustrialDocumentation::createMaterials(&project);
    require(materialPages>0 && project.diagrams().size()==originalPages.size()+1+materialPages,
            "dedicated materials function creates only material folios");
    QList<Diagram*> previousMaterials;
    for(Diagram *d:project.diagrams())if(IndustrialDocumentation::generated(d)) {
        require(IndustrialDocumentation::sheetKind(d)=="materials","BOM function does not generate unrelated tables");
        previousMaterials<<d;
    }
    IndustrialDocumentation::text(previousMaterials.first(),"EDIT BEFORE REFRESH",QRectF(30,720,400,35));
    auto legacy=previousMaterials.first()->border_and_titleblock.exportTitleBlock();
    legacy.context.remove("qet_documentation_kind");
    previousMaterials.first()->border_and_titleblock.importTitleBlock(legacy);
    const int symbolPages=IndustrialDocumentation::createSymbols(&project);
    require(symbolPages==(baseline.symbols.size()+20)/21,"dedicated symbol function paginates actual definitions");
    QList<Diagram*> previousSymbols;
    for(Diagram *d:project.diagrams())if(IndustrialDocumentation::sheetKind(d)=="symbols")previousSymbols<<d;
    for(Diagram *d:previousMaterials)require(project.diagrams().contains(d),"symbol generation retains material sheets");
    info=firstInfo;info.addValue("manufacturer_reference","UPDATED-CODE-123",false);first->setElementInformations(info);
    const int refreshedMaterialPages=IndustrialDocumentation::createMaterials(&project);
    require(refreshedMaterialPages>0,"material refresh generates current data");
    require(project.diagrams().size()==originalPages.size()+1+refreshedMaterialPages+symbolPages,
            "refresh replaces previous BOM instead of accumulating duplicates");
    bool updated=false;
    for(Diagram *d:project.diagrams())if(IndustrialDocumentation::sheetKind(d)=="materials")
        for(auto *item:d->items())if(auto *t=qgraphicsitem_cast<IndependentTextItem*>(item))
            if(t->toPlainText().contains("UPDATED-CODE-123"))updated=true;
    require(updated,"refresh reads changed real component data into native table");
    for(Diagram *d:previousSymbols)require(project.diagrams().contains(d),"material refresh preserves symbol sheets");
    require(project.diagrams().contains(manual),"manual sheet with generated title survives refresh");
    project.undoStack()->undo();
    for(Diagram *d:previousMaterials)require(project.diagrams().contains(d),"single undo restores previous material pages");
    bool editRestored=false;
    for(auto *item:previousMaterials.first()->items())if(auto *t=qgraphicsitem_cast<IndependentTextItem*>(item))
        if(t->toPlainText()=="EDIT BEFORE REFRESH")editRestored=true;
    require(editRestored,"refresh undo recovers manual edits on old automatic sheets");
    project.undoStack()->redo();
    require(SymbolLegend::inventory(&project).elementsScanned==baseline.elementsScanned,
            "automatic documents never inflate source component quantities");
    const QString previousPath=project.filePath();
    const QString path=QDir(outputDir).filePath("QET-Listas-Automaticas.qet");
    project.setFilePath(path);require(project.write().isOk(),"selective lists save as native QET");
    project.setFilePath(previousPath);
    {
        QETProject saved(path);require(saved.state()==QETProject::Ok,"selective lists reopen");
        QList<Diagram*> savedMaterials;
        for(Diagram *d:saved.diagrams())if(IndustrialDocumentation::sheetKind(d)=="materials")savedMaterials<<d;
        require(savedMaterials.size()==refreshedMaterialPages,"material ownership marker survives XML");
        require(IndustrialDocumentation::createSymbols(&saved)==symbolPages &&
                saved.diagrams().size()==originalPages.size()+1+refreshedMaterialPages+symbolPages,
                "symbol refresh after reopen does not duplicate pages");
        for(Diagram *d:savedMaterials)require(saved.diagrams().contains(d),"persisted sections refresh independently");
    }
    while(project.undoStack()->index()>undoIndex)project.undoStack()->undo();
    require(project.diagrams()==originalPages,"undo restores exact original folio order");
    first->setElementInformations(firstInfo);
    std::printf("PASS QET automatic lists: grouped BOM, native quantities/exclusions, auxiliary parts, pending data, independent refresh, legacy/manual/empty sheets, undo/redo, save/reopen\n");
    std::fflush(stdout);
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
        const QString longDescription=QStringLiteral("Descrição do borne X5: conexão de proteção 🔌 e revisão. ").repeated(160);
        const QStringList longRow{QStringLiteral("X5"),longDescription};
        const QList<int> columnWidths{80,180};
        const auto parts=IndustrialDocumentation::splitNativeRow(longRow,columnWidths,120);
        require(parts.size()>1,"long terminal descriptions continue across native rows");
        QStringList restored{QString(),QString()};
        for(const auto &part:parts) {
            require(IndustrialDocumentation::nativeRowHeight(part,columnWidths)<=120,
                    "every continued row fits its printable page");
            for(int col=0;col<part.size();++col)restored[col]+=part[col];
        }
        require(restored==longRow,"pagination preserves every description character, including Unicode");
        qputenv("QET_EAGER_FOLIOS", "1");
        QETProject project(QString::fromLocal8Bit(argv[1]));
        require(project.state() == QETProject::Ok, "fixture opened");
        require(!project.isReadOnly(), "fixture editable");
        QList<Conductor *> wires;
        for (Diagram *d : project.diagrams())
            wires.append(DiagramContent(d, false).conductors());
        require(!wires.isEmpty(), "fixture has conductors");
        Element *roleDevice=wires.first()->terminal1->parentElement();
        require(roleDevice,"fixture offers an actual device for electrical roles");
        const auto savedRoleData=roleDevice->elementInformations();
        auto roleData=savedRoleData;
        roleData.addValue("qet_current_a","25",false);
        roleData.addValue("qet_electrical_role","protection",false);
        roleDevice->setElementInformations(roleData);
        require(!ElectricalData::load(roleDevice),"protector nominal current is not a load");
        roleData.addValue("qet_electrical_role","source",false);
        roleDevice->setElementInformations(roleData);
        require(!ElectricalData::load(roleDevice),"source current rating is not a downstream load");
        roleData.addValue("qet_electrical_role","unknown",false);
        roleDevice->setElementInformations(roleData);
        require(!ElectricalData::load(roleDevice),"unknown device current is not invented load evidence");
        roleData.addValue("qet_electrical_role","load",false);
        roleDevice->setElementInformations(roleData);
        require(ElectricalData::load(roleDevice),"explicit load role is used regardless of symbol naming");
        roleDevice->setElementInformations(savedRoleData);
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
        bool checkedConflict=false;
        for(const auto &group:topology.groups) {
            if(group.conductors.size()<2)continue;
            Conductor *a=group.conductors[0],*b=group.conductors[1];
            auto pa=a->properties(),pb=b->properties();
            pa.m_function="L1";pb.m_function="L2";a->setProperties(pa);b->setProperties(pb);
            const auto conflicting=WireTrace::scan(&project);
            require(!conflicting.canSizeGroup(conflicting.groupByConductor.value(a,-1)),
                    "a continuous potential with conflicting explicit phases cannot be sized");
            a->setProperties(old.value(a));b->setProperties(old.value(b));
            checkedConflict=true;break;
        }
        require(checkedConflict,"phase-conflict regression exercised a real branched potential");
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
        QSet<Conductor*> partition;
        for(const auto &group:topology.groups)for(auto *member:group.conductors) {
            require(!partition.contains(member),"overlapping native potentials cannot duplicate a physical wire");
            partition.insert(member);
        }
        require(partition.size()==wires.size(),"potential partition covers every wire exactly once");
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
            documentedTerminal,QStringLiteral("X5"),
            QStringLiteral("Borne de retorno do teste"),
            QStringLiteral("Retorno da bomba"),
            QStringLiteral("Borne de conexão 24 Vcc")));
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
        require(restoredXml.value(QStringLiteral("comment")).toString() ==
                    QStringLiteral("Borne de retorno do teste") &&
                restoredXml.value(QStringLiteral("function")).toString() ==
                    QStringLiteral("Retorno da bomba") &&
                restoredXml.value(QStringLiteral("description")).toString() ==
                    QStringLiteral("Borne de conexão 24 Vcc"),
                "all terminal documentation fields persist in native QET XML");
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
                require(item.function==QStringLiteral("Retorno da bomba"),
                        "missing native Function field is stored for terminal documentation");
                require(item.description==QStringLiteral("Borne de conexão 24 Vcc"),
                        "missing native textual Description is stored for terminal documentation");
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
        QetWirePrecalc::Input controlLoad=input;
        controlLoad.current=38.;
        controlLoad.lengthMeters=140.;
        const auto controlSizing=QetWirePrecalc::calculate(controlLoad);
        require(controlSizing.valid && controlSizing.withinTable &&
                controlSizing.sectionMm2>1.5,
                "real electrical input for long control circuit needs more than 1.5mm2");
        const auto proposedControl=AutoWireProperties::proposal(
            wires.first(),AutoWireProperties::Role::Control,
            controlSizing,true,true);
        require(proposedControl.m_wire_section==
                    AutoWireProperties::sectionText(controlSizing.sectionMm2),
                "control circuit sizing uses calculated section, never fixed 1.5mm2");
        const auto proposedUnclassified=AutoWireProperties::proposal(
            wires.first(),AutoWireProperties::Role::Unknown,
            controlSizing,true,true);
        require(proposedUnclassified.m_wire_section==
                    AutoWireProperties::sectionText(controlSizing.sectionMm2),
                "explicitly selected unclassified circuit accepts user-verified sizing");
        const auto proposedPE=AutoWireProperties::proposal(
            wires.first(),AutoWireProperties::Role::Earth,
            controlSizing,false,true);
        require(proposedPE.m_wire_section==wires.first()->properties().m_wire_section,
                "PE section is never assigned from a generic load");

        for(auto *c:wires)c->setSelected(false);
        const auto result=AutoWireProperties::apply(&project, calc);
        std::printf("Classification counts: %d + %d + %d + %d + %d; wires=%d traceComplete=%d\n",
            result.power,result.control,result.earth,result.neutral,result.unknown,int(wires.size()),int(result.traceComplete));
        std::fflush(stdout);
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
            const int candidateId=topology.groupByConductor.value(candidate,-1);
            if (!topology.canSizeGroup(candidateId) ||
                ElectricalData::loads(topology,QSet<int>{candidateId}).size()>1)continue;
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
        const auto proposedPreservedPE=AutoWireProperties::proposal(
            anyWire,AutoWireProperties::Role::Earth,calc,true,true);
        require(proposedPreservedPE.m_wire_section==beforePE.m_wire_section,
                "PE section not guessed from motor's preliminary section");

        bool multiLoadBlocked=false;
        for(int id=0;id<topology.groups.size();++id) {
            if(!topology.canSizeGroup(id) || ElectricalData::loads(topology,QSet<int>{id}).size()<2)continue;
            QHash<Conductor*,ConductorProperties> snapshot;
            for(auto*c:wires)snapshot.insert(c,c->properties());
            const auto rejected=AutoWireProperties::apply(&project,calc,true,false,QSet<int>{id});
            require(!rejected.traceComplete && rejected.sizedConductors==0 && rejected.modified==0,
                    "multiple load branches cannot receive one generic conductor section");
            for(auto*c:wires)require(c->properties()==snapshot.value(c),"blocked branch sizing is atomic");
            multiLoadBlocked=true;break;
        }
        require(multiLoadBlocked,"real industrial fixture exercises ambiguous shared feeders");
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
        for(Conductor *c:reopenedWires)if(!c->properties().m_sizing_data.isEmpty()) {
            require(QJsonDocument::fromJson(c->properties().m_sizing_data.toUtf8()).isObject(),
                    "calculation audit persists as valid versioned JSON in native XML");
        }
        // Open the sizing dialog with a single explicitly selected wire.
        // Whole-project V/I/P sizing is deliberately no longer available.
        require(!reopenedWires.isEmpty(),"saved project still has wires");
        const auto reopenedGraph=WireTrace::scan(&reopened);
        int eligiblePotential=-1;
        for(int id=0;id<reopenedGraph.groups.size();++id) {
            if(!reopenedGraph.canSizeGroup(id) ||
               ElectricalData::loads(reopenedGraph,QSet<int>{id}).size()>1)continue;
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
                   ElectricalData::loads(reopenedGraph,QSet<int>{id}).size()<=1 &&
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
                        return; // Let selector.exec() unwind before submitting its parent.
                    }
                    if (d->windowTitle().contains(QStringLiteral("Dimensionamento preliminar"))) {
                        if (sawDialog) {
                            // QTimer callbacks do not recursively dispatch
                            // themselves while nested selector.exec() runs.
                            // Submit only AFTER the queued picker has closed.
                            if (sawPicker && !dialogSubmitted) {
                                dialogSubmitted=true;
                                const auto fields=d->findChildren<QDoubleSpinBox*>();
                                const auto combos=d->findChildren<QComboBox*>();
                                require(fields.at(3)->value()==0. && fields.at(8)->value()==0. &&
                                        fields.at(9)->value()==0. && fields.at(10)->value()==0.,
                                        "changing circuits clears power factor, protection and fault data");
                                require(combos.at(0)->currentIndex()==1 && combos.at(1)->currentIndex()==0,
                                        "changing circuits clears the previous supply and known-pair choice");
                                combos.at(0)->setCurrentIndex(2);combos.at(1)->setCurrentIndex(1);
                                fields.at(0)->setValue(24.);fields.at(2)->setValue(.24);
                                d->findChild<QDoubleSpinBox*>("qet_physical_length_m")->setValue(20.);
                                d->findChild<QCheckBox*>("qet_profile_confirmed")->setChecked(true);
                                const auto box=d->findChild<QDialogButtonBox*>();
                                require(box && box->button(QDialogButtonBox::Ok)->isEnabled(),
                                        "trace-picked V/I/P dialog remains valid");
                                d->grab().save(QDir(QString::fromLocal8Bit(argv[3])).filePath("dimensionamento.png"));
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
                        require(fields.size()==13,"load, physical length, correction and protection quantities shown");
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
                        d->findChild<QDoubleSpinBox*>("qet_physical_length_m")->setValue(20.);
                        d->findChild<QCheckBox*>("qet_profile_confirmed")->setChecked(true);
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
                        fields.at(8)->setValue(32.);fields.at(9)->setValue(46.4);
                        fields.at(10)->setValue(1000.);fields.at(11)->setValue(.1);
                        fields.at(12)->setValue(115.);
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
        Element *uiTerminal=nullptr;
        for(Diagram*d:reopened.diagrams())for(Element*e:d->elements())
            if(!uiTerminal && SymbolLegend::terminalLike(e))uiTerminal=e;
        require(uiTerminal,"real fixture contains a terminal for the documentation editor");
        for(Diagram*d:reopened.diagrams())d->clearSelection();
        uiTerminal->setSelected(true);const qreal terminalRotation=uiTerminal->rotation();
        const QString terminalLabel=uiTerminal->actualLabel();
        bool editedTerminal=false;
        QTimer editPoll;editPoll.setInterval(20);
        QObject::connect(&editPoll,&QTimer::timeout,&app,[&](){
            for(QWidget*w:QApplication::topLevelWidgets())if(auto*d=qobject_cast<QDialog*>(w)) {
                if(!d->isVisible())continue;
                auto*table=d->findChild<QTableWidget*>("qet_terminal_documentation_table");
                if(!table)continue;
                require(table->rowCount()==1 && table->columnCount()==7,"selected terminal documentary editor opens");
                table->item(0,1)->setText("X5");table->item(0,2)->setText("Retorno da boia");
                table->item(0,3)->setText("Sinal de nível");table->item(0,4)->setText("Borne documental do exemplo");
                table->item(0,5)->setText("Fabricante de demonstração");table->item(0,6)->setText("DEMO-X5");
                d->grab().save(QDir(QString::fromLocal8Bit(argv[3])).filePath("editor-bornes.png"));
                editedTerminal=true;d->accept();
            }
        });editPoll.start();QTimer::singleShot(15000,&editPoll,&QTimer::stop);
        SymbolLegend::setDocumentReferences(nullptr,&reopened);editPoll.stop();
        require(editedTerminal && SymbolLegend::documentedDesignator(uiTerminal)=="X5","native terminal table applied documentary ID");
        require(uiTerminal->rotation()==terminalRotation && uiTerminal->actualLabel()==terminalLabel,
                "terminal table keeps actual drawing rotation and label");
        reopened.undoStack()->undo();reopened.undoStack()->redo();
        const QUuid documentedTerminalUuid=uiTerminal->uuid();
        checkAutomaticLists(reopened,QString::fromLocal8Bit(argv[3]));
        const int originalFolios=reopened.diagrams().size();
        const int originalWireCount=WireTrace::scan(&reopened).wires;
        const int sheets=IndustrialDocumentation::createSheets(&reopened);
        require(sheets>0 && reopened.diagrams().size()==originalFolios+sheets,
                "native editable documentation sheets added");
        require(WireTrace::scan(&reopened).wires==originalWireCount,
                "documentation symbols do not contaminate electrical topology");
        require(SymbolLegend::inventory(&reopened).elementsScanned==topology.elements,
                "documentation symbol copies do not inflate BOM quantities");
        reopened.undoStack()->undo();
        require(reopened.diagrams().size()==originalFolios,"undo removes generated folios atomically");
        reopened.undoStack()->redo();
        require(reopened.diagrams().size()==originalFolios+sheets,"redo restores documentation");
        const QString demoPath=QDir(QString::fromLocal8Bit(argv[3])).filePath("QET-Industrial-Demonstracao.qet");
        reopened.setFilePath(demoPath);require(reopened.write().isOk(),"native documentation save");
        QETProject documented(demoPath);require(documented.state()==QETProject::Ok,"native documentation reopens");
        require(documented.diagrams().size()==originalFolios+sheets,"documentation survives reopen");
        bool terminalRestored=false;
        for(Diagram*d:documented.diagrams())if(!IndustrialDocumentation::generated(d))
            for(Element*e:d->elements())if(e->uuid()==documentedTerminalUuid) {
                terminalRestored=SymbolLegend::documentedDesignator(e)=="X5" &&
                    e->elementInformations().value("function").toString()=="Sinal de nível" &&
                    e->elementInformations().value("description").toString()=="Borne documental do exemplo";
            }
        require(terminalRestored,"terminal ID, function and textual description survive actual project save/reopen");
        int restoredVectorSymbols=0;
        for(Diagram*d:documented.diagrams())if(IndustrialDocumentation::generated(d)) {
            require(d->elements().isEmpty(),"symbol legend graphics are not electrical components");
            for(auto *item:d->items())if(auto*shape=qgraphicsitem_cast<QetShapeItem*>(item))
                if(shape->hasDocumentaryVector())++restoredVectorSymbols;
        }
        require(restoredVectorSymbols==inventory.symbols.size(),"all actual vector pictograms survive native shape XML");
        require(WireTrace::scan(&documented).wires==originalWireCount,"generated-folio marker survives XML");
        require(IndustrialDocumentation::writePdf(&documented,QDir(QString::fromLocal8Bit(argv[3]))
            .filePath("QET-Industrial-Documentacao.pdf")),"complete industrial PDF export");
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
