#include "canon/mdbreader.h"
#include "canon/projection.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
namespace Canon {void reportProjectionCounters();}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;QFile f(QString::fromLocal8Bit(argv[1]));if(!f.open(QIODevice::ReadOnly))return 3;Canon::Cancellation c;const SourceSnapshotRef snapshot=QSharedPointer<SourceSnapshot>::create(SourceSnapshot{MetadataSource::Mdb,f.fileName(),QFileInfo(f).lastModified(),SourceReadState::NotRead});const auto source=Canon::MdbReader{}.read(f,{snapshot,c});const auto projection=Canon::projectOmf(source,c);Canon::reportProjectionCounters();const QJsonObject o{{"path",f.fileName()},{"outcome",int(source.outcome)},{"objects",source.objects.size()},{"relationships",source.relationships.size()},{"files",projection.files.size()},{"masters",projection.masters.size()}};std::puts(QJsonDocument(o).toJson(QJsonDocument::Compact).constData());}
