// GPL-2.0-or-later. Native circuit graph, GUI, persistence and undo integration.
#include "qetapptest.h"
#include "radial_test_fixture.h"
#include "automation/projectcircuitsizingui.h"
#include "automation/industrialdocumentation.h"
#include "qetresult.h"
#include "qetmessagebox.h"
#include <QTimer>
#include <QSaveFile>
using namespace ProjectCircuitSizing;
static Element *element(QETProject &p,const QString &tag) {
    for(Diagram *d:p.diagrams())for(Element *e:d->elements())if(SymbolLegend::documentedDesignator(e)==tag)return e;return nullptr;
}
static Conductor *conductor(QETProject &p,const QString &name) {
    for(Diagram *d:p.diagrams())for(Conductor *c:d->conductors())if(c->properties().text==name)return c;return nullptr;
}
static RadialSizing::WireResult resultWire(const Analysis &a,const QString &name,QETProject &p) {
    auto *c=conductor(p,name);for(const auto &w:a.result.wires)if(c && w.id==id(c->uuid()))return w;return {};
}
static QJsonObject plan(QETProject &p) {
    auto *source=element(p,"S0");QJsonObject rootIds;
    for(Terminal *t:source->terminals())rootIds[t->name()]=terminalId(t);
    const QString sid=id(source->uuid()),origin="origem:"+sid;
    QJsonObject cfg{{"supply","1F"},{"voltage",230},{"frequency_hz",60},{"drop",4},{"start_drop",15},{"source_r",.5},
        {"source_x",0},{"fault_max",500},{"cmin",.95},{"earthing","TN"},{"contacts_confirmed",true},{"roots",rootIds}};
    auto profile=preliminaryProfile();profile.confirmed=true;
    profile.reference="DADOS DIDÁTICOS — conferir instalação real; não é catálogo de fabricante";
    QJsonObject data{{"schema",1},{"source",sid},{"sources",QJsonObject{{sid,cfg}}},
        {"profiles",QJsonObject{{"default",profileJson(profile)}}}};
    auto sources=data["sources"].toObject();auto *dcSource=element(p,"S24");const QString dcId=id(dcSource->uuid());QJsonObject dcRoots;
    for(Terminal *t:dcSource->terminals())dcRoots[t->name()]=terminalId(t);
    sources[dcId]=QJsonObject{{"supply","CC"},{"voltage",24},{"drop",4},{"start_drop",15},{"source_r",.05},
        {"source_x",0},{"fault_max",500},{"cmin",.95},{"earthing","TN"},{"contacts_confirmed",true},{"roots",dcRoots}};data["sources"]=sources;
    QJsonObject wires;
    for(Diagram *d:p.diagrams())for(Conductor *c:d->conductors()) {
        const QString text=c->properties().text;
        const QString cable=text.startsWith("A-")?"A":text.startsWith("B-")?"B":text.startsWith("24")?"24":"F";
        wires[id(c->uuid())]=QJsonObject{{"cable",cable},{"length",text=="F-L-link"?1:5},{"kt",1},{"kg",1},{"loaded",2}};
    }data["wires"]=wires;
    QJsonObject protections;QJsonArray selectivity;
    for(const QString &tag:QStringList{"Q1","Q2"}) {
        const QString pid=id(element(p,tag)->uuid());
        protections[pid]=QJsonObject{{"in",0},{"curve","C"},{"breaking_a",6000},{"i2_multiple",1.45},{"max_seconds",.4}};
        selectivity<<QJsonObject{{"upstream",origin},{"downstream",pid},{"limit_a",500},
            {"reference","ASSOCIAÇÃO DIDÁTICA do teste — não é catálogo real"}};
    }
    protections[origin]=QJsonObject{{"in",0},{"curve","C"},{"breaking_a",6000},{"i2_multiple",1.45},{"max_seconds",.4}};
    protections["origem:"+dcId]=QJsonObject{{"in",2},{"breaking_a",6000},{"i2_multiple",1.45},{"max_seconds",.4},
        {"dc_rated",true},{"reference","ENVELOPE CC DIDÁTICO — não é catálogo"},
        {"points",QJsonArray{QJsonArray{1,1,2},QJsonArray{100,.02,.05},QJsonArray{1000,.001,.01}}}};
    data["protections"]=protections;data["selectivity"]=selectivity;return data;
}
class ProjectCircuitSizingTest:public QObject {
    Q_OBJECT
private slots:
    void nativeWorkflow();
    void threePhaseMotor();
};
void ProjectCircuitSizingTest::nativeWorkflow() {
    QETProject::setBackupEnabled(false);QET::QetMessageBox::setNonInteractive(true);
    QTemporaryDir folder;QVERIFY(folder.isValid());const QString fixture=folder.filePath("radial.qet");QVERIFY(RadialTestFixture::write(fixture));
    QETProject p(fixture);QCOMPARE(p.state(),QETProject::Ok);QCOMPARE(p.diagrams().size(),2);
    QVERIFY2(element(p,"S0"),"native embedded source loads");QVERIFY(element(p,"H1"));QVERIFY(element(p,"Q1"));
    auto *dc=conductor(p,"24+");QVERIFY(dc);const auto dcBefore=dc->properties();
    const auto trace=WireTrace::scan(&p);QCOMPARE(trace.reportHops.size(),3);QCOMPARE(trace.unmatchedReports,0);
    const auto data=plan(p);auto a=analyze(&p,data,true);
    QVERIFY2(a.result.ready,qPrintable(report(a)));QCOMPARE(a.result.loads.size(),2);QCOMPARE(a.omittedLoads,1);
    QCOMPARE(resultWire(a,"F-L-a",p).current,20.);QCOMPARE(resultWire(a,"F-L-b",p).current,20.);
    QCOMPARE(resultWire(a,"F-L-link",p).current,12.);QCOMPARE(resultWire(a,"A-L",p).current,8.);QCOMPARE(resultWire(a,"B-L",p).current,12.);
    QVERIFY(resultWire(a,"F-L-a",p).section>resultWire(a,"A-L",p).section);
    QHash<QString,ConductorProperties> before;QHash<QString,QPainterPath> geometry;
    for(Diagram *d:p.diagrams())for(Conductor *c:d->conductors()){before[id(c->uuid())]=c->properties();geometry[id(c->uuid())]=c->path();}
    QVERIFY(apply(&p,data,true));
    for(const auto &w:a.result.wires) {
        auto *c=a.wires[w.id].data();QVERIFY(c);QCOMPARE(c->properties().m_wire_section,QString::number(w.section,'g',12).replace('.',',')+" mm²");
        QCOMPARE(c->path(),geometry[w.id]);
        const auto record=QJsonDocument::fromJson(c->properties().m_sizing_data.toUtf8()).object();
        QCOMPARE(record["schema"].toInt(),2);QVERIFY(record["criteria_checked"].toBool());
    }
    QCOMPARE(dc->properties(),dcBefore);
    p.undoStack()->undo();for(Diagram *d:p.diagrams())for(Conductor *c:d->conductors())QCOMPARE(c->properties(),before[id(c->uuid())]);
    QVERIFY(settings(&p)["sources"].toObject().isEmpty());p.undoStack()->redo();
    const QString saved=folder.filePath("dimensionado.qet");p.setFilePath(saved);QVERIFY(p.write().isOk());
    QETProject reopened(saved);QCOMPARE(reopened.state(),QETProject::Ok);const auto stored=settings(&reopened);
    a=analyze(&reopened,stored,true);QVERIFY2(a.result.ready,qPrintable(report(a)));
    QCOMPARE(stored["audit"].toObject()[stored["source"].toString()].toString(),a.fingerprint);
    QCOMPARE(conductor(reopened,"A-L")->properties().m_sizing_data,conductor(p,"A-L")->properties().m_sizing_data);
    auto *h2=element(reopened,"H2");auto changed=h2->elementInformations();changed.addValue("qet_current_a","18",false);h2->setElementInformations(changed);
    const auto updated=analyze(&reopened,stored,true);QVERIFY2(updated.result.ready,qPrintable(report(updated)));
    QCOMPARE(resultWire(updated,"F-L-a",reopened).current,26.);QCOMPARE(resultWire(updated,"A-L",reopened).current,8.);
    QVERIFY(updated.fingerprint!=a.fingerprint);
    bool staleShown=false;for(const auto &t:IndustrialDocumentation::tables(&reopened))if(t.title=="RELATÓRIO DE DIMENSIONAMENTO")
        for(const auto &row:t.rows)if(row.join(' ').contains("RECALCULAR"))staleShown=true;
    QVERIFY(staleShown);
    auto bad=stored;auto wireInputs=bad["wires"].toObject();auto w=wireInputs[id(conductor(reopened,"B-L")->uuid())].toObject();w["length"]=0;wireInputs[id(conductor(reopened,"B-L")->uuid())]=w;bad["wires"]=wireInputs;
    const auto original=conductor(reopened,"F-L-a")->properties();QStringList issues;QVERIFY(!apply(&reopened,bad,true,&issues));QVERIFY(!issues.isEmpty());QCOMPARE(conductor(reopened,"F-L-a")->properties(),original);

    // The actual native dialog loads the saved data and invokes the same
    // public calculation/application actions a user clicks.
    const QString output=qEnvironmentVariable("QET_RADIAL_DELIVERY_DIR");
    if(!output.isEmpty())QVERIFY(QDir().mkpath(output));
    bool sawDialog=false,sawReady=false,sawCompletion=false,timedOut=false;int stage=0;
    QTimer poll;poll.setInterval(20);
    QObject::connect(&poll,&QTimer::timeout,&poll,[&](){
        for(QWidget *widget:QApplication::topLevelWidgets()) {
            if(!widget->isVisible())continue;
            if(auto *message=qobject_cast<QMessageBox*>(widget)) {
                if(message->windowTitle()=="Dimensionamento aplicado"){sawCompletion=true;message->accept();}continue;
            }
            auto *dialog=qobject_cast<QDialog*>(widget);if(!dialog || dialog->objectName()!="qet_radial_sizing_dialog")continue;
            sawDialog=true;
            if(stage==0) {
                stage=1;if(!output.isEmpty())dialog->grab().save(QDir(output).filePath("dimensionamento-origem.png"));
                auto *table=dialog->findChild<QTableWidget*>("qet_radial_loads");QVERIFY(table);QCOMPARE(table->rowCount(),2);
                auto *calc=dialog->findChild<QPushButton*>("qet_radial_calculate");QVERIFY(calc);calc->click();
                auto *applyButton=dialog->findChild<QPushButton*>("qet_radial_apply");QVERIFY(applyButton);sawReady=applyButton->isEnabled();
                QVERIFY(sawReady);if(!output.isEmpty())dialog->grab().save(QDir(output).filePath("dimensionamento-revisao.png"));
                stage=2;QTimer::singleShot(0,applyButton,[applyButton](){applyButton->click();});
            } else if(stage==2 && sawCompletion) {stage=3;dialog->accept();}
        }
    });
    QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,&watchdog,[&](){
        timedOut=true;for(QWidget *widget:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QDialog*>(widget))if(d->isVisible())d->reject();
    });
    poll.start();watchdog.start(60000);ProjectCircuitSizing::run(QetAppTest::editor(),&p);poll.stop();watchdog.stop();
    QVERIFY(!timedOut);QVERIFY(sawDialog && sawReady && sawCompletion);
    const auto finalAnalysis=analyze(&p,settings(&p),true);QVERIFY2(finalAnalysis.result.ready,qPrintable(report(finalAnalysis)));
    const auto all=analyzeAll(&p,settings(&p),true);QVERIFY2(all.ready,qPrintable(all.issues.join('\n')));QCOMPARE(all.domains.size(),2);
    const auto beforeAll=dc->properties();QVERIFY(applyAll(&p,settings(&p),true));
    QCOMPARE(QJsonDocument::fromJson(dc->properties().m_sizing_data.toUtf8()).object()["role"].toString(),QString("+"));
    p.undoStack()->undo();QCOMPARE(dc->properties(),beforeAll);p.undoStack()->redo();
    QCOMPARE(settings(&p)["results"].toObject().size(),2);
    auto automatic=settings(&p);automatic["auto_recalculate"]=true;QVERIFY(saveParameters(&p,automatic));
    auto *originalH2=element(p,"H2");auto h2Info=originalH2->elementInformations();h2Info.addValue("qet_current_a","18",false);originalH2->setElementInformations(h2Info);p.setModified(true);
    QVERIFY(p.write().isOk());
    auto record=QJsonDocument::fromJson(conductor(p,"F-L-a")->properties().m_sizing_data.toUtf8()).object();QCOMPARE(record["current"].toDouble(),26.);
    auto *manual=conductor(p,"A-L");auto manualProps=manual->properties();manualProps.m_wire_section="10 mm²";manual->setProperties(manualProps);p.setModified(true);
    QVERIFY(p.write().isOk());QCOMPARE(manual->properties().m_wire_section,QString("10 mm²"));
    QVERIFY(QJsonDocument::fromJson(manual->properties().m_sizing_data.toUtf8()).object()["manual_section"].toBool());
    h2Info.addValue("qet_current_a","19",false);originalH2->setElementInformations(h2Info);p.setModified(true);QVERIFY(p.write().isOk());QCOMPARE(manual->properties().m_wire_section,QString("10 mm²"));
    auto *originalH1=element(p,"H1");auto h1Info=originalH1->elementInformations();h1Info.addValue("qet_current_a","",false);originalH1->setElementInformations(h1Info);p.setModified(true);
    const auto previousSection=conductor(p,"F-L-a")->properties().m_wire_section;QVERIFY(p.write().isOk());
    QVERIFY(!settings(&p)["last_auto_issues"].toArray().isEmpty());QCOMPARE(conductor(p,"F-L-a")->properties().m_wire_section,previousSection);
    h1Info.addValue("qet_current_a","8",false);originalH1->setElementInformations(h1Info);p.setModified(true);QVERIFY(p.write().isOk());QVERIFY(settings(&p)["last_auto_issues"].toArray().isEmpty());
    const int unchangedIndex=p.undoStack()->index();QFile stableFile(p.filePath());QVERIFY(stableFile.open(QIODevice::ReadOnly));const auto stableBytes=stableFile.readAll();stableFile.close();
    QVERIFY(p.write().isOk());QCOMPARE(p.undoStack()->index(),unchangedIndex);QVERIFY(stableFile.open(QIODevice::ReadOnly));QCOMPARE(stableFile.readAll(),stableBytes);stableFile.close();
    // Restore the didactic 8 A / 12 A demonstration after exercising edits.
    h2Info.addValue("qet_current_a","12",false);originalH2->setElementInformations(h2Info);QVERIFY(applyAll(&p,settings(&p),true));
    if(!output.isEmpty()) {
        const auto fresh=analyzeAll(&p,settings(&p),true);QVERIFY2(fresh.ready,qPrintable(fresh.issues.join('\n')));QStringList reports;
        reports<<"Demonstração didática: duas origens, três cargas; critérios declarados verificados. Parâmetros não são catálogo real.";
        for(const auto &domain:fresh.domains)reports<<report(domain);
        QFile reportFile(QDir(output).filePath("Relatorio-Dimensionamento.txt"));QVERIFY(reportFile.open(QIODevice::WriteOnly));reportFile.write(reports.join("\n\n").toUtf8());reportFile.close();
        IndustrialDocumentation::createSheets(&p);p.setFilePath(QDir(output).filePath("QET-Dimensionamento-Demonstracao.qet"));QVERIFY(p.write().isOk());
        QVERIFY(IndustrialDocumentation::writePdf(&p,QDir(output).filePath("QET-Dimensionamento-Documentacao.pdf")));
    }
}
void ProjectCircuitSizingTest::threePhaseMotor() {
    QTemporaryDir folder;QVERIFY(folder.isValid());const QString path=folder.filePath("motor.qet");QVERIFY(RadialTestFixture::write(path,true));
    QETProject p(path);QCOMPARE(p.state(),QETProject::Ok);auto *source=element(p,"S0");auto *motor=element(p,"M3");auto *breaker=element(p,"Q3");
    QVERIFY(source && motor && breaker);QJsonObject roots;for(Terminal *t:source->terminals())roots[t->name()]=terminalId(t);
    const QString sid=id(source->uuid()),main="origem:"+sid,qid=id(breaker->uuid());
    auto profile=preliminaryProfile();profile.confirmed=true;profile.reference="Perfil didático 3F — não é catálogo real";
    QJsonObject cfg{{"supply","3F"},{"voltage",380},{"frequency_hz",60},{"drop",4},{"start_drop",15},{"source_r",.3},{"source_x",0},
        {"fault_max",1500},{"cmin",.95},{"earthing","TN"},{"contacts_confirmed",true},{"roots",roots}};
    QJsonObject data{{"schema",1},{"source",sid},{"sources",QJsonObject{{sid,cfg}}},{"profiles",QJsonObject{{"default",profileJson(profile)}}}};
    QJsonObject wires;
    for(Diagram *d:p.diagrams())for(Conductor *c:d->conductors()) {
        const auto name=c->properties().text;if(name.startsWith("24"))continue;const QString cable=name.section('-',0,0);
        wires[id(c->uuid())]=QJsonObject{{"cable",cable},{"length",cable=="F1" || cable=="F2"?1:5},{"kt",1},{"kg",1},{"loaded",3}};
    }data["wires"]=wires;
    data["protections"]=QJsonObject{{main,QJsonObject{{"in",40},{"curve","C"},{"breaking_a",6000},{"i2_multiple",1.45},{"max_seconds",.4}}},
        {qid,QJsonObject{{"in",10},{"breaking_a",6000},{"i2_multiple",1.3},{"max_seconds",.4},{"reference","Envelope de teste MPCB — não é catálogo real"},
            {"points",QJsonArray{QJsonArray{10,3600,7200},QJsonArray{20,20,60},QJsonArray{42,1.5,3},QJsonArray{100,.01,.05},QJsonArray{1000,.001,.01},QJsonArray{10000,.0001,.002}}}}}};
    data["selectivity"]=QJsonArray{QJsonObject{{"upstream",main},{"downstream",qid},{"limit_a",1500},{"reference","Associação didática para o teste"}}};
    const auto trace=WireTrace::scan(&p);QCOMPARE(trace.reportHops.size(),4);
    const auto a=analyze(&p,data,true);QVERIFY2(a.result.ready,qPrintable(report(a)));QCOMPARE(a.result.loads.size(),1);
    for(const QString &phase:QStringList{"L1","L2","L3"}) {
        QVERIFY(std::abs(resultWire(a,"F0-"+phase+"-a",p).current-7)<1e-8);
        QVERIFY(std::abs(resultWire(a,"F3-"+phase,p).current-7)<1e-8);
    }
    QVERIFY(a.result.loads[0].startDrop>a.result.loads[0].drop);QVERIFY(a.result.loads[0].startDrop<=15);
    QCOMPARE(a.result.protections[main].nominal,40.);QCOMPARE(a.result.protections[qid].nominal,10.);
    QVERIFY(resultWire(a,"F0-L1-a",p).section>resultWire(a,"F3-L1",p).section);
    const auto dc=conductor(p,"24+")->properties();QVERIFY(apply(&p,data,true));QCOMPARE(conductor(p,"24+")->properties(),dc);
    auto changed=motor->elementInformations();changed.addValue("qet_start_seconds","10",false);motor->setElementInformations(changed);
    QVERIFY(!analyze(&p,data,true).result.ready);
    changed.addValue("qet_start_seconds","1",false);motor->setElementInformations(changed);
    const QString output=qEnvironmentVariable("QET_RADIAL_DELIVERY_DIR");
    if(!output.isEmpty()) {
        QVERIFY(QDir().mkpath(output));QFile reportFile(QDir(output).filePath("Relatorio-Motor-Trifasico.txt"));QVERIFY(reportFile.open(QIODevice::WriteOnly));reportFile.write(report(a).toUtf8());reportFile.close();
        IndustrialDocumentation::createSheets(&p);p.setFilePath(QDir(output).filePath("QET-Motor-Trifasico-Demonstracao.qet"));QVERIFY(p.write().isOk());
        QVERIFY(IndustrialDocumentation::writePdf(&p,QDir(output).filePath("QET-Motor-Trifasico-Documentacao.pdf")));
    }
}
int main(int argc,char **argv){return QetAppTest::run<ProjectCircuitSizingTest>(argc,argv);}
#include "tst_projectcircuitsizing.moc"
