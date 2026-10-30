/* GPL-2.0-or-later. Native workflow for feeder/branch/N/PE sizing. */
#ifndef QET_PROJECT_CIRCUIT_SIZING_UI_H
#define QET_PROJECT_CIRCUIT_SIZING_UI_H
#include "projectcircuitsizing.h"
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QInputDialog>
#include <QSaveFile>
#include <QFile>
#include <QScrollArea>
#include <QCompleter>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <functional>
namespace ProjectCircuitSizing {
inline QLineEdit *field(QFormLayout *form,const QString &title,const QString &value={}) {
    auto *f=new QLineEdit(value);form->addRow(title,f);return f;
}
inline QTableWidget *table(QWidget *parent,const QStringList &headers) {
    auto *t=new QTableWidget(0,headers.size(),parent);t->setHorizontalHeaderLabels(headers);
    t->setAlternatingRowColors(true);t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->horizontalHeader()->setDefaultSectionSize(125);return t;
}
inline void cell(QTableWidget *t,int row,int col,const QString &s,bool editable=true) {
    auto *i=new QTableWidgetItem(s);if(!editable)i->setFlags(i->flags() & ~Qt::ItemIsEditable);t->setItem(row,col,i);
}
inline QString value(QTableWidget *t,int row,int col) {
    if(auto *c=qobject_cast<QComboBox*>(t->cellWidget(row,col)))return c->currentData().toString();
    return t->item(row,col)?t->item(row,col)->text().trimmed():QString();
}
inline QSet<int> rows(QTableWidget *t) {
    QSet<int> selected;for(const auto &i:t->selectionModel()->selectedRows())selected.insert(i.row());
    if(selected.isEmpty())for(int row=0;row<t->rowCount();++row)selected.insert(row);return selected;
}
inline void editCurve(QWidget *parent,QTableWidget *protections) {
    const int row=protections->currentRow();if(row<0)return;
    QDialog d(parent);d.setWindowTitle("Envelope tempo × corrente do fabricante");d.resize(650,480);
    auto *layout=new QVBoxLayout(&d);auto *form=new QFormLayout;
    auto *reference=field(form,"Documento / produto / tensão / edição:",value(protections,row,6));layout->addLayout(form);
    auto *nominal=field(form,"Corrente nominal In do modelo A:",value(protections,row,1));
    auto *t=table(&d,{"Corrente A","Tempo mínimo s","Tempo máximo s"});t->setObjectName("qet_trip_curve_table");layout->addWidget(t);
    const auto points=protections->item(row,0)->data(Qt::UserRole+1).toJsonArray();
    for(const auto &v:points){const auto p=v.toArray();const int r=t->rowCount();t->insertRow(r);
        for(int c=0;c<3;++c)cell(t,r,c,QString::number(p[c].toDouble(),'g',12));}
    auto *add=new QPushButton("Adicionar ponto");layout->addWidget(add);
    QObject::connect(add,&QPushButton::clicked,&d,[&](){const int r=t->rowCount();t->insertRow(r);for(int c=0;c<3;++c)cell(t,r,c,{});});
    auto *help=new QLabel("Use o envelope documentado do dispositivo, em ampères absolutos. Tempos devem ser não crescentes. Faixas fora dos pontos permanecem pendentes. Apague uma linha inteira para removê-la.");
    help->setWordWrap(true);layout->addWidget(help);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&d,[&](){
        QJsonArray data;QVector<RadialSizing::CurvePoint> parsed;
        for(int r=0;r<t->rowCount();++r) {
            if(value(t,r,0).isEmpty() && value(t,r,1).isEmpty() && value(t,r,2).isEmpty())continue;
            const double a=number(value(t,r,0)),lo=number(value(t,r,1)),hi=number(value(t,r,2));
            parsed<<RadialSizing::CurvePoint{a,lo,hi};data<<QJsonArray{a,lo,hi};
        }
        if(!parsed.isEmpty() && (!RadialSizing::validCurve(parsed) || reference->text().trimmed().isEmpty() || !RadialSizing::positive(number(nominal->text())))) {
            QMessageBox::warning(&d,"Curva inválida","Confira a referência, correntes crescentes e limites positivos/não crescentes de tempo.");return;
        }
        protections->item(row,0)->setData(Qt::UserRole+1,data);
        protections->item(row,6)->setText(reference->text().trimmed());
        if(!data.isEmpty())protections->item(row,1)->setText(nominal->text().trimmed());
        protections->item(row,2)->setText(data.isEmpty()?QString():"Fabricante");d.accept();
    });
    QObject::connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);d.exec();
}
inline void run(QWidget *parent,QETProject *project) {
    if(!project || project->isReadOnly() || !project->undoStack())return;
    QJsonObject draft=settings(project);if(draft.value("schema").toInt()!=1) {
        QMessageBox::warning(parent,"Dimensionamento","Versão de dados não suportada. Preserve o arquivo e use a versão que o gravou.");return;
    }
    if(!draft["profiles"].toObject().contains("default")) {
        auto p=draft["profiles"].toObject();p["default"]=profileJson(preliminaryProfile());draft["profiles"]=p;
    }
    const auto initialTrace=WireTrace::scan(project);
    QList<Element*> allElements;for(Diagram *d:project->diagrams())if(!d->border_and_titleblock.additionalFields().value("qet_generated_documentation").toBool())allElements<<d->elements();
    std::sort(allElements.begin(),allElements.end(),[](Element *a,Element *b){return QString::localeAwareCompare(label(a),label(b))<0;});
    QDialog dialog(parent);dialog.setObjectName("qet_radial_sizing_dialog");dialog.setWindowTitle("Dimensionar alimentação, derivações, neutro e PE");dialog.resize(1380,870);
    auto *outer=new QVBoxLayout(&dialog);auto *tabs=new QTabWidget(&dialog);outer->addWidget(tabs);
    auto page=[&](const QString &name){auto *w=new QWidget;auto *l=new QVBoxLayout(w);tabs->addTab(w,name);return l;};
    auto help=[&](QVBoxLayout *l,const QString &s){auto *h=new QLabel(s);h->setWordWrap(true);l->addWidget(h);};
    auto *sourceLayout=page("1. Origem e condições");
    help(sourceLayout,"Escolha o limite da alimentação e os terminais de saída. O rastreamento percorre fios, pares elétricos e setas nativas entre folhas. Cada derivação conserva sua própria corrente. Fontes/conversores não unem domínios de tensão. Comprimento é físico, nunca medido no desenho.");
    auto *source=new QComboBox;source->setObjectName("qet_radial_source");source->setEditable(true);
    source->addItem("Selecione o componente de origem",QString());
    for(Element *e:allElements)if(!(e->linkType() & Element::AllReport) && !e->terminals().isEmpty())source->addItem(label(e),id(e->uuid()));
    source->completer()->setFilterMode(Qt::MatchContains);sourceLayout->addWidget(source);
    auto *rootForm=new QFormLayout;sourceLayout->addLayout(rootForm);
    auto *supply=new QComboBox;supply->addItem("Não declarada","");supply->addItem("CA monofásica L-N","1F");supply->addItem("CA bifásica 120° — tensão entre fases","2F");supply->addItem("CA trifásica — tensão entre fases","3F");supply->addItem("Corrente contínua","CC");
    rootForm->addRow("Alimentação:",supply);
    QMap<QString,QLineEdit*> rootFields;
    for(const auto &p:QList<QPair<QString,QString>>{{"voltage","Tensão nominal V:"},{"drop","Queda acumulada máxima %:"},
        {"frequency_hz","Frequência Hz (CA):"},
        {"start_drop","Queda máxima de partida %:"},{"source_r","Resistência externa de laço R Ω:"},
        {"source_x","Reatância externa de laço X Ω (0 se desprezível):"},{"fault_max","Curto máximo na origem A:"},
        {"cmin","Fator mínimo de tensão (0 < c ≤ 1):"}})rootFields[p.first]=field(rootForm,p.second);
    auto *earth=new QComboBox;earth->addItem("Não declarado","");earth->addItem("TN","TN");earth->addItem("TT — requer dados do DR e terra","TT");rootForm->addRow("Aterramento:",earth);
    auto *states=new QCheckBox("Confirmo o cenário de funcionamento: contatos dos percursos habilitados considerados fechados");states->setObjectName("qet_radial_contacts_confirmed");sourceLayout->addWidget(states);
    auto *roots=table(&dialog,{"Função na origem","Terminal real do componente"});roots->setRowCount(RadialSizing::RoleCount);
    roots->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);sourceLayout->addWidget(roots);
    for(int r=0;r<RadialSizing::RoleCount;++r){cell(roots,r,0,RadialSizing::roleName(RadialSizing::Role(r)),false);roots->setCellWidget(r,1,new QComboBox);}
    auto *refresh=new QPushButton("Rastrear e atualizar as listas desta origem");refresh->setObjectName("qet_radial_refresh");sourceLayout->addWidget(refresh);

    auto *loadLayout=page("2. Cargas");
    help(loadLayout,"Dados existentes são carregados primeiro. H3 em % da fundamental (0 quando ausente); informe outras ordens pelo botão de espectro. Espectro vazio declara demais ordens desprezíveis. O cálculo usa limites conservadores de soma harmônica e do neutro, sem presumir cancelamentos. Classe I exige PE; II/III exigem declaração. Corrente é RMS; potência é ativa em W. Motores exigem dados de partida.");
    const QStringList loadKeys{"qet_voltage_v","qet_current_a","qet_power_w","qet_supply","qet_power_factor","qet_efficiency",
        "qet_power_kind","qet_harmonic3_percent","qet_protection_class","qet_demand_factor","qet_load_kind",
        "qet_start_multiple","qet_start_seconds","qet_start_pf","qet_phase_l1_a","qet_phase_l2_a","qet_phase_l3_a"};
    auto *loads=table(&dialog,{"Carga / folha","Tensão V","Corrente RMS A","Potência W","1F / 3F / CC","cos φ","Rendimento η",
        "Tipo de potência","H3 %","Classe I/II/III","Demanda (0–1)","motor / carga","Ipart / In","Partida s","cos φ partida","I L1 A","I L2 A","I L3 A"});
    loads->setObjectName("qet_radial_loads");loads->setColumnWidth(0,280);loadLayout->addWidget(loads);
    auto *spectrumButton=new QPushButton("Editar outras harmônicas da carga selecionada...");loadLayout->addWidget(spectrumButton);

    auto *wireLayout=page("3. Trechos e cabos");
    help(wireLayout,"Uma linha por trecho físico. O cabo/circuito reúne suas fases, neutro e PE e permite dimensionar PE pela seção das fases. Dê identificadores diferentes às derivações. Dados dos cabos nativos são reaproveitados quando a metragem representa um único trecho por veia. A edição em lote alcança linhas selecionadas; sem seleção, todas as linhas visíveis.");
    auto *wires=table(&dialog,{"Trecho / folha / terminais","Cabo/circuito","Ida física m","Kt temperatura","Kg agrupamento","Carregados 2/3/4","S mínima mm²","Perfil",
        "S cadastrada","Função calculada","Ib A","S calculada mm²"});wires->setObjectName("qet_radial_wires");wires->setColumnWidth(0,330);wireLayout->addWidget(wires);
    const QStringList wireKeys{"cable","length","kt","kg","loaded","minimum","profile"};
    auto *bulkWire=new QPushButton("Preencher dados comuns nas linhas selecionadas...");wireLayout->addWidget(bulkWire);

    auto *profileLayout=page("4. Perfis de instalação");
    help(profileLayout,"Tabela e parâmetros devem corresponder ao material, isolação, método, temperatura de referência e condutores carregados. O perfil inicial é conservador e começa sem confirmação. Importe uma tabela aplicável ou confira os valores e sua fonte. Os fatores Kt/Kg são declarados por trecho. Mínimo PE deve contemplar se o condutor é integrante do cabo, separado e protegido mecanicamente.");
    auto *profileChoice=new QComboBox;profileLayout->addWidget(profileChoice);auto *profileForm=new QFormLayout;profileLayout->addLayout(profileForm);
    QMap<QString,QLineEdit*> profileFields;
    for(const auto &p:QList<QPair<QString,QString>>{{"material","Material (Cu/Al):"},{"insulation","Isolação / temperatura:"},
        {"method","Método de instalação:"},{"reference","Documento / tabela / edição / condições de referência:"},
        {"rho","Resistividade efetiva máxima no espectro/temperatura Ω·mm²/m:"},{"x_ohm_km","Reatância fundamental Ω/km:"},{"k","Constante adiabática k:"},
        {"harmonic_factor","Fator térmico para harmônicas (0 = dado pendente):"},
        {"minimum_phase","Mínimo de fase mm²:"},{"minimum_n","Mínimo de neutro mm²:"},{"minimum_pe","Mínimo físico de PE mm²:"}})
        profileFields[p.first]=field(profileForm,p.second);
    auto *profileConfirmed=new QCheckBox("Conferi a referência, a tabela e sua aplicação às condições declaradas");profileLayout->addWidget(profileConfirmed);
    auto *ampacity=table(&dialog,{"Seção mm²","Iz: 2 carregados A","Iz: 3 carregados A","Iz: 4 carregados A"});profileLayout->addWidget(ampacity);
    auto *profileButtons=new QHBoxLayout;auto *newProfile=new QPushButton("Duplicar perfil...");auto *importProfile=new QPushButton("Importar tabela CSV...");auto *addAmpacity=new QPushButton("Adicionar seção");
    profileButtons->addWidget(newProfile);profileButtons->addWidget(importProfile);profileButtons->addWidget(addAmpacity);profileLayout->addLayout(profileButtons);

    auto *protectionLayout=page("5. Proteções e seletividade");
    help(protectionLayout,"In = 0 propõe um calibre da série; um valor positivo fixa o dispositivo existente. Declare curva e capacidade de interrupção aplicáveis à tensão CA/CC. B/C/D usa somente limites garantidos IEC 60898-1, sem criar uma curva de fabricante. Para CC use envelope documentado. TT permite DR a montante cobrindo derivações. Seletividade requer envelopes separados ou associação documentada; In maior, isoladamente, não comprova seletividade.");
    auto *protections=table(&dialog,{"Dispositivo","In A (0 auto)","Curva B/C/D/Fabricante","Interrupção A","I2 / In","Seccionamento máx. s","Documento da curva",
        "Neutro protegido","Uso CC declarado","DR IΔn A","DR tempo máx. s","DR nominal A","RA Ω","UL V","Documento DR"});
    protections->setObjectName("qet_radial_protections");protections->setColumnWidth(0,280);protectionLayout->addWidget(protections);
    const QStringList protectionKeys{"in","curve","breaking_a","i2_multiple","max_seconds","reference","neutral_protected","dc_rated",
        "rcd_a","rcd_seconds","rcd_rated_a","ra_ohm","ul_v","rcd_reference"};
    auto *protectionButtons=new QHBoxLayout;auto *curve=new QPushButton("Editar envelope do dispositivo selecionado...");auto *bulkProtection=new QPushButton("Preencher dados comuns das proteções...");
    protectionButtons->addWidget(curve);protectionButtons->addWidget(bulkProtection);protectionLayout->addLayout(protectionButtons);
    auto *pairs=table(&dialog,{"Proteção a montante","Proteção a jusante","Limite de seletividade A","Documento / produtos / tensão"});pairs->setObjectName("qet_radial_selectivity");
    pairs->setMaximumHeight(180);protectionLayout->addWidget(pairs);auto *addPair=new QPushButton("Adicionar associação de seletividade documentada");protectionLayout->addWidget(addPair);
    auto *pairLayout=page("6. Pares internos");
    help(pairLayout,"Confirme somente pares de terminais eletricamente verificados. Pares inferidos pela geometria e alternativas de comutadores começam desmarcados. Cargas e conversores permanecem barreiras. Não confirme simultaneamente alternativas mutuamente exclusivas.");
    auto *contacts=new QListWidget;contacts->setObjectName("qet_radial_internal_pairs");pairLayout->addWidget(contacts);
    QHash<Terminal*,int> degree;for(const auto &p:initialTrace.passages){++degree[p.terminalA];++degree[p.terminalB];}
    QSet<QString> savedPairs;for(const auto &v:draft["confirmed_pairs"].toArray())savedPairs.insert(v.toString());
    for(const auto &p:initialTrace.passages)if(!p.traversable || degree[p.terminalA]>1 || degree[p.terminalB]>1) {
        if(!p.device || ElectricalData::load(p.device) || p.behaviour.startsWith("CONVERSÃO") || p.behaviour.startsWith("CARGA"))continue;
        const QString pid=passageId(p);auto *i=new QListWidgetItem(label(p.device)+" | "+p.pinA+" ↔ "+p.pinB+" | "+p.behaviour,contacts);
        i->setData(Qt::UserRole,pid);i->setFlags(i->flags()|Qt::ItemIsUserCheckable);i->setCheckState(savedPairs.contains(pid)?Qt::Checked:Qt::Unchecked);
    }
    auto *resultLayout=page("7. Revisão e aplicação");auto *resultText=new QPlainTextEdit;resultText->setReadOnly(true);
    resultText->setObjectName("qet_radial_result");resultLayout->addWidget(resultText);
    auto *overwrite=new QCheckBox("Permitir substituir seções/propriedades técnicas existentes nos trechos calculados");outer->addWidget(overwrite);
    auto *automatic=new QCheckBox("Recalcular todas as origens cadastradas ao salvar — preservar seções manuais");automatic->setObjectName("qet_radial_auto_save");
    automatic->setChecked(draft["auto_recalculate"].toBool());automatic->setToolTip("Atualiza com todos os critérios e dados disponíveis, preserva seções manuais e regenera documentação técnica automática existente. Pendências mantêm seções anteriores e ficam registradas para revisão. Salvar parâmetros grava esta opção no projeto.");outer->addWidget(automatic);
    auto *buttons=new QHBoxLayout;auto *save=new QPushButton("Salvar parâmetros");auto *calculateButton=new QPushButton("Calcular / revisar");auto *applyButton=new QPushButton("Aplicar resultado verificado");
    auto *exportButton=new QPushButton("Exportar relatório...");auto *close=new QPushButton("Fechar");
    auto *calculateAll=new QPushButton("Revisar todas as origens");auto *applyAllButton=new QPushButton("Aplicar todas verificadas");applyAllButton->setEnabled(false);
    calculateAll->setObjectName("qet_radial_calculate_all");applyAllButton->setObjectName("qet_radial_apply_all");
    applyButton->setEnabled(false);calculateButton->setObjectName("qet_radial_calculate");applyButton->setObjectName("qet_radial_apply");save->setObjectName("qet_radial_save");
    for(auto *b:{save,calculateButton,applyButton,calculateAll,applyAllButton,exportButton,close})buttons->addWidget(b);outer->addLayout(buttons);
    QString activeSource,activeProfile;bool populating=false;Analysis current;
    auto invalidate=[&](){if(!populating){applyButton->setEnabled(false);applyAllButton->setEnabled(false);}};
    auto fillPairChoices=[&](){
        auto ids=current.network.protections.keys();ids.sort();
        for(int r=0;r<pairs->rowCount();++r)for(int c=0;c<2;++c) {
            const QString selected=value(pairs,r,c);auto *combo=new QComboBox;combo->addItem("Selecione a proteção",QString());
            for(const QString &pid:ids)combo->addItem(current.labels.value(pid,pid),pid);
            if(!selected.isEmpty() && combo->findData(selected)<0)combo->addItem("Outra origem — "+selected,selected);
            combo->setCurrentIndex(std::max(0,combo->findData(selected)));pairs->setCellWidget(r,c,combo);
            QObject::connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){invalidate();});
        }
    };
    auto storeProfile=[&](){if(activeProfile.isEmpty())return;QJsonObject p;
        for(auto it=profileFields.cbegin();it!=profileFields.cend();++it) {
            if(QStringList{"material","insulation","method","reference"}.contains(it.key()))p[it.key()]=it.value()->text().trimmed();
            else p[it.key()]=number(it.value()->text());
        }
        p["confirmed"]=profileConfirmed->isChecked();QJsonArray values;
        for(int r=0;r<ampacity->rowCount();++r) {
            if(value(ampacity,r,0).isEmpty())continue;
            QJsonArray row;for(int c=0;c<4;++c)row<<number(value(ampacity,r,c));values<<row;
        }
        p["table"]=values;auto all=draft["profiles"].toObject();all[activeProfile]=p;draft["profiles"]=all;
    };
    auto store=[&](){
        storeProfile();if(!activeSource.isEmpty()) {
            auto sources=draft["sources"].toObject();auto cfg=sources[activeSource].toObject();
            for(auto it=rootFields.cbegin();it!=rootFields.cend();++it)cfg[it.key()]=number(it.value()->text());
            cfg["supply"]=supply->currentData().toString();cfg["earthing"]=earth->currentData().toString();cfg["contacts_confirmed"]=states->isChecked();
            QJsonObject r;for(int k=0;k<RadialSizing::RoleCount;++k)r[roleKey(k)]=qobject_cast<QComboBox*>(roots->cellWidget(k,1))->currentData().toString();
            cfg["roots"]=r;sources[activeSource]=cfg;draft["sources"]=sources;draft["source"]=activeSource;
        }
        draft["auto_recalculate"]=automatic->isChecked();
        auto allW=draft["wires"].toObject();for(int r=0;r<wires->rowCount();++r) {
            const QString wid=wires->item(r,0)->data(Qt::UserRole).toString();auto w=allW[wid].toObject();
            for(int c=0;c<wireKeys.size();++c)w[wireKeys[c]]=(c==0 || c==6)?QJsonValue(value(wires,r,c+1)):
                QJsonValue(number(value(wires,r,c+1)));allW[wid]=w;
        }draft["wires"]=allW;
        auto allL=draft["loads"].toObject();for(int r=0;r<loads->rowCount();++r) {
            const QString lid=loads->item(r,0)->data(Qt::UserRole).toString();QJsonObject l;
            for(int c=0;c<loadKeys.size();++c)l[loadKeys[c]]=value(loads,r,c+1);allL[lid]=l;
            l["qet_harmonic_spectrum"]=loads->item(r,0)->data(Qt::UserRole+1).toString();allL[lid]=l;
        }draft["loads"]=allL;
        auto allP=draft["protections"].toObject();for(int r=0;r<protections->rowCount();++r) {
            const QString pid=protections->item(r,0)->data(Qt::UserRole).toString();auto p=allP[pid].toObject();
            for(int c=0;c<protectionKeys.size();++c) {
                if(c==1 || c==5 || c==13)p[protectionKeys[c]]=value(protections,r,c+1);
                else if(c==6 || c==7)p[protectionKeys[c]]=protections->item(r,c+1)->checkState()==Qt::Checked;
                else p[protectionKeys[c]]=number(value(protections,r,c+1));
            }
            p["points"]=protections->item(r,0)->data(Qt::UserRole+1).toJsonArray();allP[pid]=p;
        }draft["protections"]=allP;
        QJsonArray associations;for(int r=0;r<pairs->rowCount();++r)if(!value(pairs,r,0).isEmpty() || !value(pairs,r,1).isEmpty())
            associations<<QJsonObject{{"upstream",value(pairs,r,0)},{"downstream",value(pairs,r,1)},
                {"limit_a",number(value(pairs,r,2))},{"reference",value(pairs,r,3)}};
        draft["selectivity"]=associations;
        QJsonArray confirmed;for(int i=0;i<contacts->count();++i)if(contacts->item(i)->checkState()==Qt::Checked)confirmed<<contacts->item(i)->data(Qt::UserRole).toString();
        draft["confirmed_pairs"]=confirmed;
    };
    auto showProfile=[&](const QString &name){populating=true;activeProfile=name;const auto p=draft["profiles"].toObject()[name].toObject();
        for(auto it=profileFields.cbegin();it!=profileFields.cend();++it)it.value()->setText(p[it.key()].isString()?p[it.key()].toString():QString::number(p[it.key()].toDouble(),'g',12));
        profileConfirmed->setChecked(p["confirmed"].toBool());ampacity->setRowCount(0);
        for(const auto &v:p["table"].toArray()){const auto row=v.toArray();const int r=ampacity->rowCount();ampacity->insertRow(r);
            for(int c=0;c<4;++c)cell(ampacity,r,c,QString::number(c<row.size()?row[c].toDouble():0,'g',12));}
        populating=false;invalidate();
    };
    auto showSource=[&](const QString &eid){populating=true;activeSource=eid;const auto cfg=draft["sources"].toObject()[eid].toObject();
        Element *e=nullptr;for(Element *candidate:allElements)if(id(candidate->uuid())==eid)e=candidate;
        const auto info=information(e);
        supply->setCurrentIndex(std::max(0,supply->findData(cfg.value("supply").toString(info.value("qet_supply").toString()))));
        earth->setCurrentIndex(std::max(0,earth->findData(cfg["earthing"].toString())));states->setChecked(cfg["contacts_confirmed"].toBool());
        for(auto it=rootFields.cbegin();it!=rootFields.cend();++it)it.value()->setText(cfg.contains(it.key())?QString::number(cfg[it.key()].toDouble(),'g',12):
            it.key()=="voltage"?info.value("qet_voltage_v").toString():QString());
        for(int r=0;r<RadialSizing::RoleCount;++r) {
            auto *combo=qobject_cast<QComboBox*>(roots->cellWidget(r,1));combo->clear();combo->addItem("Não utilizado / não informado",QString());
            if(e)for(Terminal *t:e->terminals())combo->addItem(t->name()+" ["+id(t->stableUuid()).left(10)+"]",terminalId(t));
            QString saved=cfg["roots"].toObject()[roleKey(r)].toString();
            if(saved.isEmpty() && e)for(Terminal *t:e->terminals())if(t->name().trimmed().toUpper()==roleKey(r))saved=terminalId(t);
            combo->setCurrentIndex(std::max(0,combo->findData(saved)));
        }
        populating=false;invalidate();
    };
    auto populate=[&](){
        store();current=analyze(project,draft,overwrite->isChecked());populating=true;
        wires->setRowCount(0);loads->setRowCount(0);protections->setRowCount(0);
        QList<QString> ids=current.domainWires.values();ids.sort();
        for(const QString &wid:ids) {
            RadialSizing::Edge e;for(const auto &candidate:current.network.edges)if(candidate.id==wid)e=candidate;
            const int r=wires->rowCount();wires->insertRow(r);cell(wires,r,0,current.labels.value(wid),false);wires->item(r,0)->setData(Qt::UserRole,wid);
            const QStringList values{e.cable,QString::number(e.length,'g',12),QString::number(e.temperatureFactor,'g',12),QString::number(e.groupingFactor,'g',12),
                QString::number(e.loaded),QString::number(e.minimum,'g',12),e.profile,current.wires.value(wid)?current.wires.value(wid)->properties().m_wire_section:QString()};
            for(int c=0;c<values.size();++c)cell(wires,r,c+1,values[c],c<7);
            for(int c=9;c<12;++c)cell(wires,r,c,{},false);
        }
        ids=current.domainLoads.values();ids.sort();
        for(const QString &lid:ids) {
            const int r=loads->rowCount();loads->insertRow(r);cell(loads,r,0,current.labels.value(lid),false);loads->item(r,0)->setData(Qt::UserRole,lid);
            const auto e=current.elements.value(lid);const auto info=information(e);const auto overrides=draft["loads"].toObject()[lid].toObject();
            loads->item(r,0)->setData(Qt::UserRole+1,overrides.contains("qet_harmonic_spectrum")?
                overrides["qet_harmonic_spectrum"].toString():info.value("qet_harmonic_spectrum").toString());
            for(int c=0;c<loadKeys.size();++c) {
                const auto text=overrides.contains(loadKeys[c])?overrides[loadKeys[c]].toString():info.value(loadKeys[c]).toString();
                if(c==3 || c==6 || c==8) {
                    auto *combo=new QComboBox;
                    if(c==3){combo->addItem("Não declarada","");for(const QString &s:QStringList{"1F","3F","CC"})combo->addItem(s,s);}
                    if(c==6){combo->addItem("Elétrica ativa","electrical");combo->addItem("Mecânica (exige η)","mechanical");}
                    if(c==8){combo->addItem("Não declarada","");combo->addItem("I — com PE","I");combo->addItem("II — dupla isolação","II");combo->addItem("III — extrabaixa tensão","III");}
                    combo->setCurrentIndex(std::max(0,combo->findData(text)));loads->setCellWidget(r,c+1,combo);
                    QObject::connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){invalidate();});
                } else cell(loads,r,c+1,text);
            }
        }
        auto prot=current.network.protections;ids=prot.keys();ids.sort();
        for(const QString &pid:ids) {
            const auto p=prot[pid];const auto old=draft["protections"].toObject()[pid].toObject();const int r=protections->rowCount();protections->insertRow(r);
            cell(protections,r,0,current.labels.value(pid,pid)+"\nID: "+pid,false);protections->item(r,0)->setData(Qt::UserRole,pid);
            protections->item(r,0)->setData(Qt::UserRole+1,old["points"].toArray());
            const QStringList values{QString::number(p.fixedIn,'g',12),p.points.isEmpty()?p.curve:QStringLiteral("Fabricante"),QString::number(p.breakingA,'g',12),
                QString::number(p.i2Multiple,'g',12),QString::number(p.maxDisconnect,'g',12),p.reference,QString(),QString(),
                QString::number(p.rcdA,'g',12),QString::number(p.rcdMaxSeconds,'g',12),QString::number(p.rcdRatedA,'g',12),
                QString::number(p.earthResistance,'g',12),QString::number(p.touchLimit,'g',12),p.rcdReference};
            for(int c=0;c<values.size();++c){cell(protections,r,c+1,values[c]);if(c==6 || c==7){auto *i=protections->item(r,c+1);i->setFlags(i->flags()|Qt::ItemIsUserCheckable);i->setCheckState((c==6?p.neutralProtected:p.dcRated)?Qt::Checked:Qt::Unchecked);}}
        }
        fillPairChoices();populating=false;resultText->setPlainText(report(current));invalidate();
    };
    for(const QString &name:draft["profiles"].toObject().keys())profileChoice->addItem(name);
    showProfile(profileChoice->currentText());
    const auto associations=draft["selectivity"].toArray();for(const auto &v:associations) {
        const auto p=v.toObject();const int r=pairs->rowCount();pairs->insertRow(r);
        const QStringList values{p["upstream"].toString(),p["downstream"].toString(),QString::number(p["limit_a"].toDouble(),'g',12),p["reference"].toString()};
        for(int c=0;c<4;++c)cell(pairs,r,c,values[c]);
    }
    QString chosen=draft["source"].toString();if(chosen.isEmpty())for(Element *e:allElements)if(e->isSelected() || e->elementInformations().value("qet_electrical_role").toString()=="source"){chosen=id(e->uuid());break;}
    source->setCurrentIndex(std::max(0,source->findData(chosen)));showSource(source->currentData().toString());populate();
    QObject::connect(source,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){if(populating)return;store();showSource(source->currentData().toString());populate();});
    QObject::connect(profileChoice,&QComboBox::currentTextChanged,&dialog,[&](const QString &s){storeProfile();showProfile(s);});
    QObject::connect(refresh,&QPushButton::clicked,&dialog,populate);
    for(auto *t:{loads,wires,protections,pairs,ampacity})QObject::connect(t,&QTableWidget::cellChanged,&dialog,[&](int,int){invalidate();});
    for(auto *f:rootFields)QObject::connect(f,&QLineEdit::textEdited,&dialog,[&](const QString &){invalidate();});
    for(auto *f:profileFields)QObject::connect(f,&QLineEdit::textEdited,&dialog,[&](const QString &){invalidate();});
    for(auto *c:{states,profileConfirmed,overwrite,automatic})QObject::connect(c,&QCheckBox::toggled,&dialog,[&](bool){invalidate();});
    for(auto *c:{supply,earth})QObject::connect(c,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){invalidate();});
    for(int r=0;r<RadialSizing::RoleCount;++r)QObject::connect(qobject_cast<QComboBox*>(roots->cellWidget(r,1)),qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){invalidate();});
    QObject::connect(contacts,&QListWidget::itemChanged,&dialog,[&](QListWidgetItem*){invalidate();});
    QObject::connect(newProfile,&QPushButton::clicked,&dialog,[&](){
        storeProfile();bool ok=false;const QString name=QInputDialog::getText(&dialog,"Novo perfil","Identificador do perfil:",QLineEdit::Normal,{},&ok).trimmed();
        if(!ok || name.isEmpty())return;auto profiles=draft["profiles"].toObject();if(profiles.contains(name)){QMessageBox::warning(&dialog,"Perfil","Esse perfil já existe.");return;}
        auto p=profiles[activeProfile].toObject();p["confirmed"]=false;profiles[name]=p;draft["profiles"]=profiles;profileChoice->addItem(name);profileChoice->setCurrentText(name);
    });
    QObject::connect(addAmpacity,&QPushButton::clicked,&dialog,[&](){const int r=ampacity->rowCount();ampacity->insertRow(r);for(int c=0;c<4;++c)cell(ampacity,r,c,{});});
    QObject::connect(importProfile,&QPushButton::clicked,&dialog,[&](){
        const auto path=QFileDialog::getOpenFileName(&dialog,"Tabela de capacidade: seção;Iz2;Iz3;Iz4",{},"CSV (*.csv);;Todos (*)");if(path.isEmpty())return;
        QFile f(path);if(!f.open(QIODevice::ReadOnly)){QMessageBox::warning(&dialog,"Tabela","Falha ao abrir tabela.");return;}
        const auto lines=QString::fromUtf8(f.readAll()).remove(QChar(0xfeff)).split(QRegularExpression("[\r\n]+"),Qt::SkipEmptyParts);
        QVector<QStringList> parsed;bool bad=false;
        for(const QString &line:lines){auto v=line.split(';');if(v.size()<3){bad=true;break;}
            for(auto &s:v)s=s.trimmed().remove('"');if(!std::isfinite(number(v[0])) || number(v[0])==0){if(parsed.isEmpty() && line==lines.first())continue;bad=true;break;}
            while(v.size()<4)v<<"0";parsed<<v;
        }
        if(bad || parsed.isEmpty()){QMessageBox::warning(&dialog,"Tabela","Use seção;Iz2;Iz3;Iz4, com unidades mm² e A. Iz4=0 significa dado indisponível.");return;}
        ampacity->setRowCount(parsed.size());for(int r=0;r<parsed.size();++r)for(int c=0;c<4;++c)cell(ampacity,r,c,parsed[r][c]);profileConfirmed->setChecked(false);
    });
    QObject::connect(curve,&QPushButton::clicked,&dialog,[&](){editCurve(&dialog,protections);invalidate();});
    QObject::connect(spectrumButton,&QPushButton::clicked,&dialog,[&](){
        const int selected=loads->currentRow();if(selected<0)return;
        QDialog d(&dialog);d.setWindowTitle("Espectro de corrente — demais harmônicas");d.resize(600,430);auto *l=new QVBoxLayout(&d);
        auto *t=table(&d,{"Ordem (2–99, exceto 3)","Corrente % da fundamental"});l->addWidget(t);
        const auto old=QJsonDocument::fromJson(loads->item(selected,0)->data(Qt::UserRole+1).toString().toUtf8()).array();
        for(const auto &v:old){const auto a=v.toArray();const int r=t->rowCount();t->insertRow(r);for(int c=0;c<2;++c)cell(t,r,c,c<a.size()?QString::number(a[c].toDouble(),'g',12):QString());}
        auto *add=new QPushButton("Adicionar ordem");l->addWidget(add);QObject::connect(add,&QPushButton::clicked,&d,[&](){const int r=t->rowCount();t->insertRow(r);cell(t,r,0,{});cell(t,r,1,{});});
        auto *b=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);l->addWidget(b);
        QObject::connect(b,&QDialogButtonBox::accepted,&d,[&](){
            QSet<int> seen;QJsonArray data;
            for(int r=0;r<t->rowCount();++r) {
                if(value(t,r,0).isEmpty() && value(t,r,1).isEmpty())continue;
                const double order=number(value(t,r,0)),ratio=number(value(t,r,1));
                if(!std::isfinite(order) || order<2 || order>99 || order==3 || order!=std::floor(order) ||
                   !RadialSizing::nonnegative(ratio) || ratio>10000 || seen.contains(int(order))) {
                    QMessageBox::warning(&d,"Espectro inválido","Use ordens inteiras distintas, de 2 a 99 (3ª no campo próprio), e amplitudes percentuais não negativas.");return;
                }seen.insert(int(order));data<<QJsonArray{int(order),ratio};
            }
            loads->item(selected,0)->setData(Qt::UserRole+1,QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));d.accept();invalidate();
        });
        QObject::connect(b,&QDialogButtonBox::rejected,&d,&QDialog::reject);d.exec();
    });
    QObject::connect(addPair,&QPushButton::clicked,&dialog,[&](){const int r=pairs->rowCount();pairs->insertRow(r);for(int c=0;c<4;++c)cell(pairs,r,c,{});fillPairChoices();invalidate();});
    QObject::connect(bulkWire,&QPushButton::clicked,&dialog,[&](){
        QDialog d(&dialog);d.setWindowTitle("Dados comuns dos trechos");auto *f=new QFormLayout(&d);
        auto *cable=field(f,"Cabo/circuito (vazio mantém):");auto *length=field(f,"Comprimento físico m (vazio mantém):");
        auto *kt=field(f,"Kt (vazio mantém):");auto *kg=field(f,"Kg (vazio mantém):");auto *loaded=field(f,"Carregados 2/3/4 (vazio mantém):");
        auto *profile=field(f,"ID do perfil (vazio mantém):");
        auto *b=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);f->addRow(b);
        QObject::connect(b,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(b,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        if(d.exec()!=QDialog::Accepted)return;
        for(int r:rows(wires))for(const auto &p:QList<QPair<int,QLineEdit*>>{{1,cable},{2,length},{3,kt},{4,kg},{5,loaded},{7,profile}})
            if(!p.second->text().trimmed().isEmpty())wires->item(r,p.first)->setText(p.second->text().trimmed());
    });
    QObject::connect(bulkProtection,&QPushButton::clicked,&dialog,[&](){
        QDialog d(&dialog);d.setWindowTitle("Dados comuns das proteções selecionadas");auto *f=new QFormLayout(&d);
        auto *type=field(f,"Curva IEC 60898-1 B/C/D (vazio mantém):");auto *breaking=field(f,"Interrupção aplicável à tensão A:");
        auto *i2=field(f,"I2 / In:");auto *time=field(f,"Tempo máximo de seccionamento s:");
        auto *b=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);f->addRow(b);
        QObject::connect(b,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(b,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        if(d.exec()!=QDialog::Accepted)return;
        for(int r:rows(protections)) {
            for(const auto &p:QList<QPair<int,QLineEdit*>>{{2,type},{3,breaking},{4,i2},{5,time}})
                if(!p.second->text().trimmed().isEmpty())protections->item(r,p.first)->setText(p.second->text().trimmed());
            if(!type->text().trimmed().isEmpty())protections->item(r,0)->setData(Qt::UserRole+1,QJsonArray());
        }invalidate();
    });
    QObject::connect(save,&QPushButton::clicked,&dialog,[&](){store();if(saveParameters(project,draft))QMessageBox::information(&dialog,"Parâmetros","Dados salvos no projeto. Ctrl+Z desfaz o lote. Nenhuma seção foi aplicada.");invalidate();});
    QObject::connect(calculateButton,&QPushButton::clicked,&dialog,[&](){
        store();current=analyze(project,draft,overwrite->isChecked());resultText->setPlainText(report(current));
        populating=true;for(int r=0;r<wires->rowCount();++r) {
            const auto wid=wires->item(r,0)->data(Qt::UserRole).toString();for(const auto &w:current.result.wires)if(w.id==wid) {
                wires->item(r,9)->setText(RadialSizing::roleName(w.role));wires->item(r,10)->setText(QString::number(w.current,'g',8));wires->item(r,11)->setText(QString::number(w.section,'g',8));
            }
        }populating=false;tabs->setCurrentIndex(6);applyButton->setEnabled(current.result.ready);
    });
    QObject::connect(applyButton,&QPushButton::clicked,&dialog,[&](){
        store();QStringList issues;if(!apply(project,draft,overwrite->isChecked(),&issues)){QMessageBox::warning(&dialog,"Dimensionamento pendente",issues.join('\n'));invalidate();return;}
        QMessageBox::information(&dialog,"Dimensionamento aplicado",QStringLiteral("%1 trechos atualizados, com correntes próprias de alimentador/ramais, neutro e PE. Ctrl+Z restaura os dados anteriores. Salve o projeto e gere/atualize a documentação.").arg(current.result.wires.size()));invalidate();
    });
    QObject::connect(calculateAll,&QPushButton::clicked,&dialog,[&](){
        store();const auto all=analyzeAll(project,draft,overwrite->isChecked());QStringList reports;
        for(const auto &a:all.domains)reports<<report(a);
        for(const auto &issue:all.issues)reports<<"PENDENTE NO CONJUNTO: "+issue;
        resultText->setPlainText(reports.join("\n\n"));tabs->setCurrentIndex(6);applyButton->setEnabled(false);applyAllButton->setEnabled(all.ready);
    });
    QObject::connect(applyAllButton,&QPushButton::clicked,&dialog,[&](){
        store();QStringList issues;if(!applyAll(project,draft,overwrite->isChecked(),&issues))QMessageBox::warning(&dialog,"Dimensionamento pendente",issues.join('\n'));
        else QMessageBox::information(&dialog,"Dimensionamento aplicado","Todas as origens cadastradas foram verificadas e aplicadas em um único lote. Ctrl+Z restaura o conjunto anterior. A verificação é condicionada aos dados e critérios apresentados.");
        invalidate();
    });
    QObject::connect(exportButton,&QPushButton::clicked,&dialog,[&](){
        const auto path=QFileDialog::getSaveFileName(&dialog,"Relatório de dimensionamento","dimensionamento.txt","Texto (*.txt)");if(path.isEmpty())return;
        QSaveFile f(path);if(!f.open(QIODevice::WriteOnly) || f.write(resultText->toPlainText().toUtf8())<0 || !f.commit())QMessageBox::warning(&dialog,"Relatório","Falha ao salvar relatório.");
    });
    QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();
}
}
#endif
