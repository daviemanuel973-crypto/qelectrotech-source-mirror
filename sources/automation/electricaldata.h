/* GPL-2.0-or-later. Explicit device electrical data, persisted in .qet. */
#ifndef QET_ELECTRICAL_DATA_H
#define QET_ELECTRICAL_DATA_H
#include "symbollegend.h"
#include "wiretrace.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <cmath>
namespace ElectricalData {
inline QStringList keys() {return {"qet_voltage_v","qet_current_a","qet_power_w","qet_supply",
    "qet_power_factor","qet_efficiency","qet_circuit_length_m","qet_temperature_factor",
    "qet_grouping_factor","qet_protection_a","qet_trip_i2_a","qet_fault_a","qet_clearing_s","qet_adiabatic_k",
    "qet_electrical_role"};}
inline bool load(Element*e) {
    if(!e)return false;
    const auto info=e->elementInformations();
    const QString declared=info.value("qet_electrical_role").toString().trimmed();
    if(!declared.isEmpty())return declared=="load";
    if(e->linkType() & (Element::AllReport | Element::Terminale))return false;
    const QString n=(e->name()+" "+e->location().baseName()).toLower();
    // A coil consumes power even when its name also contains "contactor".
    if(n.contains("coil") || n.contains("bobina"))return true;
    // Nominal current on a protector or supply is a rating, not a load.
    if(n.contains("breaker") || n.contains("disjuntor") || n.contains("contact") ||
       n.contains("contato") || n.contains("thermal relay") || n.contains("relé térmico") ||
       n.contains("fuse") || n.contains("fusível") || n.contains("fonte") ||
       n.contains("power supply") || n.contains("transform") || n.contains("converter") ||
       n.contains("switch") || n.contains("seccionador"))return false;
    return n.contains("motor") ||
           n.contains("lamp") || n.contains("resist") || n.contains("sinalizador") ||
           n.contains("carga") || n.contains("sensor") || n.contains("solenoid");
}
class SetData:public QUndoCommand {
    QPointer<Element> element;DiagramContext before,after;
public:
    SetData(Element*e,const DiagramContext&data,QUndoCommand*parent):QUndoCommand(parent),
        element(e),before(e->elementInformations()),after(data){}
    void redo()override {if(element)element->setElementInformations(after);}
    void undo()override {if(element)element->setElementInformations(before);}
};
inline void edit(QWidget*parent,QETProject*project) {
    if(!project || project->isReadOnly() || !project->undoStack())return;
    QList<Element*> elements,selected;
    for(Diagram*d:project->diagrams()) {
        if(!d || d->border_and_titleblock.additionalFields().value("qet_generated_documentation").toBool())continue;
        for(Element*e:d->elements()) {if(e->isSelected())selected<<e;if(load(e))elements<<e;}
    }
    if(!selected.isEmpty())elements=selected;
    if(elements.isEmpty()){QMessageBox::information(parent,"Dados elétricos","Selecione os componentes que deseja cadastrar.");return;}
    QDialog dialog(parent);dialog.setWindowTitle("Dados elétricos das cargas / componentes");dialog.resize(1100,480);
    auto*layout=new QVBoxLayout(&dialog);auto*help=new QLabel(
        "Informe somente dados conhecidos. Campos vazios permanecem pendentes. Potência elétrica ativa em W; "
        "para motores use corrente de placa ou converta potência mecânica com rendimento. "
        "Alimentação: 1F, 3F ou CC. Declare o papel elétrico: corrente nominal de uma proteção ou fonte "
        "não é corrente de carga. Fatores são declarados, conforme instalação real.");
    help->setWordWrap(true);layout->addWidget(help);
    const auto kk=keys();auto*table=new QTableWidget(elements.size(),kk.size()+1,&dialog);
    table->setHorizontalHeaderLabels({"Referência / página","Tensão V","Corrente A","Potência elétrica W","1F / 3F / CC",
        "cos φ","Rendimento η","Comprimento ida m","Fator temperatura","Fator agrupamento","Proteção In A",
        "I2 A","Curto A","Tempo s","k adiabático","Papel elétrico"});
    for(int row=0;row<elements.size();++row) {
        auto*e=elements[row];auto*ref=new QTableWidgetItem(SymbolLegend::documentedDesignator(e)+
            QStringLiteral(" / %1").arg(e->diagram()->folioIndex()+1));ref->setFlags(ref->flags() & ~Qt::ItemIsEditable);
        table->setItem(row,0,ref);
        for(int col=0;col<kk.size();++col) {
            if(kk[col]=="qet_electrical_role") {
                auto *role=new QComboBox(table);
                role->addItem("Não declarado (inferência)","");
                role->addItem("Carga","load");role->addItem("Fonte","source");
                role->addItem("Proteção","protection");role->addItem("Contato / manobra","switch");
                role->addItem("Conversor / transformador","converter");
                role->addItem("Borne","terminal");role->addItem("Não identificado","unknown");
                const QString saved=e->elementInformations().value(kk[col]).toString();
                const int index=role->findData(saved);
                if(index<0) {role->addItem(saved,saved);role->setCurrentIndex(role->count()-1);}
                else role->setCurrentIndex(index);
                table->setCellWidget(row,col+1,role);
            } else table->setItem(row,col+1,
                new QTableWidgetItem(e->elementInformations().value(kk[col]).toString()));
        }
    }
    layout->addWidget(table);auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    auto cellValue=[&](int row,int col) {
        if(auto *choice=qobject_cast<QComboBox*>(table->cellWidget(row,col+1)))
            return choice->currentData().toString();
        return table->item(row,col+1)->text().trimmed();
    };
    while(dialog.exec()==QDialog::Accepted) {
        bool valid=true;
        for(int row=0;row<elements.size();++row)for(int col=0;col<kk.size();++col) {
            QString s=cellValue(row,col);if(s.isEmpty() || kk[col]=="qet_electrical_role")continue;
            if(kk[col]=="qet_supply"){if(s!="1F" && s!="3F" && s!="CC")valid=false;}
            else {bool ok=false;const double v=s.replace(',','.').toDouble(&ok);if(!ok || !std::isfinite(v) || v<=0.)valid=false;}
        }
        if(!valid){QMessageBox::warning(&dialog,"Dados inválidos","Use números positivos e alimentação 1F, 3F ou CC.");continue;}
        auto*batch=new QUndoCommand("Dados elétricos dos componentes");
        for(int row=0;row<elements.size();++row) {
            auto*e=elements[row];auto after=e->elementInformations();
            for(int col=0;col<kk.size();++col)after.addValue(kk[col],cellValue(row,col),false);
            if(after!=e->elementInformations())new SetData(e,after,batch);
        }
        if(batch->childCount())project->undoStack()->push(batch);else delete batch;return;
    }
}
// Contact traversal never joins potentials. Loads terminate the search;
// multiple discovered loads mean manual review rather than summing currents.
inline QSet<Element*> loads(const WireTrace::Snapshot&trace,const QSet<int>&groups) {
    QSet<Element*> result;QSet<int> visited;QQueue<int> pending;
    for(int id:groups)if(id>=0 && id<trace.groups.size()){pending.enqueue(id);visited.insert(id);}
    while(!pending.isEmpty()) {
        const int id=pending.dequeue();
        for(Element*e:trace.groups.at(id).adjacentElements)if(load(e))result.insert(e);
        for(const auto&arc:trace.passages) {
            const int next=arc.a==id?arc.b:arc.b==id?arc.a:-1;
            if(next>=0 && arc.traversable && !visited.contains(next)) {visited.insert(next);pending.enqueue(next);}
        }
    }
    return result;
}
}
#endif
