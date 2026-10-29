/* QElectroTech — schematic legend and device schedules from real project symbols.
 * GPL-2.0-or-later. Export is read-only and never changes the diagrams.
 */
#ifndef QET_AUTOMATIC_SYMBOL_LEGEND_H
#define QET_AUTOMATIC_SYMBOL_LEGEND_H

#include "../qetproject.h"
#include "../diagram.h"
#include "../diagramcontext.h"
#include "../qetgraphicsitem/element.h"
#include <QPdfWriter>
#include <QPainter>
#include <QPageSize>
#include <QPageLayout>
#include <QStyleOptionGraphicsItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QHash>
#include <QRegularExpression>
#include <QFileInfo>
#include <QFontMetrics>
#include <QSet>
#include <QInputDialog>
#include <QLineEdit>
#include <QPointer>
#include <QUndoCommand>
#include <QPushButton>
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPicture>
#include <QSignalBlocker>
#include <cmath>
#include <algorithm>

namespace SymbolLegend {
struct Record {
    Element *symbol = nullptr; // Only used for drawing within this synchronous export.
    QString designator, abbreviation, kind, comment, function, description, manufacturer, partNumber;
    QString identity, model;
    QStringList references;
    double quantity = 1;
    QString declaredQuantity;
    bool quantityKnown = true;
};
struct Inventory {
    QList<Record> symbols;
    QList<Record> devices;
    QList<Record> materials;
    int elementsScanned=0;
    int foliosScanned=0;
};
inline QString infoText(const DiagramContext &info,const QString &key) {
    return info.value(key).toString().simplified();
}
// Custom project metadata: a documentation reference is deliberately
// NOT QET's on-screen "label". It survives project save/reopen and is
// independent of symbol orientation, mirror and visible text items.
inline QString documentationReferenceKey() {
    return QStringLiteral("qet_documentation_reference");
}
inline QString documentedDesignator(Element *element) {
    if (!element) return {};
    const QString documentary=element->elementInformations()
        .value(documentationReferenceKey()).toString().simplified();
    return documentary.isEmpty() ? element->actualLabel().simplified() : documentary;
}
inline QString firstValue(const DiagramContext &info,const QStringList &keys) {
    for(const QString &key:keys) {
        const QString s=infoText(info,key);
        if(!s.isEmpty())return s;
    }
    return {};
}
inline QString materialQuantity(const Record &record) {
    return record.quantityKnown?QString::number(record.quantity,'g',12):record.declaredQuantity;
}
inline QString abbreviation(const QString &label) {
    const auto match=QRegularExpression(QStringLiteral("^[^A-Za-zÀ-ÿ]*([A-Za-zÀ-ÿ]+)"))
        .match(label.trimmed());
    return match.hasMatch() ? match.captured(1).toUpper() :
           QStringLiteral("—");
}
inline Inventory inventory(QETProject *project) {
    Inventory inv;
    if(!project)return inv;
    QHash<QString,int> symbolByDefinition,materialByRef;
    // QET builds lazily loaded folios here, so objects/symbols on every page
    // participate in the output, not only the currently opened folio.
    for(Diagram *folio:project->diagrams()) {
        if(!folio || folio->border_and_titleblock.additionalFields()
                .value(QStringLiteral("qet_generated_documentation")).toBool())continue;
        ++inv.foliosScanned;
        for(Element *element:folio->elements()) {
            if(!element)continue;
            ++inv.elementsScanned;
            DiagramContext info=element->elementInformations();
            Record entry;
            entry.symbol=element;
            entry.designator=documentedDesignator(element);
            entry.abbreviation=(element->linkType() & Element::AllReport) ?
                QStringLiteral("—") : abbreviation(entry.designator);
            entry.kind=firstValue(info,{QStringLiteral("designation"),
                                        QStringLiteral("description")});
            if(entry.kind.isEmpty())entry.kind=element->name().simplified();
            if(entry.kind.isEmpty())entry.kind=QStringLiteral("Símbolo sem descrição");
            // Don't relabel a terminal's COMMENT as its electrical
            // FUNCTION. Preserve separate QET fields in the schedule.
            entry.comment=infoText(info,QStringLiteral("comment"));
            entry.function=infoText(info,QStringLiteral("function"));
            entry.description=firstValue(info,{QStringLiteral("description"),
                                               QStringLiteral("designation")});
            if(entry.description.isEmpty())entry.description=entry.kind;
            entry.manufacturer=infoText(info,QStringLiteral("manufacturer"));
            entry.partNumber=firstValue(info,{QStringLiteral("manufacturer_reference"),
                                              QStringLiteral("machine_manufacturer_reference")});
            entry.model=firstValue(info,{QStringLiteral("model"),QStringLiteral("qet_documentation_model")});
            entry.declaredQuantity=infoText(info,QStringLiteral("quantity"));
            if(!entry.declaredQuantity.isEmpty()) {
                QString number=entry.declaredQuantity;
                // Accept decimal comma, but leave unit-bearing expressions
                // untouched and separate instead of guessing their value/unit.
                if(QRegularExpression(QStringLiteral("^[0-9]+,[0-9]+$")).match(number).hasMatch())
                    number.replace(',', '.');
                entry.quantity=number.toDouble(&entry.quantityKnown);
                entry.quantityKnown=entry.quantityKnown && std::isfinite(entry.quantity) && entry.quantity>=0;
            }
            entry.references << (entry.designator.isEmpty()?
                QStringLiteral("Pág. %1 / %2").arg(folio->folioIndex()+1)
                    .arg(element->uuid().toString(QUuid::WithoutBraces)):entry.designator);
            entry.identity=element->location().path();
            if(entry.identity.isEmpty())
                entry.identity=element->uuid().toString();
            inv.devices << entry;
            // Each distinct native element definition gets its own REAL
            // pictogram. Repeated K1/K2/etc do not fill the legend with copies.
            auto sit=symbolByDefinition.constFind(entry.identity);
            if(sit==symbolByDefinition.cend()) {
                symbolByDefinition.insert(entry.identity,inv.symbols.size());
                inv.symbols << entry;
            }
            // BOM groups identical stocked parts, not merely equal tag letters.
            // Missing manufacturer/model data is NOT fabricated.
            bool linkedSlave=false;
            if(element->linkType() & Element::Slave)
                for(Element *linked:element->linkedElements())
                    if(linked && (linked->linkType() & Element::Master)) {
                        const auto master=linked->elementInformations();
                        const QString masterCode=firstValue(master,{"manufacturer_reference","machine_manufacturer_reference"});
                        // A separately specified auxiliary block is orderable
                        // hardware; a repeat view of its master is not.
                        linkedSlave=linkedSlave || entry.partNumber.isEmpty() ||
                            (entry.partNumber==masterCode && entry.manufacturer==infoText(master,"manufacturer") &&
                             entry.model==firstValue(master,{"model","qet_documentation_model"}));
                    }
            const QString excluded=infoText(info,"exclude_from_bom").toCaseFolded();
            if(!(element->linkType() & (Element::Simple|Element::Master|Element::Slave|Element::Terminale)) || linkedSlave ||
               QStringList{"true","1","yes","on"}.contains(excluded))continue;
            // The linked coil/master is the physical product. Folio arrows
            // are references, not purchasable material occurrences.
            // With missing manufacturer/product code equivalence is unknown.
            // A graphical definition is never a stock keeping identifier.
            // Length prefixes avoid collisions if a code/model contains '|'.
            auto keyed=[](const QString &value){return QString::number(value.size())+':'+value;};
            const QString materialKey=entry.partNumber.isEmpty() || entry.manufacturer.isEmpty() || !entry.quantityKnown ?
                QStringLiteral("instance:")+element->uuid().toString() :
                QStringLiteral("part:")+keyed(entry.manufacturer.toCaseFolded())+
                    keyed(entry.partNumber)+keyed(entry.model);
            auto mit=materialByRef.constFind(materialKey);
            if(mit==materialByRef.cend()) {
                materialByRef.insert(materialKey,inv.materials.size());
                inv.materials << entry;
            } else {
                inv.materials[mit.value()].quantity+=entry.quantity;
                inv.materials[mit.value()].references.append(entry.references);
            }
        }
    }
    std::sort(inv.symbols.begin(),inv.symbols.end(),[](const Record&a,const Record&b){
        const int c=QString::localeAwareCompare(a.abbreviation,b.abbreviation);
        return c==0 ? QString::localeAwareCompare(a.kind,b.kind)<0 : c<0;
    });
    std::sort(inv.devices.begin(),inv.devices.end(),[](const Record&a,const Record&b){
        return QString::localeAwareCompare(a.designator,b.designator)<0;
    });
    std::sort(inv.materials.begin(),inv.materials.end(),[](const Record&a,const Record&b){
        return QString::localeAwareCompare(a.kind,b.kind)<0;
    });
    return inv;
}
inline void rect(QPainter &p,const QRect &r) {
    p.setPen(QPen(Qt::black,1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}
inline void cellText(QPainter &p,const QRect &r,const QString &value,
                     int pt=7,bool bold=false,int flags=Qt::AlignVCenter|Qt::TextWordWrap) {
    p.save();
    QFont font(QStringLiteral("Arial"),pt);
    font.setBold(bold);
    if(!(flags & Qt::TextWordWrap)) {
        while(font.pointSize()>3 && QFontMetrics(font,p.device()).horizontalAdvance(value)>r.width()-10)
            font.setPointSize(font.pointSize()-1);
        if(QFontMetrics(font,p.device()).horizontalAdvance(value)>r.width()-10)
            flags |= Qt::TextWrapAnywhere;
    }
    p.setFont(font);
    p.setPen(Qt::black);
    p.drawText(r.adjusted(5,2,-5,-2),flags,
               value.isEmpty()?QStringLiteral("—"):value);
    p.restore();
}
inline QPicture symbolVector(Element *element) {
    QPicture vector;if(!element)return vector;
    const bool selected=element->isSelected();
    const QSignalBlocker sceneSignals(element->scene());
    element->setSelected(false);
    QPainter recorder(&vector);element->paint(&recorder,nullptr,nullptr);recorder.end();
    element->setSelected(selected);return vector;
}
inline void symbolImage(QPainter &p,Element *element,const QRect &box) {
    if(!element)return;
    QPicture vector=symbolVector(element);
    const QRectF bounds=vector.boundingRect();
    if(bounds.width()<=0.1 || bounds.height()<=0.1)return;
    const QRect target=box.adjusted(7,7,-7,-7);
    if(target.width()<3 || target.height()<3)return;
    const qreal scale=std::min(qreal(target.width())/bounds.width(),
                               qreal(target.height())/bounds.height());
    p.save();p.setClipRect(target);p.translate(target.center());
    p.scale(scale*qreal(vector.logicalDpiX())/p.device()->logicalDpiX(),
            scale*qreal(vector.logicalDpiY())/p.device()->logicalDpiY());
    p.translate(-bounds.center().x()*p.device()->logicalDpiX()/qreal(vector.logicalDpiX()),
                -bounds.center().y()*p.device()->logicalDpiY()/qreal(vector.logicalDpiY()));
    // QPicture playback rescales its coordinates to the output DPI.
    // Compensate both scale and origin against the actual vector bounds.
    vector.play(&p);p.restore();
}

inline void header(QPainter &p,const QRect &page,const QString &title,
                   const QString &projectTitle,int pageNo) {
    const QRect headline(page.left(),page.top(),page.width(),50);
    rect(p,headline);
    cellText(p,headline,title,12,true,Qt::AlignCenter);
    const QRect sub(page.left(),page.top()+50,page.width(),35);
    rect(p,sub);
    cellText(p,sub,QStringLiteral("PROJETO: %1  |  FOLHA GERADA: %2")
                       .arg(projectTitle.isEmpty()?QStringLiteral("sem título"):projectTitle)
                       .arg(pageNo),8,false,Qt::AlignVCenter);
}
inline bool renderSymbols(QPainter &p,QPdfWriter &pdf,const Inventory &inv,
                          const QRect &page,const QString &projectTitle,int &folio) {
    constexpr int columns=4,rows=7;
    const int perPage=columns*rows;
    for(int offset=0;offset<inv.symbols.size();offset+=perPage) {
        if(offset && !pdf.newPage())return false;
        header(p,page,QStringLiteral("SIMBOLOGIA"),projectTitle,++folio);
        const int y0=page.top()+85;
        const int usable=page.bottom()-y0-28;
        const int panelWidth=page.width()/columns;
        const int panelHeight=usable/rows;
        for(int n=0;n<perPage;++n) {
            const int col=n%columns,row=n/columns;
            QRect panel(page.left()+col*panelWidth,y0+row*panelHeight,
                        col==columns-1?page.width()-col*panelWidth:panelWidth,
                        panelHeight);
            rect(p,panel);
            const int cap=std::max(24,panelHeight/5);
            QRect names(panel.x(),panel.y(),panel.width(),cap);
            rect(p,names);
            int symbolW=std::max(60,panel.width()*27/100);
            int letterW=std::max(38,panel.width()*13/100);
            p.drawLine(names.x()+symbolW,names.top(),names.x()+symbolW,panel.bottom());
            p.drawLine(names.x()+symbolW+letterW,names.top(),
                       names.x()+symbolW+letterW,panel.bottom());
            cellText(p,QRect(names.x(),names.y(),symbolW,cap),
                QStringLiteral("SÍMBOLO"),6,true,Qt::AlignCenter);
            cellText(p,QRect(names.x()+symbolW,names.y(),letterW,cap),
                QStringLiteral("LET."),6,true,Qt::AlignCenter);
            cellText(p,QRect(names.x()+symbolW+letterW,names.y(),
                 names.width()-symbolW-letterW,cap),
                QStringLiteral("DESCRIÇÃO"),6,true,Qt::AlignCenter);
            const int at=offset+n;
            if(at>=inv.symbols.size())continue;
            const Record &item=inv.symbols.at(at);
            symbolImage(p,item.symbol,QRect(panel.x(),panel.y()+cap,
                                           symbolW,panel.height()-cap));
            cellText(p,QRect(panel.x()+symbolW,panel.y()+cap,
                             letterW,panel.height()-cap),
                     item.abbreviation,8,true,Qt::AlignCenter);
            cellText(p,QRect(panel.x()+symbolW+letterW,panel.y()+cap,
                             panel.width()-symbolW-letterW,panel.height()-cap),
                     item.kind,7);
        }
    }
    return true;
}
inline bool renderDeviceList(QPainter &p,QPdfWriter &pdf,
                             const Inventory &inv,const QRect &page,
                             const QString &projectTitle,int &folio) {
    // Mirrors the user's QET nomenclature: independent comment and
    // function columns, not an inferred replacement of one by the other.
    const int rows=22;
    const int percentages[]={12,20,23,32,13};
    for(int offset=0;offset<inv.devices.size();offset+=rows) {
        if(!pdf.newPage())return false;
        header(p,page,QStringLiteral("FUNÇÃO DOS DISPOSITIVOS"),projectTitle,++folio);
        int y=page.top()+85;
        const int rowH=std::min(74,(page.bottom()-y-30)/(rows+1));
        auto draw=[&](const QStringList &values,bool isHead) {
            int x=page.left();
            for(int k=0;k<5;++k) {
                const int width=k==4 ? page.right()-x+1 :
                    page.width()*percentages[k]/100;
                const QRect cell(x,y,width,rowH);
                rect(p,cell);
                cellText(p,cell,values.value(k),7,isHead);
                x+=width;
            }
            y+=rowH;
        };
        draw({QStringLiteral("LEGENDA"),QStringLiteral("COMENTÁRIO"),
              QStringLiteral("FUNÇÃO"),QStringLiteral("DESCRIÇÃO TEXTUAL"),
              QStringLiteral("FABRICANTE")},true);
        for(int j=offset;j<std::min(offset+rows,int(inv.devices.size()));++j) {
            const auto &item=inv.devices.at(j);
            draw({item.designator,item.comment,item.function,
                  item.description,item.manufacturer},false);
        }
    }
    return true;
}
inline bool renderMaterials(QPainter &p,QPdfWriter &pdf,const Inventory &inv,
                            const QRect &page,const QString &projectTitle,int &folio) {
    const int rows=20;
    for(int offset=0;offset<inv.materials.size();offset+=rows) {
        if(!pdf.newPage())return false;
        header(p,page,QStringLiteral("RELAÇÃO DE MATERIAIS"),projectTitle,++folio);
        int y=page.top()+85;
        const int rowH=std::min(80,(page.bottom()-y-30)/(rows+1));
        const int widths[]={int(page.width()*.20),int(page.width()*.07),
                            int(page.width()*.17),int(page.width()*.41)};
        auto draw=[&](const Record &item,bool headerRow) {
            int x=page.left();
            const QString vals[]={headerRow?QStringLiteral("REFERÊNCIA / COMPONENTE"):item.kind,
                                  headerRow?QStringLiteral("QTD."):materialQuantity(item),
                                  headerRow?QStringLiteral("CÓDIGO"):item.partNumber,
                                  headerRow?QStringLiteral("DESCRIÇÃO"):item.kind,
                                  headerRow?QStringLiteral("FABRICANTE"):item.manufacturer};
            for(int k=0;k<5;++k) {
                const int width=k<4?widths[k]:page.right()-x+1;
                const QRect cell(x,y,width,rowH);
                rect(p,cell);
                cellText(p,cell,vals[k],7,headerRow);
                x+=width;
            }
            y+=rowH;
        };
        draw(Record{},true);
        for(int j=offset;j<std::min(offset+rows,int(inv.materials.size()));++j)
            draw(inv.materials.at(j),false);
    }
    return true;
}
// Noninteractive writer is shared by the GUI action and native regression
// checks: a successful list alone does not verify real symbol drawings.
inline bool writePdf(QETProject *project,const QString &path) {
    if(!project || path.isEmpty()) return false;
    const Inventory inv=inventory(project);
    if(inv.symbols.isEmpty())return false;
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A3));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setPageMargins(QMarginsF(12,12,12,12),QPageLayout::Millimeter);
    pdf.setResolution(150);
    pdf.setTitle(QObject::tr("Simbologia, função dos dispositivos e relação de materiais"));
    QPainter painter(&pdf);
    if(!painter.isActive())return false;
    const QRect page(0,0,pdf.width(),pdf.height());
    int folio=0;
    const bool ok=renderSymbols(painter,pdf,inv,page,project->title(),folio) &&
                  renderDeviceList(painter,pdf,inv,page,project->title(),folio) &&
                  renderMaterials(painter,pdf,inv,page,project->title(),folio);
    painter.end();
    return ok;
}
// Undoable metadata-only edit. The visible QET label stays unchanged.
class SetDocumentReference : public QUndoCommand {
public:
    SetDocumentReference(Element *element,const QString &reference,
                         QUndoCommand *parent=nullptr)
        : QUndoCommand(parent),m_element(element),
          m_before(element->elementInformations()),m_after(m_before) {
        m_after.addValue(documentationReferenceKey(),reference,false);
        setText(QObject::tr("Referência documental do borne"));
    }
    // The stock terminal editor does not expose "function" or
    // "description", but DiagramContext persists these QET keys in XML.
    SetDocumentReference(Element *element,const QString &reference,
                         const QString &comment,const QString &function,
                         const QString &description,QUndoCommand *parent=nullptr)
        : QUndoCommand(parent),m_element(element),
          m_before(element->elementInformations()),m_after(m_before) {
        m_after.addValue(documentationReferenceKey(),reference,false);
        m_after.addValue(QStringLiteral("comment"),comment);
        m_after.addValue(QStringLiteral("function"),function);
        m_after.addValue(QStringLiteral("description"),description);
        setText(QObject::tr("Documentar borne sem mostrar texto no esquema"));
    }
    SetDocumentReference(Element *element,const QString &reference,
                         const QString &comment,const QString &function,
                         const QString &description,const QString &manufacturer,
                         const QString &partNumber,QUndoCommand *parent=nullptr)
        : SetDocumentReference(element,reference,comment,function,description,parent) {
        m_after.addValue(QStringLiteral("manufacturer"),manufacturer,false);
        m_after.addValue(QStringLiteral("manufacturer_reference"),partNumber,false);
    }
    void redo() override { if(m_element)m_element->setElementInformations(m_after); }
    void undo() override { if(m_element)m_element->setElementInformations(m_before); }
private:
    QPointer<Element> m_element;
    DiagramContext m_before,m_after;
};
inline bool terminalLike(Element *element) {
    if(!element) return false;
    if(element->linkType() & Element::Terminale)return true;
    const QString kind=(element->name()+QStringLiteral(" ")+
                         element->location().baseName()).toLower();
    return kind.contains(QStringLiteral("borne")) ||
           kind.contains(QStringLiteral("terminal")) ||
           kind.contains(QStringLiteral("clamp")) ||
           kind.contains(QStringLiteral("screw-terminal"));
}
inline void setDocumentReferences(QWidget *parent,QETProject *project) {
    if(!project || project->isReadOnly() || !project->undoStack()) {
        QMessageBox::warning(parent,QObject::tr("Documentar bornes"),
            QObject::tr("Abra um projeto editável."));
        return;
    }
    QList<Element *> terminals,selected;
    for(Diagram *folio:project->diagrams()) {
        if(!folio || folio->border_and_titleblock.additionalFields()
            .value(QStringLiteral("qet_generated_documentation")).toBool())continue;
        for(Element *e:folio->elements())if(e && terminalLike(e)) {
            terminals << e;
            if(e->isSelected())selected << e;
        }
    }
    if(!selected.isEmpty())terminals=selected;
    std::sort(terminals.begin(),terminals.end(),[](Element*a,Element*b){
        return documentedDesignator(a)<documentedDesignator(b);
    });
    if(terminals.isEmpty()) {
        QMessageBox::information(parent,QObject::tr("Documentar bornes"),
            QObject::tr("Nenhum borne encontrado no projeto."));
        return;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Documentação técnica dos bornes"));
    dlg.resize(1100,560);
    auto *layout=new QVBoxLayout(&dlg);
    auto *notice=new QLabel(QObject::tr(
        "Edite os bornes selecionados ou todos os bornes quando não houver seleção. "
        "A identificação documental funciona com a legenda oculta e o borne girado."),&dlg);
    notice->setWordWrap(true);layout->addWidget(notice);
    auto *table=new QTableWidget(terminals.size(),7,&dlg);
    table->setObjectName(QStringLiteral("qet_terminal_documentation_table"));
    table->setHorizontalHeaderLabels({QObject::tr("Página / símbolo"),QObject::tr("Identificação"),
        QObject::tr("Comentário"),QObject::tr("Função"),QObject::tr("Descrição textual"),
        QObject::tr("Fabricante"),QObject::tr("Referência comercial")});
    const QStringList keys={documentationReferenceKey(),QStringLiteral("comment"),
        QStringLiteral("function"),QStringLiteral("description"),QStringLiteral("manufacturer"),
        QStringLiteral("manufacturer_reference")};
    for(int row=0;row<terminals.size();++row) {
        Element *e=terminals.at(row);const auto info=e->elementInformations();
        auto *source=new QTableWidgetItem(QStringLiteral("%1 / %2")
            .arg(e->diagram()?e->diagram()->folioIndex()+1:0).arg(e->name()));
        source->setFlags(source->flags() & ~Qt::ItemIsEditable);table->setItem(row,0,source);
        for(int col=1;col<7;++col)
            table->setItem(row,col,new QTableWidgetItem(col==1?documentedDesignator(e):
                infoText(info,keys.at(col-1))));
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->setColumnWidth(0,150);table->setColumnWidth(4,240);layout->addWidget(table);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dlg);
    layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
    while(dlg.exec()==QDialog::Accepted) {
        bool valid=true;
        for(int row=0;row<terminals.size();++row) {
            const QString name=table->item(row,1)->text().trimmed();
            if(!name.isEmpty() && !QRegularExpression(QStringLiteral(
                "^[A-Za-z][A-Za-z0-9._:/-]{0,79}$")).match(name).hasMatch()) {
                valid=false;table->setCurrentCell(row,1);break;
            }
        }
        if(!valid) {
            QMessageBox::warning(&dlg,QObject::tr("Referência inválida"),
                QObject::tr("Use identificações como X4, X5 ou X1:2."));continue;
        }
        auto *batch=new QUndoCommand(QObject::tr("Documentação técnica dos bornes"));
        for(int row=0;row<terminals.size();++row) {
            Element *e=terminals.at(row);const auto original=e->elementInformations();
            QStringList values;bool changed=false;
            for(int col=1;col<7;++col) {
                QString value=table->item(row,col)->text().trimmed();
                // Preserve absence of a custom ID if the native label suffices.
                if(col==1 && infoText(original,documentationReferenceKey()).isEmpty() &&
                   value==e->actualLabel().simplified())value.clear();
                values << value;
                changed |= value!=infoText(original,keys.at(col-1));
            }
            if(changed)new SetDocumentReference(e,values[0],values[1],values[2],values[3],
                                                values[4],values[5],batch);
        }
        if(batch->childCount())project->undoStack()->push(batch);else delete batch;
        return;
    }
}

inline void exportPdf(QWidget *parent,QETProject *project) {
    if(!project) {
        QMessageBox::warning(parent,QObject::tr("Gerar simbologia"),
                             QObject::tr("Abra um projeto antes de gerar a legenda."));
        return;
    }
    const Inventory inv=inventory(project);
    if(inv.symbols.isEmpty()) {
        QMessageBox::information(parent,QObject::tr("Gerar simbologia"),
                                 QObject::tr("Nenhum elemento foi encontrado no projeto."));
        return;
    }
    const QString path=QFileDialog::getSaveFileName(parent,
        QObject::tr("Exportar simbologia e documentação técnica"),
        QStringLiteral("simbologia-e-documentacao.pdf"),
        QObject::tr("Documento PDF (*.pdf)"));
    if(path.isEmpty())return;
    if(!writePdf(project,path)) {
        QMessageBox::warning(parent,QObject::tr("Gerar simbologia"),
            QObject::tr("Houve falha ao criar o PDF com os símbolos do projeto."));
        return;
    }
    QMessageBox::information(parent,QObject::tr("Documentação gerada"),
        QObject::tr("Arquivo: %1\n\n%2 símbolo(s) distinto(s), "
                    "%3 ocorrência(s) e %4 itens de material, de %5 folhas.\n"
                    "Gerado como PDF independente; o desenho QET não foi alterado.")
        .arg(QFileInfo(path).fileName()).arg(inv.symbols.size())
        .arg(inv.devices.size()).arg(inv.materials.size()).arg(inv.foliosScanned));
}
}
#endif
