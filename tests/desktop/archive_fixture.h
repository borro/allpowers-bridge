#pragma once
#include "../../firmware/R2500Adapter/HistoryWire.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>
template<class Channel> QByteArray archiveBytes(const Channel *io, const QVector<HistoryFormat::Record> &records) {
  QByteArray command;
  for (const auto &line : io->writes.split('\n')) if (line.startsWith("HISTORY-EXPORT ")) command = line;
  const auto fields = command.split(' ');
  Q_ASSERT(fields.size() == 3);
  QJsonObject envelope{{"history_export_version",3},{"request",fields[1].toInt()},
    {"operation_nonce",QString::fromLatin1(fields[2])},{"export_session",7},{"generation",10},
    {"count",records.size()},{"oldest",records.isEmpty()?0:double(records.first().sequence)},
    {"latest",records.isEmpty()?0:double(records.last().sequence)}};
  auto line=[](const QJsonObject &o){return QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n';};
  auto begin=envelope;begin["history_export_begin"]=true;QByteArray bytes=line(begin);
  quint32 digest=HistoryFormat::kDigestSeed;int ordinal=0;
  for(auto record:records){
    record.crc=HistoryFormat::crc32(&record,28);char raw[512];
    int n=HistoryFormat::serialize(record,raw,sizeof(raw));
    auto row=QJsonDocument::fromJson(QByteArray(raw,n)).object();
    for(auto it=envelope.begin();it!=envelope.end();++it)row[it.key()]=it.value();
    row["ordinal"]=ordinal++;bytes+=line(row);digest=HistoryFormat::digestRecord(digest,record);
  }
  auto done=envelope;done["history_dump_done"]=true;done["sent"]=records.size();
  done["cursor"]=records.isEmpty()?0:double(records.last().sequence);done["digest"]=double(digest);
  done["status"]="complete";return bytes+line(done);
}
