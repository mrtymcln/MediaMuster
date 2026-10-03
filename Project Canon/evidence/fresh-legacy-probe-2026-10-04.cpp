#include "canon/legacyreader.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <algorithm>
#include <cstdio>
struct CountingFile : QFile {
 using QFile::QFile;
 qint64 requested=0, returned=0, calls=0;
 QVector<Canon::ByteRange> reads;
 qint64 readData(char *destination,qint64 amount) override {
  const auto start=pos(); requested+=amount; ++calls;
  const auto got=QFile::readData(destination,amount);
  if(got>0) {returned+=got; reads.append({start,got});}
  return got;
 }
};
qint64 unionSize(QVector<Canon::ByteRange> ranges) {
 std::sort(ranges.begin(),ranges.end(),[](auto a,auto b){return a.offset<b.offset;});
 qint64 sum=0,end=0;
 for(auto r:ranges) {const auto stop=r.offset+r.length;if(stop>end){sum+=stop-std::max(end,r.offset);end=stop;}}
 return sum;
}
struct Stats {qint64 graphs=0,objects=0,properties=0,relationships=0,retained=0,rangeOnly=0;QVector<Canon::ByteRange> ranges,essence,nativeSound;QJsonArray diagnostics;};
void property(Stats &s,const Canon::RawProperty &p){++s.properties;s.retained+=p.encoding.size();
 const QStringList nativeEssence{"OMFI:IDAT:ImageData","OMFI:TIFF:Data","OMFI:TIFF:ImageData","OMFI:AIFC:Data","OMFI:AIFC:AudioData","OMFI:WAVE:Data","OMFI:WAVE:AudioData"};
 if(nativeEssence.contains(p.locator.name)||p.locator.name=="Audio.data")s.essence+=p.locator.ranges;
 if(p.locator.name=="Audio.data")s.nativeSound+=p.locator.ranges;
 if(p.locator.name=="Audio.SSND"&&!p.locator.ranges.isEmpty()){const auto r=p.locator.ranges.front();const auto skip=8+p.decoded.toMap().value("offset").toLongLong();if(skip<=r.length){s.essence.append({r.offset+skip,r.length-skip});s.nativeSound.append({r.offset+skip,r.length-skip});}}
 if(!p.bytesRetained){++s.rangeOnly;s.ranges+=p.locator.ranges;}}
void collect(Stats &s,const Canon::ParsedSource &p){++s.graphs;s.objects+=p.objects.size();s.relationships+=p.relationships.size();for(const auto &d:p.diagnostics)s.diagnostics.append(d);for(const auto &r:p.unownedProperties)property(s,r);for(const auto&o:p.objects)for(const auto&r:o.properties)property(s,r);for(const auto &c:p.embeddedSources)collect(s,c);}
qint64 overlap(const QVector<Canon::ByteRange> &reads,const QVector<Canon::ByteRange> &essence){QVector<Canon::ByteRange> overlaps;for(auto r:reads)for(auto e:essence){const auto start=std::max(r.offset,e.offset),end=std::min(r.offset+r.length,e.offset+e.length);if(start<end)overlaps.append({start,end-start});}return unionSize(overlaps);}
QString outcome(Canon::ParsedSource::Outcome o){switch(o){case Canon::ParsedSource::Outcome::NotRead:return "NotRead";case Canon::ParsedSource::Outcome::Complete:return "Complete";case Canon::ParsedSource::Outcome::Incomplete:return "Incomplete";case Canon::ParsedSource::Outcome::Malformed:return "Malformed";case Canon::ParsedSource::Outcome::Unsupported:return "Unsupported";case Canon::ParsedSource::Outcome::IoError:return "IoError";case Canon::ParsedSource::Outcome::Cancelled:return "Cancelled";}return "Unknown";}
int main(int argc,char **argv){QCoreApplication app(argc,argv);QTextStream out(stdout);Canon::Cancellation cancellation;for(int i=1;i<argc;++i){QString path=QString::fromLocal8Bit(argv[i]);CountingFile file(path);if(!file.open(QIODevice::ReadOnly|QIODevice::Unbuffered)){out<<"{\"openError\":true}\n";continue;}auto receipt=QSharedPointer<SourceSnapshot>::create();receipt->path=path;receipt->source=MetadataSource::Omf;const auto parsed=Canon::LegacyReader{}.read(file,{receipt,cancellation});Stats s;collect(s,parsed);QJsonObject r{{"path",path},{"outcome",outcome(parsed.outcome)},{"container",int(parsed.container)},{"sourceBytes",file.size()},{"graphsIncludingNativeRoot",s.graphs},{"objects",s.objects},{"properties",s.properties},{"relationships",s.relationships},{"retainedEncodingBytes",s.retained},{"rangeOnlyProperties",s.rangeOnly},{"rangeOnlyUniqueReferencedBytes",unionSize(s.ranges)},{"readCalls",file.calls},{"bytesRequested",file.requested},{"bytesReturned",file.returned},{"uniqueSourceBytesRead",unionSize(file.reads)},{"sourceBytesNotRead",file.size()-unionSize(file.reads)},{"declaredMediaDataBytes",unionSize(s.essence)},{"declaredMediaDataBytesRead",overlap(file.reads,s.essence)},{"nativeSoundSampleBytes",unionSize(s.nativeSound)},{"nativeSoundSampleBytesRead",overlap(file.reads,s.nativeSound)},{"diagnostics",s.diagnostics}};out<<QJsonDocument(r).toJson(QJsonDocument::Compact)<<'\n';}return 0;}
