#include "avbparser.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv); QJsonArray result;
 for(int i=1;i<argc;++i){auto bin=AvbParser::parse(QString::fromUtf8(argv[i]));QJsonArray ids;for(const auto&id:bin.mobIds)ids.append(id);
 result.append(QJsonObject{{"path",bin.filePath},{"valid",bin.valid},{"mobIds",ids}});}
 auto output=QJsonDocument(result).toJson(QJsonDocument::Compact);fwrite(output.constData(),1,output.size(),stdout);
}
