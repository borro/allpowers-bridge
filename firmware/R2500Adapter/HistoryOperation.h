#pragma once
#include <stddef.h>
#include <stdint.h>
namespace HistoryFormat {
constexpr uint32_t kHistoryNonceBits=128;
constexpr size_t kHistoryNonceHexLength=32,kHistoryCommandCapacity=80;
inline bool validOperationNonce(const char *value,size_t length){
  if(!value||length!=kHistoryNonceHexLength)return false;
  for(size_t i=0;i<length;++i)
    if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')))return false;
  return true;
}
inline size_t historyCommandLength(const char *line){
  if(!line)return kHistoryCommandCapacity;
  size_t length=0;while(length<kHistoryCommandCapacity&&line[length])++length;return length;
}
// The real CDC collector and host tests share the same bounded line grammar.
// Overflow discards the entire remainder, never a suffix that looks like a new
// command. The returned line is borrowed until the next feed().
class HistoryCommandLine {
  char bytes[kHistoryCommandCapacity]{};size_t used=0;bool dropping=false;
public:
  const char *feed(char byte){
    if(byte=='\r')return nullptr;
    if(byte=='\n'){
      if(dropping){dropping=false;used=0;bytes[0]=0;return nullptr;}
      bytes[used]=0;const bool present=used!=0;used=0;return present?bytes:nullptr;
    }
    if(byte=='\0'){dropping=true;used=0;return nullptr;}
    if(dropping)return nullptr;
    if(used==sizeof(bytes)-1){dropping=true;used=0;return nullptr;}
    bytes[used++]=byte;return nullptr;
  }
  bool discarding()const{return dropping;}
};
}
