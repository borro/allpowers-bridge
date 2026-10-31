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
}
