/* GPL-2.0-or-later. Explicit device electrical data, persisted in .qet. */
#ifndef QET_ELECTRICAL_DATA_H
#define QET_ELECTRICAL_DATA_H
#include "symbollegend.h"
#include "wiretrace.h"
#include <QTableWidget>
#include <QHeaderView>
#include <cmath>
namespace ElectricalData {
inline QStringList keys() {return {"qet_voltage_v","qet_current_a","qet_power_w","qet_supply",
    "qet_power_factor","qet_efficiency","qet_circuit_length_m","qet_temperature_factor",
    "qet_grouping_factor","qet_protection_a","qet_trip_i2_a","qet_fault_a","qet_clearing_s","qet_adiabatic_k"};}
inline bool load(Element*e) {
    if(!e)return false;
    if(e->elementInformations().contains("qet_current_a") || e->elementInformations().contains("qet_power_w"))return true;
    const QString n=e->name().toLower();
    return n.contains("motor") || n.contains("coil") || n.contains("bobina") ||
           n.contains("lamp") || n.contains("resist") || n.contains("sinalizador");
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
        "Alimentação: 1F, 3F ou CC. Fatores são declarados, conforme instalação real.");
    help->setWordWrap(true);layout->addWidget(help);
    const auto kk=keys();auto*table=new QTableWidget(elements.size(),kk.size()+1,&dialog);
    table->setHorizontalHeaderLabels({"Referência / página","Tensão V","Corrente A","Potência elétrica W","1F / 3F / CC",
        "cos φ","Rendimento η","Comprimento ida m","Fator temperatura","Fator agrupamento","Proteção In A",
        "I2 A","Curto A","Tempo s","k adiabático"});
    for(int row=0;row<elements.size();++row) {
        auto*e=elements[row];auto*ref=new QTableWidgetItem(SymbolLegend::documentedDesignator(e)+
            QStringLiteral(" / %1").arg(e->diagram()->folioIndex()+1));ref->setFlags(ref->flags() & ~Qt::ItemIsEditable);
        table->setItem(row,0,ref);
        for(int col=0;col<kk.size();++col)table->setItem(row,col+1,
            new QTableWidgetItem(e->elementInformations().value(kk[col]).toString()));
    }
    layout->addWidget(table);auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    while(dialog.exec()==QDialog::Accepted) {
        bool valid=true;
        for(int row=0;row<elements.size();++row)for(int col=0;col<kk.size();++col) {
            QString s=table->item(row,col+1)->text().trimmed();if(s.isEmpty())continue;
            if(col==3){if(s!="1F" && s!="3F" && s!="CC")valid=false;}
            else {bool ok=false;const double v=s.replace(',','.').toDouble(&ok);if(!ok || !std::isfinite(v) || v<=0.)valid=false;}
        }
        if(!valid){QMessageBox::warning(&dialog,"Dados inválidos","Use números positivos e alimentação 1F, 3F ou CC.");continue;}
        auto*batch=new QUndoCommand("Dados elétricos dos componentes");
        for(int row=0;row<elements.size();++row) {
            auto*e=elements[row];auto after=e->elementInformations();
            for(int col=0;col<kk.size();++col)after.addValue(kk[col],table->item(row,col+1)->text().trimmed(),false);
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
