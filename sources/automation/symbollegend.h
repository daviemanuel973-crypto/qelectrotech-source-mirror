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
#include <algorithm>

namespace SymbolLegend {
struct Record {
    Element *symbol = nullptr; // Only used for drawing within this synchronous export.
    QString designator, abbreviation, kind, function, manufacturer, partNumber;
    QString identity;
    int quantity = 1;
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
inline QString firstValue(const DiagramContext &info,const QStringList &keys) {
    for(const QString &key:keys) {
        const QString s=infoText(info,key);
        if(!s.isEmpty())return s;
    }
    return {};
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
        if(!folio)continue;
        ++inv.foliosScanned;
        for(Element *element:folio->elements()) {
            if(!element)continue;
            ++inv.elementsScanned;
            DiagramContext info=element->elementInformations();
            Record entry;
            entry.symbol=element;
            entry.designator=element->actualLabel().simplified();
            entry.abbreviation=abbreviation(entry.designator);
            entry.kind=firstValue(info,{QStringLiteral("designation"),
                                        QStringLiteral("description")});
            if(entry.kind.isEmpty())entry.kind=element->name().simplified();
            if(entry.kind.isEmpty())entry.kind=QStringLiteral("Símbolo sem descrição");
            entry.function=firstValue(info,{QStringLiteral("function"),
                                            QStringLiteral("description"),
                                            QStringLiteral("designation")});
            if(entry.function.isEmpty())entry.function=entry.kind;
            entry.manufacturer=infoText(info,QStringLiteral("manufacturer"));
            entry.partNumber=firstValue(info,{QStringLiteral("manufacturer_reference"),
                                              QStringLiteral("machine_manufacturer_reference")});
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
            const QString materialKey=entry.partNumber.isEmpty() ?
                QStringLiteral("definition:")+entry.identity :
                entry.manufacturer.toLower()+QStringLiteral("|")+
                entry.partNumber.toLower();
            auto mit=materialByRef.constFind(materialKey);
            if(mit==materialByRef.cend()) {
                materialByRef.insert(materialKey,inv.materials.size());
                inv.materials << entry;
            } else {
                ++inv.materials[mit.value()].quantity;
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
    p.setFont(font);
    p.setPen(Qt::black);
    p.drawText(r.adjusted(5,2,-5,-2),flags,
               value.isEmpty()?QStringLiteral("—"):value);
    p.restore();
}
inline void symbolImage(QPainter &p,Element *element,const QRect &box) {
    if(!element) return;
    QRectF bounds=element->boundingRect();
    if(bounds.width()<=0.1 || bounds.height()<=0.1)return;
    const QRect target=box.adjusted(7,7,-7,-7);
    if(target.width()<3 || target.height()<3)return;
    const qreal scale=std::min(qreal(target.width())/bounds.width(),
                               qreal(target.height())/bounds.height());
    p.save();
    p.setClipRect(target);
    p.translate(target.center());
    p.scale(scale,scale);
    p.translate(-bounds.center());
    QStyleOptionGraphicsItem option;
    option.state=QStyle::State_None;
    // Render the same vector QPicture that the QET editor draws. We do not
    // paste a screenshot, copy a page or replace the symbol with an icon.
    element->paint(&p,&option,nullptr);
    p.restore();
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
    const int rows=22;
    const int heights[3]={22,60,18};
    const int w1=page.width()*18/100,w2=page.width()*65/100,w3=page.width()-w1-w2;
    for(int offset=0;offset<inv.devices.size();offset+=rows) {
        if(!pdf.newPage())return false;
        header(p,page,QStringLiteral("FUNÇÃO DOS DISPOSITIVOS"),projectTitle,++folio);
        int y=page.top()+85;
        const int rowH=std::min(74,(page.bottom()-y-30)/(rows+1));
        auto draw=[&](const QString &a,const QString &b,const QString &c,bool isHead) {
            QRect c1(page.left(),y,w1,rowH);
            QRect c2(page.left()+w1,y,w2,rowH);
            QRect c3(page.left()+w1+w2,y,w3,rowH);
            rect(p,c1);rect(p,c2);rect(p,c3);
            cellText(p,c1,a,7,isHead);
            cellText(p,c2,b,7,isHead);
            cellText(p,c3,c,7,isHead);
            y+=rowH;
        };
        draw(QStringLiteral("REFERÊNCIA"),QStringLiteral("DESCRIÇÃO / FUNÇÃO"),
             QStringLiteral("FABRICANTE"),true);
        for(int j=offset;j<std::min(offset+rows,inv.devices.size());++j) {
            const auto &item=inv.devices.at(j);
            draw(item.designator,item.function,item.manufacturer,false);
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
                                  headerRow?QStringLiteral("QTD."):QString::number(item.quantity),
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
        for(int j=offset;j<std::min(offset+rows,inv.materials.size());++j)
            draw(inv.materials.at(j),false);
    }
    return true;
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
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A3));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setPageMargins(QMarginsF(12,12,12,12),QPageLayout::Millimeter);
    pdf.setResolution(150);
    pdf.setTitle(QObject::tr("Simbologia, função dos dispositivos e relação de materiais"));
    QPainter painter(&pdf);
    if(!painter.isActive()) {
        QMessageBox::warning(parent,QObject::tr("Gerar simbologia"),
                             QObject::tr("Não foi possível criar o arquivo PDF."));
        return;
    }
    const QRect page(0,0,pdf.width(),pdf.height());
    int folio=0;
    const bool ok=renderSymbols(painter,pdf,inv,page,project->title(),folio) &&
                  renderDeviceList(painter,pdf,inv,page,project->title(),folio) &&
                  renderMaterials(painter,pdf,inv,page,project->title(),folio);
    painter.end();
    if(!ok) {
        QMessageBox::warning(parent,QObject::tr("Gerar simbologia"),
            QObject::tr("Houve falha ao escrever uma ou mais páginas do PDF."));
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
