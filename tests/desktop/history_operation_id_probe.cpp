#include "history_operation_id.h"
#include <QByteArray>
#include <cstdio>
#include <cstring>

int main(int argc,char **argv) {
  if(argc==2&&!std::strcmp(argv[1],"--contract")) {
    QByteArray raw(16,'\0');
    raw[0]=char(0x78);raw[1]=char(0x56);raw[2]=char(0x34);raw[3]=char(0x12);
    if(HistoryOperationIds::fallbackRequest(raw)!=0x12345678u)return 1;
    if(HistoryOperationIds::fallbackRequest(QByteArray(16,'\0'))!=0)return 2;
    raw=QByteArray(16,'\0');raw[4]=char(0x21);raw[5]=char(0x43);raw[6]=char(0x65);raw[7]=char(0x87);
    if(HistoryOperationIds::fallbackRequest(raw)!=0x87654321u)return 6;
    raw=QByteArray(16,'\0');raw[12]=char(0xab);
    if(HistoryOperationIds::fallbackRequest(raw)!=0xabu)return 7;
    if(HistoryOperationIds::fallbackRequest(QByteArray(15,'\0'))!=0)return 8;
    return 0;
  }
  if(argc!=1)return 3;
  const QByteArray raw=HistoryOperationIds::random128();
  if(raw.size()!=16)return 4;
  const QByteArray hex=raw.toHex();
  if(std::fwrite(hex.constData(),1,size_t(hex.size()),stdout)!=size_t(hex.size())||
      std::fputc('\n',stdout)==EOF)return 5;
  return 0;
}
