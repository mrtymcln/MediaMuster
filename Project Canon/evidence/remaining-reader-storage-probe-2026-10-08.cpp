#include "canon/avbreader.h"
#include "canon/pmrreader.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <mach/mach.h>
#include <cstdio>

qint64 footprint() {
 task_vm_info_data_t info{}; mach_msg_type_number_t count=TASK_VM_INFO_COUNT;
 return task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS?qint64(info.phys_footprint):-1;
}
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 if(argc!=2)return 1;
 const QString path=QString::fromLocal8Bit(argv[1]); const QFileInfo before(path);
 QFile file(path); if(!file.open(QIODevice::ReadOnly))return 2;
 const Canon::Cancellation cancellation;
 const bool pmr=before.suffix().compare(QStringLiteral("pmr"),Qt::CaseInsensitive)==0;
 const auto source=pmr?Canon::PmrReader{}.read(file,{{},cancellation}):Canon::AvbReader{}.read(file,{{},cancellation});
 const qint64 retained=footprint();
 qint64 properties=source.unownedProperties.size(), capacity=source.unownedProperties.capacity();
 for(const auto &object:source.objects) { properties+=object.properties.size(); capacity+=object.properties.capacity(); }
 const QFileInfo after(path);
 const QJsonObject result{{"path",path},{"outcome",int(source.outcome)},{"objects",source.objects.size()},
  {"properties",properties},{"propertyCapacity",capacity},{"sparePropertySlotBytes",(capacity-properties)*qint64(sizeof(Canon::RawProperty))},
  {"nativeRawPropertyBytes",qint64(sizeof(Canon::RawProperty))},{"physicalFootprintBytes",retained},
  {"sizeBytes",before.size()},{"modified",before.lastModified().toString(Qt::ISODateWithMs)},
  {"sourceSizeAndMtimeUnchanged",before.size()==after.size()&&before.lastModified()==after.lastModified()}};
 std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
}
