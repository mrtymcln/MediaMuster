#include "canon/avbobjects_p.h"
#include <QFile>
#include <QMap>
#include <QtEndian>
#include <QTextStream>
#include <algorithm>
using namespace Canon;
int main(int argc,char**argv)
{
 QTextStream out(stdout);
 for(int arg=1;arg<argc;++arg)
 {
  QFile f(QString::fromLocal8Bit(argv[arg]));if(!f.open(QIODevice::ReadOnly))return 2;
  const auto bytes=f.readAll();qint64 pos=119;quint64 index=0;QMap<QByteArray,int> counts,failures;Cancellation cancel;
  while(pos+8<=bytes.size())
  {
   ++index;auto id=bytes.mid(pos,4);std::reverse(id.begin(),id.end());auto size=qFromLittleEndian<quint32>(bytes.constData()+pos+4);pos+=8;
   if(size>bytes.size()-pos)return 3;
   ParsedSource source;AvidObject object;object.handle=index;
   Detail::AvbCursor c(QByteArrayView(bytes).sliced(pos,size),pos,false,source,&object,cancel);
   try
   {
    if(Detail::readAvbDescriptor(c,id))
    {
     ++counts[id];
     if(c.remaining()){++failures[id];out<<id<<" object "<<index<<" unread "<<c.remaining()<<"\n";}
    }
   }
   catch(const Detail::AvbFailure&e){++failures[id];out<<id<<" object "<<index<<" "<<e.explanation<<"\n";}
   pos+=size;
  }
  out<<f.fileName()<<"\n";
  for(auto it=counts.begin();it!=counts.end();++it)out<<it.key()<<" "<<it.value()<<"\n";
  out<<"failure classes "<<failures.size()<<"\n";
 }
}
