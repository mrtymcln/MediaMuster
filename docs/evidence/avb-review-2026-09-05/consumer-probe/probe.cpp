#include "avbparser.h"
#include "binfilterdialog.h"
#include "mediafilterproxy.h"
#include "mediatablemodel.h"
#include "mobid.h"
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QTextStream>

const QString base = QStringLiteral("/tmp/mediamuster-avb-review/consumer-probe/");
const QByteArray fileHex = "060a2b340101010501010f10130000004a507dea741106907a361e6a605d3613";
const QByteArray masterHex = "060a2b340101010501010f101300000055555555666666667777777777777777";

QString writeFixture(const QString &name, const QByteArray &body) {
    const QString path=base+name+QStringLiteral(".avb");
    QFile file(path); file.open(QIODevice::WriteOnly);
    file.write(QByteArray::fromHex("0600446f6d61696e444a424f"));
    file.write(body); return path;
}

QListWidget *bins(BinFilterDialog &d) {
    return d.findChildren<QListWidget *>().first();
}
void select(BinFilterDialog &d, int index) {
    auto *list=bins(d);
    for(int row=0;row<list->count();row++) list->item(row)->setCheckState(row==index?Qt::Checked:Qt::Unchecked);
}
void invoke(BinFilterDialog &d, const char *name) { QMetaObject::invokeMethod(&d,name,Qt::DirectConnection); }
void drain() { QCoreApplication::processEvents(); }

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QJsonArray output;
    MediaFile f; f.filePath="one.mxf"; f.mobId=MobId::format(QByteArray::fromHex(fileHex)); f.masterMobId=MobId::format(QByteArray::fromHex(masterHex));
    MediaTableModel model; model.setMediaFiles({f});
    const QString masterOnly=writeFixture("master-only",QByteArray(1,'\0')+masterHex+QByteArray(1,'\0'));
    const QString fileOnly=writeFixture("file-only",QByteArray(1,'\0')+fileHex+QByteArray(1,'\0'));
    const QString both=writeFixture("both",QByteArray(1,'\0')+masterHex+QByteArray(1,'\0')+fileHex+QByteArray(1,'\0'));
    const QString headerOnly=writeFixture("header-only",{});
    for(const auto &p: {headerOnly, base+"empty-real.avb"}) {
        BinFilterDialog d; MediaFilterProxy proxy; proxy.setSourceModel(&model);
        bool active=false; int signalCount=0;
        QObject::connect(&d,&BinFilterDialog::filterChainChanged,&proxy,[&](bool a,const QSet<QString>&m,const QStringList&){active=a; signalCount++;proxy.setBinFilterMobs(a,m);});
        d.addBinFromFile(p); drain(); invoke(d,"onIntersectClicked");
        const auto parsed=AvbParser::parse(p);
        output.append(QJsonObject{{"case",QFileInfo(p).fileName()},{"parserValid",parsed.valid},{"ids",parsed.mobIds.size()},{"loadedBins",bins(d)->count()},{"active",active},{"signals",signalCount},{"visibleRows",proxy.rowCount()}});
    }
    for(const QString &op: {QStringLiteral("intersection"),QStringLiteral("subtraction")}) {
        BinFilterDialog d; MediaFilterProxy proxy; proxy.setSourceModel(&model);
        QObject::connect(&d,&BinFilterDialog::filterChainChanged,&proxy,[&](bool a,const QSet<QString>&m,const QStringList&){proxy.setBinFilterMobs(a,m);});
        d.addBinFromFile(op=="intersection"?masterOnly:both); drain();
        int before=proxy.rowCount();
        d.addBinFromFile(fileOnly); select(d,1);
        invoke(d,op=="intersection"?"onIntersectClicked":"onSubtractClicked");
        output.append(QJsonObject{{"case",op},{"visibleBefore",before},{"visibleAfter",proxy.rowCount()},{"expectedAfter",op=="intersection"?1:0}});
    }
    {
        BinFilterDialog d; MediaFilterProxy proxy; proxy.setSourceModel(&model);
        QObject::connect(&d,&BinFilterDialog::filterChainChanged,&proxy,[&](bool a,const QSet<QString>&m,const QStringList&){proxy.setBinFilterMobs(a,m);});
        d.addBinFromFile(masterOnly); drain(); d.addBinFromFile(fileOnly); select(d,1); invoke(d,"onSubtractClicked");
        QMetaObject::invokeMethod(&d,"onRemoveStep",Qt::DirectConnection,Q_ARG(int,0));
        const int before=proxy.rowCount();
        bins(d)->item(0)->setSelected(true); invoke(d,"onRemoveSelectedBinsClicked");
        output.append(QJsonObject{{"case","leading-subtract-universe-after-removing-first-step"},{"visibleBeforeRemovingLoadedBin",before},{"visibleAfterRemovingLoadedBin",proxy.rowCount()}});
    }
    QTextStream(stdout)<<QJsonDocument(output).toJson();
}
