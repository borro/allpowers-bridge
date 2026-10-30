#include "history_operation_id.h"
#include <QByteArray>
#include <cstdio>

int main(int argc,char **) {
  if(argc!=1)return 3;
  const QByteArray raw=HistoryOperationIds::random128();
  if(raw.size()!=16)return 4;
  const QByteArray hex=raw.toHex();
  if(std::fwrite(hex.constData(),1,size_t(hex.size()),stdout)!=size_t(hex.size())||
      std::fputc('\n',stdout)==EOF)return 5;
  return 0;
}
