/* GPL-2.0-or-later. Native editable industrial documentation. */
#ifndef QET_INDUSTRIAL_DOCUMENTATION_H
#define QET_INDUSTRIAL_DOCUMENTATION_H
#include "symbollegend.h"
#include "wiretrace.h"
#include "../qetgraphicsitem/independenttextitem.h"
#include "../qetgraphicsitem/qetshapeitem.h"
#include "../factory/elementfactory.h"
#include "../undocommand/adddiagramcommand.h"
#include "../undocommand/removediagramcommand.h"
#include <QSaveFile>
#include <QTextStream>
#include <QTextDocument>
#include <QTextBoundaryFinder>
#include <QStringConverter>
#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include <cmath>

namespace IndustrialDocumentation {
enum class Section { All, Materials, Symbols, Lists };
struct Table { QString title; QStringList headings; QList<QStringList> rows; QList<int> weights; };
inline bool generated(Diagram *d) {
    return d && d->border_and_titleblock.additionalFields()
        .value(QStringLiteral("qet_generated_documentation")).toBool();
}
inline Table materialTable(const SymbolLegend::Inventory &inv) {
    Table materials{QStringLiteral("RELAÇÃO DE MATERIAIS"),
        {"REFERÊNCIAS","QTD.","CÓDIGO","DESCRIÇÃO","FABRICANTE","MODELO","PENDÊNCIAS"},
        {},{16,5,13,24,13,12,17}};
    for(const auto &r:inv.materials) {
        QStringList pending;
        if(r.manufacturer.isEmpty())pending << QStringLiteral("fabricante");
        if(r.partNumber.isEmpty())pending << QStringLiteral("código comercial");
        if(!r.quantityKnown)pending << QStringLiteral("quantidade numérica / unidade");
        materials.rows << QStringList{r.references.join(", "),SymbolLegend::materialQuantity(r),
            r.partNumber,r.description,r.manufacturer,r.model,
            pending.isEmpty()?QString():QStringLiteral("Preencher: ")+pending.join(", ")};
    }
    return materials;
}
inline QList<Table> tables(QETProject *project) {
    const auto inv=SymbolLegend::inventory(project);
    const auto trace=WireTrace::scan(project);
    const auto materials=materialTable(inv);
    Table devices{QStringLiteral("FUNÇÃO DOS DISPOSITIVOS"),
        {"REFERÊNCIA","COMENTÁRIO","FUNÇÃO","DESCRIÇÃO TEXTUAL","FABRICANTE"},{},{12,20,22,31,15}};
    Table terminals{QStringLiteral("LISTA DE BORNES"),
        {"IDENTIFICAÇÃO","PÁGINA","COMENTÁRIO","FUNÇÃO","DESCRIÇÃO","FABRICANTE / REF."},{},{12,6,20,20,27,15}};
    for(const auto &r:inv.devices) {
        devices.rows << QStringList{r.designator,r.comment,r.function,r.description,r.manufacturer};
        if(SymbolLegend::terminalLike(r.symbol))terminals.rows << QStringList{
            r.designator,QString::number(r.symbol->diagram()->folioIndex()+1),r.comment,r.function,
            r.description,r.manufacturer+" / "+r.partNumber};
    }
    Table wires{QStringLiteral("LISTA DE CONDUTORES / ORIGEM E DESTINO"),
        {"FIO / PÁG.","ORIGEM / TERMINAL","DESTINO / TERMINAL","SEÇÃO","FUNÇÃO / COR","TENSÃO"},{},{12,27,27,10,14,10}};
    Table circuits{QStringLiteral("TABELA DE POTENCIAIS E CIRCUITOS"),
        {"POTENCIAL","PÁGINAS","FIOS","DADOS DECLARADOS","SITUAÇÃO"},{},{9,10,9,47,25}};
    Table sizing{QStringLiteral("RELATÓRIO DE DIMENSIONAMENTO"),
        {"FIO / PÁG.","SEÇÃO CADASTRADA","TENSÃO CADASTRADA","VERIFICAÇÃO / PENDÊNCIA"},{},{18,16,18,48}};
    Table missing{QStringLiteral("PROPRIEDADES NÃO PREENCHIDAS / PENDÊNCIAS"),
        {"REFERÊNCIA / PÁG.","PROPRIEDADES PENDENTES","SITUAÇÃO"},{},{22,53,25}};
    QMap<QString,int> sections;
    for(int id=0;id<trace.groups.size();++id) {
        const auto &g=trace.groups.at(id);QStringList folios;
        for(Diagram *d:g.folios)folios << QString::number(d->folioIndex()+1);
        folios.sort();
        circuits.rows << QStringList{QString::number(id+1),folios.join(", "),
            QString::number(g.conductors.size()),g.declaredData.join("\n"),
            trace.canSizeGroup(id)?QStringLiteral("Topologia indexada; carga/estado a confirmar"):
                                   QStringLiteral("PENDENTE: conexão inconsistente")};
        for(Conductor *c:g.conductors) {
            const auto p=c->properties();
            const QString ref=(p.text.isEmpty()?WireTrace::shortUuid(c->uuid()):p.text)+
                QStringLiteral(" / %1").arg(c->diagram()?c->diagram()->folioIndex()+1:0);
            wires.rows << QStringList{ref,WireTrace::terminalDescription(c->terminal1),
                WireTrace::terminalDescription(c->terminal2),p.m_wire_section,p.m_function+" / "+p.m_wire_color,
                p.m_tension_protocol};
            ++sections[p.m_wire_section.isEmpty()?QStringLiteral("PENDENTE"):p.m_wire_section];
            const auto audit=QJsonDocument::fromJson(p.m_sizing_data.toUtf8()).object();
            QString assessment=QStringLiteral("PENDENTE: dados da carga, comprimento e instalação");
            if(!p.m_wire_section.isEmpty())assessment=QStringLiteral("Seção cadastrada sem registro de cálculo; revisão pendente.");
            if(audit.value("schema").toInt()==1) {
                if(audit.value("section").toString()!=p.m_wire_section)
                    assessment=QStringLiteral("REGISTRO DESATUALIZADO: a seção foi alterada depois do cálculo.");
                else assessment=QStringLiteral("PRÉ-DIMENSIONAMENTO: Ib=%1 A; Iz corrigida=%2 A; L=%3 m; queda=%4%. Proteção: %5. Curto: %6. Revisar após alterar carga/instalação. Não é conformidade integral.")
                    .arg(audit.value("current").toDouble()).arg(audit.value("iz_a").toDouble())
                    .arg(audit.value("length_m").toDouble()).arg(audit.value("drop_percent").toDouble())
                    .arg(audit.value("protection_checked").toBool()?"critérios Ib/In/Iz e I2 verificados":"PENDENTE")
                    .arg(audit.value("short_circuit_checked").toBool()?"adiabático verificado":"PENDENTE");
            }
            sizing.rows << QStringList{ref,p.m_wire_section,p.m_tension_protocol,assessment};
        }
    }
    for(const auto &r:inv.devices) {
        QStringList absent;if(r.designator.isEmpty())absent<<"identificação";
        if(r.manufacturer.isEmpty())absent<<"fabricante";if(r.partNumber.isEmpty())absent<<"referência comercial";
        if(r.function.isEmpty())absent<<"função";
        if(!absent.isEmpty())missing.rows << QStringList{r.designator+QStringLiteral(" / %1")
            .arg(r.symbol->diagram()->folioIndex()+1),absent.join(", "),"PENDENTE"};
    }
    Table sizes{QStringLiteral("TABELA DE BITOLAS"),{"SEÇÃO CADASTRADA","TRECHOS DE FIO"},{},{60,40}};
    for(auto it=sections.cbegin();it!=sections.cend();++it)sizes.rows<<QStringList{it.key(),QString::number(it.value())};
    Table links{QStringLiteral("REFERÊNCIAS CRUZADAS ENTRE FOLHAS"),
        {"ORIGEM","PÁGINA","DESTINO","PÁGINA","VÍNCULO"},{},{25,8,25,8,34}};
    for(const auto &h:trace.reportHops)links.rows<<QStringList{h.source,h.fromPage,h.destination,h.toPage,"Vínculo nativo QET"};
    Table issues{QStringLiteral("RELATÓRIO DE INCONSISTÊNCIAS"),{"OCORRÊNCIA","SITUAÇÃO"},{},{80,20}};
    for(const QString &warning:trace.warnings)issues.rows<<QStringList{warning,"REVISAR"};
    if(issues.rows.isEmpty())issues.rows<<QStringList{"Sem inconsistências topológicas detectadas; dados elétricos e estados ainda exigem revisão.","CONDICIONADO"};
    return {materials,devices,terminals,wires,sizes,circuits,links,sizing,missing,issues};
}
inline QList<int> widths(const Table&t,int total) {
    QList<int> out;int used=0,sum=0;for(int w:t.weights)sum+=w;
    for(int i=0;i<t.headings.size();++i) {
        int w=i==t.headings.size()-1?total-used:total*t.weights.value(i,1)/std::max(1,sum);
        out<<w;used+=w;
    }return out;
}
inline int rowHeight(QPainter &p,const QStringList&values,const QList<int>&ws,int minimum=38) {
    int h=minimum;
    for(int i=0;i<ws.size();++i)h=std::max(h,p.fontMetrics().boundingRect(
        QRect(0,0,std::max(10,ws[i]-10),10000),Qt::TextWordWrap,values.value(i)).height()+12);
    return h;
}
inline bool writePdf(QETProject *project,const QString&path) {
    if(!project || path.isEmpty())return false;
    QSaveFile output(path);if(!output.open(QIODevice::WriteOnly))return false;
    bool good=true;
    {
        QPdfWriter pdf(&output);pdf.setResolution(150);pdf.setPageSize(QPageSize(QPageSize::A3));
        pdf.setPageOrientation(QPageLayout::Landscape);
        pdf.setPageMargins(QMarginsF(12,12,12,12),QPageLayout::Millimeter);
        pdf.setTitle(QStringLiteral("Documentação elétrica industrial"));QPainter p(&pdf);
        if(!p.isActive())return false;
        QRect page(0,0,pdf.width(),pdf.height());int folio=0;bool started=false;
        const auto inv=SymbolLegend::inventory(project);
        if(!inv.symbols.isEmpty()) {good=SymbolLegend::renderSymbols(p,pdf,inv,page,project->title(),folio);started=true;}
        for(const auto &t:tables(project)) {
            const auto ws=widths(t,page.width());int y=0;
            auto row=[&](const QStringList&v,bool bold,int h) {
                int x=0;
                for(int i=0;i<ws.size();++i) {
                    const QRect cell(x,y,ws[i],h);SymbolLegend::rect(p,cell);
                    SymbolLegend::cellText(p,cell,v.value(i),7,bold);x+=ws[i];
                }y+=h;
            };
            auto start=[&]() {
                if(started && !pdf.newPage())good=false;started=true;
                SymbolLegend::header(p,page,t.title,project->title(),++folio);y=85;
                row(t.headings,true,40);p.setFont(QFont("Arial",7));
            };start();
            const auto rows=t.rows.isEmpty()?QList<QStringList>{{QStringLiteral("Nenhum registro aplicável.")}}:t.rows;
            for(const auto &v:rows) {
                const int h=rowHeight(p,v,ws);
                if(y+h>page.height()-20)start();
                // Very long user text continues on extra rows; don't silently clip.
                QStringList remaining=v;
                while(remaining.size()<ws.size())remaining<<QString();
                while(rowHeight(p,remaining,ws)>page.height()-125) {
                    QStringList chunk;
                    for(int col=0;col<ws.size();++col) {
                        QString text=remaining.value(col);int low=0,high=text.size();
                        while(low<high) {
                            const int mid=(low+high+1)/2;
                            const int height=p.fontMetrics().boundingRect(QRect(0,0,std::max(10,ws[col]-10),10000),
                                Qt::TextWordWrap,text.left(mid)).height()+12;
                            if(height<=page.height()-145)low=mid;else high=mid-1;
                        }
                        const int cut=std::max(1,low);
                        chunk<<text.left(cut);remaining[col]=text.mid(cut);
                    }
                    int ch=rowHeight(p,chunk,ws);row(chunk,false,ch);start();
                }
                row(remaining,false,rowHeight(p,remaining,ws));
            }
        }
        p.end();
    }
    return good && output.commit();
}
inline void exportPdf(QWidget *parent,QETProject *project) {
    if(!project)return;
    const QString path=QFileDialog::getSaveFileName(parent,QObject::tr("Documentação industrial"),
        "documentacao-industrial.pdf","PDF (*.pdf)");if(path.isEmpty())return;
    if(!writePdf(project,path))QMessageBox::warning(parent,"Documentação","Falha ao salvar o PDF.");
    else QMessageBox::information(parent,"Documentação","Documentação gerada com dados reais e pendências explícitas.");
}
inline void exportCsv(QWidget *parent,QETProject *project) {
    if(!project)return;
    const QString dir=QFileDialog::getExistingDirectory(parent,"Exportar tabelas CSV");if(dir.isEmpty())return;
    int index=0;bool ok=true;
    for(const auto&t:tables(project)) {
        QSaveFile f(dir+QStringLiteral("/qet-tabela-%1.csv").arg(++index,2,10,QLatin1Char('0')));
        if(!f.open(QIODevice::WriteOnly)){ok=false;continue;}QTextStream stream(&f);
        stream.setEncoding(QStringConverter::Utf8);stream<<QChar(0xfeff);
        auto row=[&](const QStringList&values) {
            QStringList escaped;for(QString v:values){v.replace('"',"\"\"");escaped<<'"'+v+'"';}
            stream<<escaped.join(';')<<"\r\n";
        };row(t.headings);for(const auto&r:t.rows)row(r);stream.flush();ok &= f.commit();
    }
    if(!ok)QMessageBox::warning(parent,"Exportar tabelas","Algumas tabelas não puderam ser salvas.");
}
inline IndependentTextItem *text(Diagram*d,const QString&s,const QRectF&r,bool bold=false) {
    auto *item=new IndependentTextItem(s);QFont f("Arial",8);f.setBold(bold);item->setFont(f);
    item->setPos(r.topLeft()+QPointF(3,2));item->setTextWidth(r.width()-6);d->addItem(item);return item;
}
inline void border(Diagram*d,const QRectF&r) {
    auto *shape=new QetShapeItem(r.topLeft(),r.bottomRight(),QetShapeItem::Rectangle);
    shape->setPen(QPen(Qt::black,.6));d->addItem(shape);
}
inline int nativeRowHeight(const QStringList &values,const QList<int> &ws) {
    int height=35;
    for(int i=0;i<ws.size();++i) {
        QTextDocument doc;doc.setDefaultFont(QFont("Arial",8));
        doc.setPlainText(values.value(i));doc.setTextWidth(std::max(1,ws[i]-6));
        height=std::max(height,int(std::ceil(doc.size().height()))+6);
    }
    return height;
}
// Split oversized user text across repeated-header pages without dropping
// characters or cutting a Unicode grapheme (including surrogate pairs).
inline QList<QStringList> splitNativeRow(QStringList remaining,const QList<int> &ws,
                                       int maximumHeight=615) {
    while(remaining.size()<ws.size())remaining<<QString();
    QList<QStringList> result;
    while(nativeRowHeight(remaining,ws)>maximumHeight) {
        QStringList chunk;
        for(int col=0;col<ws.size();++col) {
            const QString value=remaining[col];
            QList<int> boundaries{0};
            QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme,value);
            for(int end=finder.toNextBoundary();end>=0;end=finder.toNextBoundary())
                boundaries<<end;
            int low=0,high=boundaries.size()-1;
            while(low<high) {
                const int mid=(low+high+1)/2;
                if(nativeRowHeight({value.left(boundaries[mid])},{ws[col]})<=maximumHeight)
                    low=mid;
                else high=mid-1;
            }
            const int cut=boundaries[low];
            chunk<<value.left(cut);remaining[col]=value.mid(cut);
        }
        // Valid page/column dimensions always fit at least one grapheme.
        // Refuse invalid dimensions rather than looping or dropping text.
        if(std::all_of(chunk.cbegin(),chunk.cend(),[](const QString &s){return s.isEmpty();}))return {};
        result<<chunk;
    }
    result<<remaining;return result;
}
inline QString sheetKind(Diagram *d) {
    if(!generated(d))return {};
    const auto props=d->border_and_titleblock.exportTitleBlock();
    const QString kind=props.context.value("qet_documentation_kind").toString();
    if(!kind.isEmpty())return kind;
    // Migrate only pages already marked by our older generator. A manual
    // sheet with an identical title must never be mistaken for generated data.
    if(props.title==QStringLiteral("SIMBOLOGIA"))return QStringLiteral("symbols");
    if(props.title==QStringLiteral("RELAÇÃO DE MATERIAIS"))return QStringLiteral("materials");
    return QStringLiteral("other");
}
inline int createSheets(QETProject *project,Section section=Section::All) {
    if(!project || project->isReadOnly() || !project->undoStack())return 0;
    const QString selectedKind=section==Section::Materials?QStringLiteral("materials"):
        section==Section::Symbols?QStringLiteral("symbols"):QString();
    const QString command=section==Section::Materials?QObject::tr("Gerar/atualizar lista de materiais"):
        section==Section::Symbols?QObject::tr("Gerar/atualizar simbologia"):
        section==Section::Lists?QObject::tr("Atualizar listas automáticas"):
                                 QObject::tr("Gerar/atualizar documentação industrial");
    project->undoStack()->beginMacro(command);
    // Remove through native commands so refresh, including manual edits on
    // generated pages, is recoverable in one undo. Other sections are retained.
    const auto oldPages=project->diagrams();
    for(Diagram *d:oldPages)
        if(generated(d) && (section==Section::All || sheetKind(d)==selectedKind ||
           (section==Section::Lists && (sheetKind(d)=="materials" || sheetKind(d)=="symbols"))))
            project->undoStack()->push(new RemoveDiagramCommand(project,d));
    // Scan after detaching old documentation, so page references reflect the
    // actual source folios even when generated sheets were moved between them.
    const auto inv=SymbolLegend::inventory(project);
    const auto ts=section==Section::Symbols?QList<Table>{}:
        (section==Section::Materials || section==Section::Lists)?QList<Table>{materialTable(inv)}:tables(project);
    int count=0;
    auto page=[&](const QString&title,const QString&kind) {
        auto *d=project->addNewDiagram();auto props=d->border_and_titleblock.exportTitleBlock();
        props.title=title;props.context.addValue("qet_generated_documentation",true,false);
        props.context.addValue("qet_documentation_kind",kind,false);
        d->border_and_titleblock.importTitleBlock(props);
        d->border_and_titleblock.setColumnsCount(18);d->border_and_titleblock.setColumnsWidth(60);
        d->border_and_titleblock.setDiagramHeight(760);
        d->setBorderOptions(Diagram::EmptyBorder);
        text(d,title,QRectF(30,20,1040,35),true);border(d,QRectF(30,20,1040,35));
        ++count;return d;
    };
    // Actual native vector symbols; document folios are excluded from topology/BOM.
    for(int offset=0;section!=Section::Materials && offset<std::max(1,int(inv.symbols.size()));offset+=21) {
        Diagram*d=page("SIMBOLOGIA","symbols");
        if(inv.symbols.isEmpty())text(d,QObject::tr("Nenhum símbolo no projeto."),QRectF(30,65,1040,35));
        for(int n=0;n<21 && offset+n<inv.symbols.size();++n) {
            const auto&r=inv.symbols.at(offset+n);const int col=n%3,row=n/3;
            const QRectF box(30+col*346,65+row*92,346,92);
            border(d,box);text(d,"SÍMBOLO    | LET. | DESCRIÇÃO",QRectF(box.x(),box.y(),346,20),true);
            text(d,r.abbreviation,QRectF(box.x()+77,box.y()+27,40,60),true);
            text(d,r.kind,QRectF(box.x()+120,box.y()+24,220,65));
            QPicture vector=SymbolLegend::symbolVector(r.symbol);
            auto *graphic=new QetShapeItem(QPointF(0,0),QPointF(60,58),QetShapeItem::Rectangle);
            graphic->setDocumentaryVector(vector);graphic->setPos(box.x()+8,box.y()+26);
            d->addItem(graphic);
        }
    }
    for(const auto&t:ts) {
        const auto ws=widths(t,1040);Diagram*d=nullptr;int y=0;
        auto newPage=[&]() {
            d=page(t.title,t.title==QStringLiteral("RELAÇÃO DE MATERIAIS")?
                   QStringLiteral("materials"):QStringLiteral("other"));y=65;int x=30;
            for(int i=0;i<ws.size();++i){QRectF cell(x,y,ws[i],35);border(d,cell);text(d,t.headings[i],cell,true);x+=ws[i];}y+=35;
        };newPage();
        const auto rows=t.rows.isEmpty()?QList<QStringList>{{QStringLiteral("Nenhum registro aplicável.")}}:t.rows;
        for(const auto&r:rows) {
            for(const auto &part:splitNativeRow(r,ws)) {
                const int h=nativeRowHeight(part,ws);
                if(y+h>715)newPage();
                int x=30;for(int i=0;i<ws.size();++i){QRectF cell(x,y,ws[i],h);border(d,cell);text(d,part.value(i),cell);x+=ws[i];}y+=h;
            }
        }
    }
    project->undoStack()->endMacro();return count;
}
inline int createMaterials(QETProject *project) { return createSheets(project,Section::Materials); }
inline int createSymbols(QETProject *project) { return createSheets(project,Section::Symbols); }
inline QString autoRefreshKey() { return QStringLiteral("qet_auto_refresh_lists"); }
inline bool autoRefreshEnabled(QETProject *project) {
    return project && project->projectProperties().value(autoRefreshKey()).toBool();
}
class AutoRefreshCommand : public QUndoCommand {
    QPointer<QETProject> m_project;
    QVariant m_before;
    bool m_hadBefore, m_enabled;
    void apply(const QVariant &value,bool present) {
        if(!m_project)return;
        auto props=m_project->projectProperties();
        if(present)props.addValue(autoRefreshKey(),value,false);
        else props.remove(autoRefreshKey());
        m_project->setProjectProperties(props);m_project->setModified(true);
    }
public:
    AutoRefreshCommand(QETProject *project,bool enabled):m_project(project),
        m_before(project->projectProperties().value(autoRefreshKey())),
        m_hadBefore(project->projectProperties().contains(autoRefreshKey())),m_enabled(enabled) {
        setText(QObject::tr("Atualização automática das listas ao salvar"));
    }
    void redo() override { apply(m_enabled,true); }
    void undo() override { apply(m_before,m_hadBefore); }
};
inline void setAutoRefresh(QETProject *project,bool enabled) {
    if(project && !project->isReadOnly() && project->undoStack() && autoRefreshEnabled(project)!=enabled)
        project->undoStack()->push(new AutoRefreshCommand(project,enabled));
}
inline void refreshListsBeforeSave(QETProject *project) {
    // Saving an untouched file remains stable; this opt-in policy is stored
    // in the project, and uses the same atomic undo as explicit generation.
    if(autoRefreshEnabled(project))
        createSheets(project,Section::Lists);
}
inline void generateSheets(QWidget *parent,QETProject *project,Section section=Section::All) {
    if(!project)return;
    const int n=createSheets(project,section);
    if(n)QMessageBox::information(parent,"Documentação",QStringLiteral("%1 folhas editáveis geradas/atualizadas com os dados atuais do projeto. As folhas automáticas desta seção são substituídas a cada execução. Use Exportar PDF do QET para exportá-las. Ctrl+Z restaura a versão anterior.").arg(n));
    else QMessageBox::warning(parent,"Documentação","Não foi possível gerar as folhas.");
}
inline void generateMaterials(QWidget *parent,QETProject *project) { generateSheets(parent,project,Section::Materials); }
inline void generateSymbols(QWidget *parent,QETProject *project) { generateSheets(parent,project,Section::Symbols); }
}
#endif
