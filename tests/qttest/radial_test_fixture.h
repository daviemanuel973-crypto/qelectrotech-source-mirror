// GPL-2.0-or-later. Native, self-contained two-folio didactic QET fixture.
#ifndef QET_RADIAL_TEST_FIXTURE_H
#define QET_RADIAL_TEST_FIXTURE_H
#include <QDomDocument>
#include <QFile>
#include <QMap>
#include <QPoint>
#include <QUuid>
namespace RadialTestFixture {
inline QUuid uuid(const QString &name) {return QUuid::createUuidV5(QUuid("{ba4ed464-3b78-4fa5-9bc8-08f59666e752}"),name.toUtf8());}
inline QString uid(const QString &name) {return uuid(name).toString();}
inline bool write(const QString &path,bool threePhase=false) {
    QDomDocument doc;auto root=doc.createElement("project");doc.appendChild(root);
    root.setAttribute("version","0.200.1");root.setAttribute("uuid",uid("project"));
    root.setAttribute("title",threePhase?"DEMONSTRAÇÃO DIDÁTICA — 3F, disjuntor, contator, térmico e motor; dados não são catálogo":
        "DEMONSTRAÇÃO DIDÁTICA — alimentador 20 A, ramais 8 A / 12 A; parâmetros não são catálogo");
    root.appendChild(doc.createElement("properties"));root.appendChild(doc.createElement("newdiagrams"));
    auto collection=doc.createElement("collection"),category=doc.createElement("category");category.setAttribute("name","radial");collection.appendChild(category);
    struct Def {QString file,name,type;QList<QPair<QString,QPoint>> pins;};
    const QList<Def> definitions{
        {"source","Origem CA declarada","simple",{{"L1",{-20,20}},{"N",{0,20}},{"PE",{20,20}}}},
        {"source-dc","Origem 24 Vcc declarada","simple",{{"+",{-10,20}},{"-",{10,20}}}},
        {"load","Carga resistiva classe I","simple",{{"L1",{-10,-20}},{"N",{10,-20}},{"PE",{20,0}}}},
        {"load-dc","Carga 24 Vcc classe III","simple",{{"+",{-10,-20}},{"-",{10,-20}}}},
        {"breaker","Disjuntor monopolar","simple",{{"1",{0,-20}},{"2",{0,20}}}},
        {"bus-n","Borne de distribuição N","terminal",{{"N",{0,0}}}},
        {"bus-pe","Borne de distribuição PE","terminal",{{"PE",{0,0}}}},
        {"next","Continuação de saída","next_report",{{"",{0,0}}}},
        {"previous","Continuação de entrada","previous_report",{{"",{0,0}}}},
        {"source3","Origem trifásica declarada","simple",{{"L1",{-30,20}},{"L2",{-10,20}},{"L3",{10,20}},{"PE",{30,20}}}},
        {"motor3","Motor trifásico","simple",{{"U",{-20,-20}},{"V",{0,-20}},{"W",{20,-20}},{"PE",{20,20}}}},
        {"breaker3","Disjuntor tripolar","simple",{{"1",{-20,-20}},{"2",{-20,20}},{"3",{0,-20}},{"4",{0,20}},{"5",{20,-20}},{"6",{20,20}}}},
        {"contactor3","Power contacts contactor","simple",{{"1",{-20,-20}},{"2",{-20,20}},{"3",{0,-20}},{"4",{0,20}},{"5",{20,-20}},{"6",{20,20}}}},
        {"thermal3","Thermal relay","simple",{{"1",{-20,-20}},{"2",{-20,20}},{"3",{0,-20}},{"4",{0,20}},{"5",{20,-20}},{"6",{20,20}}}}};
    QMap<QString,Def> byFile;
    for(const auto &def:definitions) {
        byFile[def.file]=def;auto element=doc.createElement("element");element.setAttribute("name",def.file+".elmt");category.appendChild(element);
        auto d=doc.createElement("definition");d.setAttribute("type","element");d.setAttribute("version","0.100.0");
        d.setAttribute("width",60);d.setAttribute("height",60);d.setAttribute("hotspot_x",30);d.setAttribute("hotspot_y",30);
        d.setAttribute("link_type",def.type);element.appendChild(d);
        auto names=doc.createElement("names");for(const QString &lang:QStringList{"en","pt_BR"}){auto n=doc.createElement("name");n.setAttribute("lang",lang);n.appendChild(doc.createTextNode(def.name));names.appendChild(n);}d.appendChild(names);
        auto description=doc.createElement("description");d.appendChild(description);
        auto rect=doc.createElement("rect");rect.setAttribute("x",-12);rect.setAttribute("y",-10);rect.setAttribute("width",24);rect.setAttribute("height",20);
        rect.setAttribute("style","line-style:normal;line-weight:normal;filling:none;color:black");description.appendChild(rect);
        for(const auto &pin:def.pins) {
            auto t=doc.createElement("terminal");t.setAttribute("name",pin.first);t.setAttribute("uuid",uid("terminal:"+def.file+":"+pin.first));
            t.setAttribute("x",pin.second.x());t.setAttribute("y",pin.second.y());t.setAttribute("orientation",pin.second.y()<0?"n":"s");t.setAttribute("type","Generic");description.appendChild(t);
        }
    }
    QVector<QDomElement> folios;
    for(int i=0;i<2;++i) {
        auto d=doc.createElement("diagram");d.setAttribute("uuid",uid("folio:"+QString::number(i)));d.setAttribute("title",i==0?"Origem e continuações":"Alimentador e duas derivações");
        d.setAttribute("cols",17);d.setAttribute("colsize",60);d.setAttribute("rows",8);d.setAttribute("rowsize",80);d.setAttribute("height",660);d.setAttribute("order",i+1);
        auto es=doc.createElement("elements");auto cs=doc.createElement("conductors");d.appendChild(es);d.appendChild(cs);root.appendChild(d);folios<<d;
    }
    QMap<QString,QString> fileByTag;QMap<QString,QDomElement> placed;
    auto place=[&](int folio,const QString &tag,const QString &file,QPoint pos,const QMap<QString,QString> &info) {
        const auto def=byFile[file];auto e=doc.createElement("element");e.setAttribute("uuid",uid("element:"+tag));e.setAttribute("type","embed://radial/"+file+".elmt");
        e.setAttribute("x",pos.x());e.setAttribute("y",pos.y());e.setAttribute("orientation",0);e.setAttribute("z",10);
        auto terminals=doc.createElement("terminals");for(const auto &p:def.pins){auto t=doc.createElement("terminal");t.setAttribute("id",placed.size()*10+terminals.childNodes().size());t.setAttribute("x",p.second.x());t.setAttribute("y",p.second.y());t.setAttribute("orientation",p.second.y()<0?0:2);terminals.appendChild(t);}e.appendChild(terminals);
        auto properties=doc.createElement("elementInformations");auto all=info;all["label"]=tag;
        for(auto it=all.cbegin();it!=all.cend();++it){auto p=doc.createElement("elementInformation");p.setAttribute("name",it.key());p.setAttribute("show",it.key()=="label"?1:0);p.appendChild(doc.createTextNode(it.value()));properties.appendChild(p);}e.appendChild(properties);
        folios[folio].firstChildElement("elements").appendChild(e);fileByTag[tag]=file;placed[tag]=e;
    };
    place(0,"S0",threePhase?"source3":"source",{160,100},{{"qet_electrical_role","source"},{"qet_voltage_v",threePhase?"380":"230"},{"qet_supply",threePhase?"3F":"1F"}});
    const QStringList roles=threePhase?QStringList{"L1","L2","L3","PE"}:QStringList{"L","N","PE"};
    for(int i=0;i<roles.size();++i) {
        const QString role=roles[i];place(0,"OUT-"+role,"next",{130+i*120,280},{});
        place(1,"IN-"+role,"previous",{100+i*120,80},{});
        auto links=doc.createElement("links_uuids"),link=doc.createElement("link_uuid");link.setAttribute("uuid",uid("element:IN-"+role));links.appendChild(link);placed["OUT-"+role].appendChild(links);
        links=doc.createElement("links_uuids");link=doc.createElement("link_uuid");link.setAttribute("uuid",uid("element:OUT-"+role));links.appendChild(link);placed["IN-"+role].appendChild(links);
    }
    if(!threePhase) {
    place(1,"Q1","breaker",{140,180},{{"qet_electrical_role","protection"}});
    place(1,"Q2","breaker",{440,180},{{"qet_electrical_role","protection"}});
    place(1,"XN","bus-n",{270,260},{{"qet_electrical_role","terminal"}});
    place(1,"XPE","bus-pe",{620,260},{{"qet_electrical_role","terminal"}});
    QMap<QString,QString> info{{"qet_electrical_role","load"},{"qet_voltage_v","230"},{"qet_supply","1F"},{"qet_power_factor","1"},
        {"qet_harmonic3_percent","0"},{"qet_protection_class","I"},{"qet_current_a","8"}};
    place(1,"H1","load",{180,400},info);info["qet_current_a"]="12";place(1,"H2","load",{480,400},info);
    } else {
        place(1,"Q3","breaker3",{180,180},{{"qet_electrical_role","protection"}});
        place(1,"K3","contactor3",{380,180},{{"qet_electrical_role","switch"}});
        place(1,"F3","thermal3",{580,180},{{"qet_electrical_role","switch"}});
        place(1,"M3","motor3",{780,400},{{"qet_electrical_role","load"},{"qet_voltage_v","380"},{"qet_supply","3F"},
            {"qet_current_a","7"},{"qet_power_factor","0.85"},{"qet_harmonic3_percent","0"},{"qet_protection_class","I"},
            {"qet_start_multiple","6"},{"qet_start_seconds","1"},{"qet_start_pf","0.3"}});
        for(int i=0;i<3;++i)place(1,QStringList{"PE-Q","PE-K","PE-F"}[i],"bus-pe",{200+i*200,300},{{"qet_electrical_role","terminal"}});
    }
    place(0,"S24","source-dc",{700,100},{{"qet_electrical_role","source"},{"qet_voltage_v","24"},{"qet_supply","CC"}});
    place(0,"H24","load-dc",{700,260},{{"qet_electrical_role","load"},{"qet_voltage_v","24"},{"qet_supply","CC"},
        {"qet_current_a","0.1"},{"qet_harmonic3_percent","0"},{"qet_protection_class","III"}});
    auto connect=[&](int folio,const QString &name,const QString &a,const QString &pinA,const QString &b,const QString &pinB) {
        auto c=doc.createElement("conductor");c.setAttribute("uuid",uid("wire:"+name));c.setAttribute("element1",uid("element:"+a));c.setAttribute("element2",uid("element:"+b));
        c.setAttribute("terminal1",uid("terminal:"+fileByTag[a]+":"+pinA));c.setAttribute("terminal2",uid("terminal:"+fileByTag[b]+":"+pinB));
        c.setAttribute("terminalname1",pinA);c.setAttribute("terminalname2",pinB);c.setAttribute("type","multi");c.setAttribute("num",name);c.setAttribute("condsize",1);c.setAttribute("numsize",7);c.setAttribute("displaytext",1);
        folios[folio].firstChildElement("conductors").appendChild(c);
    };
    if(!threePhase) {
    connect(0,"F-L-a","S0","L1","OUT-L","");connect(0,"F-N-a","S0","N","OUT-N","");connect(0,"F-PE-a","S0","PE","OUT-PE","");
    connect(1,"F-L-b","IN-L","","Q1","1");connect(1,"F-N-b","IN-N","","XN","N");connect(1,"F-PE-b","IN-PE","","XPE","PE");
    connect(1,"F-L-link","Q1","1","Q2","1");
    connect(1,"A-L","Q1","2","H1","L1");connect(1,"A-N","XN","N","H1","N");connect(1,"A-PE","XPE","PE","H1","PE");
    connect(1,"B-L","Q2","2","H2","L1");connect(1,"B-N","XN","N","H2","N");connect(1,"B-PE","XPE","PE","H2","PE");
    } else {
        for(int i=0;i<3;++i) {
            const QString phase=QStringList{"L1","L2","L3"}[i],in=QString::number(1+2*i),out=QString::number(2+2*i);
            connect(0,"F0-"+phase+"-a","S0",phase,"OUT-"+phase,"");
            connect(1,"F0-"+phase+"-b","IN-"+phase,"","Q3",in);
            connect(1,"F1-"+phase,"Q3",out,"K3",in);connect(1,"F2-"+phase,"K3",out,"F3",in);
            connect(1,"F3-"+phase,"F3",out,"M3",QStringList{"U","V","W"}[i]);
        }
        connect(0,"F0-PE-a","S0","PE","OUT-PE","");connect(1,"F0-PE-b","IN-PE","","PE-Q","PE");
        connect(1,"F1-PE","PE-Q","PE","PE-K","PE");connect(1,"F2-PE","PE-K","PE","PE-F","PE");connect(1,"F3-PE","PE-F","PE","M3","PE");
    }
    connect(0,"24+","S24","+","H24","+");connect(0,"24-","S24","-","H24","-");
    root.appendChild(collection);QFile f(path);return f.open(QIODevice::WriteOnly) && f.write(doc.toByteArray(4))>0;
}
}
#endif
