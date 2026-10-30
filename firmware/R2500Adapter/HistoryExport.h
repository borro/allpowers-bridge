#pragma once
#include "HistoryWire.h"
#include "HistoryOperation.h"
namespace HistoryFormat {
enum class ReadState { Record, End, Busy, Error };
struct ExportMetadata { uint32_t session=0,generation=0,count=0,oldest=0,latest=0; };
class FrozenArchive {
  Record *records;uint32_t capacity,count=0;
public:
  FrozenArchive(Record *memory,uint32_t size):records(memory),capacity(size){}
  ReadState freeze(const Record *input,uint32_t head,uint32_t size,uint32_t inputCapacity,
                   uint32_t session,uint32_t generation,ExportMetadata &metadata){
    if((!records&&size)||(!input&&size)||size>capacity||size>inputCapacity)return ReadState::Error;
    for(uint32_t i=0;i<size;++i)records[i]=input[(head+i)%inputCapacity];
    count=size;metadata={session,generation,count,count?records[0].sequence:0,count?records[count-1].sequence:0};
    return ReadState::Record;
  }
  ReadState read(uint32_t ordinal,Record &record)const{
    if(ordinal==count)return ReadState::End;
    if(ordinal>count||!records)return ReadState::Error;
    record=records[ordinal];return ReadState::Record;
  }
};
struct ExportSource {
  virtual ReadState open(ExportMetadata &metadata)=0;
  virtual ReadState read(uint32_t ordinal,Record &record)=0;
  virtual void close()=0;
  virtual ~ExportSource()=default;
};
}
