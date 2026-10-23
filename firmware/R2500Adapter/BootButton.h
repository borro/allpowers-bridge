#pragma once
#include <stdint.h>

class BootButton {
  bool raw=false,stable=false,armed=false;
  uint32_t changedAt=0,pressedAt=0;
public:
  void begin(bool pressed,uint32_t now){raw=stable=pressed;armed=!pressed;changedAt=now;pressedAt=now;}
  bool update(bool pressed,uint32_t now){
    if(pressed!=raw){raw=pressed;changedAt=now;}
    if(raw==stable||uint32_t(now-changedAt)<30)return false;
    stable=raw;
    if(stable){pressedAt=now;return false;}
    bool event=armed&&uint32_t(now-pressedAt)>=40&&uint32_t(now-pressedAt)<=1000;
    armed=true;return event;
  }
  static bool selfTest(){
    BootButton b;b.begin(true,0);
    if(b.update(false,100)||b.update(false,140))return false; // Startup hold/release.
    if(b.update(true,200)||b.update(true,240)||b.update(false,300))return false;
    if(!b.update(false,340)||b.update(false,400))return false;
    if(b.update(true,500)||b.update(false,510)||b.update(false,550))return false; // Bounce.
    if(b.update(true,600)||b.update(true,640)||b.update(false,2000)||b.update(false,2040))return false;
    return true;
  }
};
