#pragma once
#include <Arduino.h>
#include "DisplayPanel.h"
#include "Telemetry.h"
#include "StationIdentity.h"
#include "HistoryStore.h"
#include "StationPicker.h"
#include "Diagnostics.h"

// Common drawing/layout layer; physical panel belongs to DisplayPanel.
class Dashboard {
  DisplayPanel panel;
  uint16_t *pixels = nullptr;
  GraphHistory::Point *graphPoints=nullptr;
  int screenWidth=Board::width,screenHeight=Board::height;
  // Compact 5x7 uppercase glyphs, columns from left to right, LSB at top.
  const uint8_t *glyph(char c) {
    static const uint8_t alphabet[][5] = {
      {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
      {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
      {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0,0x41,0x7f,0x41,0},
      {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
      {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
      {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
      {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
      {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
      {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}};
    static const uint8_t digits[][5] = {
      {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x62,0x51,0x49,0x49,0x46},
      {0x22,0x41,0x49,0x49,0x36},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
      {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
      {0x06,0x49,0x49,0x29,0x1e}};
    static const uint8_t empty[5]={0}, dash[5]={8,8,8,8,8}, colon[5]={0,0x36,0x36,0,0},
      percent[5]={0x63,0x13,8,0x64,0x63}, slash[5]={0x20,0x10,8,4,2}, dot[5]={0,0x60,0x60,0,0},
      question[5]={2,1,0x59,9,6};
    if(c>='a'&&c<='z') c-=32;
    if(c>='A'&&c<='Z') return alphabet[c-'A']; if(c>='0'&&c<='9') return digits[c-'0'];
    switch(c) {case '-':return dash;case ':':return colon;case '%':return percent;case '/':return slash;case '.':return dot;case '?':return question;default:return empty;}
  }
public:
  static constexpr uint16_t WHITE=0xffff, DIM=0x8c71, GREEN=0x07e0, CYAN=0x07ff, RED=0xf800, YELLOW=0xffe0;
  bool begin() {
    pixels=(uint16_t*)ps_malloc(Board::width*Board::height*2);if(!pixels)return false;
    graphPoints=static_cast<GraphHistory::Point*>(ps_malloc(GraphHistory::kCapacity*sizeof(GraphHistory::Point)));
    if(!panel.begin())return false;
    screenWidth=Board::compact?Board::height:Board::width;
    screenHeight=Board::compact?Board::width:Board::height;
    clear();centered(20,"ALLPOWERS",1,WHITE);centered(45,"CONNECTING BLE",1,CYAN);show();return true;
  }
  void clear(){if(pixels)memset(pixels,0,Board::width*Board::height*2);}
  void rect(int x,int y,int w,int h,uint16_t color) {
    if(!pixels)return;
    // RAMCTRL 00 E8 selects little-endian RGB565: send native ESP32 uint16_t.
    // Swapping again turns green 07E0 into the red/magenta value E007.
    for(int j=max(0,y);j<min(screenHeight,y+h);++j)for(int i=max(0,x);i<min(screenWidth,x+w);++i)pixels[j*screenWidth+i]=color;
  }
  void text(int x,int y,const char *str,int scale,uint16_t color) {
    for(;*str;++str,x+=6*scale) {const uint8_t *g=glyph(*str);for(int col=0;col<5;++col)for(int row=0;row<7;++row)if(g[col]&(1<<row))rect(x+col*scale,y+row*scale,scale,scale,color);}
  }
  int textWidth(const char *str,int scale) {return str[0]?(strlen(str)*6-1)*scale:0;}
  void centered(int y,const char *str,int scale,uint16_t color) {text((screenWidth-textWidth(str,scale))/2,y,str,scale,color);}
  void rightAligned(int y,const char *str,int scale,uint16_t color) {text(screenWidth-8-textWidth(str,scale),y,str,scale,color);}
  void line(int x0,int y0,int x1,int y1,uint16_t color){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,error=dx+dy;
    for(;;){rect(x0,y0,1,1,color);if(x0==x1&&y0==y1)break;int twice=2*error;
      if(twice>=dy){error+=dy;x0+=sx;}if(twice<=dx){error+=dx;y0+=sy;}}
  }
  void show(){if(pixels)panel.present(pixels,screenWidth);}
  void render(const Telemetry &s,const StationIdentity &identity,bool connected,uint32_t now) {
    if(!pixels)return;if constexpr(Board::compact){renderCompact(s,identity,connected,now);return;}screenWidth=172;screenHeight=320;clear();bool fresh=telemetryFresh(s,now);char line[40];
    char heading[28];strlcpy(heading,identity.name[0]?identity.name:"SEARCHING",sizeof(heading));
    centered(10,heading,textWidth(heading,2)<=156?2:1,connected?WHITE:DIM);
    snprintf(line,sizeof(line),s.mainSeen?"%u%%":"---",s.soc);centered(42,line,6,fresh?WHITE:DIM);
    centered(101,fresh?(s.source==PowerSource::Mains?"AC IN":s.source==PowerSource::Battery?"BATTERY":"SOURCE ?"):"NO DATA",3,fresh?(s.source==PowerSource::Mains?GREEN:s.source==PowerSource::Battery?YELLOW:CYAN):RED);
    text(8,142,s.protocol==uint8_t(AllpowersModels::Model::R2500V2)?"CHG":"IN",2,fresh?CYAN:DIM);
    snprintf(line,sizeof(line),s.mainSeen?"%uW":"--",s.chargeWatts);rightAligned(138,line,3,fresh?CYAN:DIM);
    text(8,178,"OUT",2,fresh?WHITE:DIM);
    snprintf(line,sizeof(line),s.mainSeen?"%uW":"--",s.outputWatts);rightAligned(174,line,3,fresh?WHITE:DIM);
    if(!s.mainSeen)snprintf(line,sizeof(line),"--");
    else if(s.minutes>=1440)snprintf(line,sizeof(line),"%uD %uH",s.minutes/1440,(s.minutes%1440)/60);
    else snprintf(line,sizeof(line),"%uH %uM",s.minutes/60,s.minutes%60);
    centered(212,line,3,fresh?WHITE:DIM);
    const char *labels[]={"AC","USB","DC"};bool on[]={bool(s.mainFlags&2),bool(s.mainFlags&1),bool(s.settingsFlags&0x10)};
    const uint32_t knownBits[]={AllpowersModels::AcOutput,AllpowersModels::UsbOutput,AllpowersModels::DcOutput};
    const int centers[]={25,86,147};
    for(int i=0;i<3;++i){
      bool known=fresh&&(s.known&knownBits[i]);uint16_t color=known&&on[i]?GREEN:DIM;
      const char *status=known?(on[i]?"ON":"OFF"):"?";
      text(centers[i]-textWidth(labels[i],3)/2,255,labels[i],3,color);
      text(centers[i]-textWidth(status,2)/2,287,status,2,color);
    }
    text(146,308,"1/3",1,DIM);
    show();
  }
  void renderHistory(HistoryStore &history,const Telemetry &current){
    if(!pixels)return;if constexpr(Board::compact){renderHistoryCompact(history,current);return;}screenWidth=320;screenHeight=172;clear();char label[40];auto info=history.status();
    text(300,158,"2/3",1,DIM);
    if(!info.ready||!graphPoints){
      centered(60,info.error&&!strcmp(info.error,"initializing")?"STARTING":"LOG ERROR",2,RED);
      if(info.error)centered(100,info.error,1,DIM);show();return;
    }
    bool unknownTime=false;size_t n=history.copyGraph(graphPoints,GraphHistory::kCapacity,unknownTime);
    if(!n){centered(70,"NO HISTORY",2,DIM);show();return;}
    bool fresh=telemetryFresh(current,millis());
    snprintf(label,sizeof(label),current.mainSeen?"%u%%":"--",current.soc);text(8,6,label,2,fresh?GREEN:DIM);
    snprintf(label,sizeof(label),fresh?"OUT %uW":"NO DATA",current.outputWatts);text(86,6,label,textWidth(label,2)>112?1:2,fresh?WHITE:DIM);
    if(fresh)snprintf(label,sizeof(label),current.protocol==uint8_t(AllpowersModels::Model::R2500V2)?"CHG %uW":"IN %uW",current.chargeWatts);
    else strlcpy(label,"IN --",sizeof(label));
    text(200,6,label,textWidth(label,2)>112?1:2,fresh?CYAN:DIM);
    // Fixed log-age axis expands recent seconds while retaining older minutes.
    // ON TIME excludes unknown downtime rather than inventing timestamps.
    for(int y:{40,92,144})line(32,y,280,y,DIM);
    text(8,27,"%",1,GREEN);text(286,27,"W",1,WHITE);
    line(94,26,94,34,RED);text(100,27,"BATT",1,RED);
    line(165,26,165,34,GREEN);text(171,27,"AC IN",1,GREEN);
    text(8,37,"100",1,GREEN);text(14,89,"50",1,GREEN);text(20,141,"0",1,GREEN);
    text(0,158,unknownTime?"ON TIME":"AGO",1,DIM);
    const uint32_t ticks[]={86400,3600,600,60,10,0};
    const char *tickLabels[]={"1D","1H","10M","1M","10S","NOW"};
    for(int i=0;i<6;++i){int x=GraphHistory::x(ticks[i]);
      line(x,40,x,146,DIM);text(x-textWidth(tickLabels[i],1)/2,150,tickLabels[i],1,DIM);
    }
    uint32_t maximum=100;
    for(size_t i=0;i<n;++i)if(graphPoints[i].age<=GraphHistory::kHorizon&&(graphPoints[i].sample.flags&HistoryFormat::Fresh)){
      maximum=max(maximum,uint32_t(max(graphPoints[i].sample.chargeWatts,graphPoints[i].sample.outputWatts)));
    }
    maximum=((maximum*115/100+49)/50)*50;
    snprintf(label,sizeof(label),"%lu",(unsigned long)maximum);text(286,37,label,1,WHITE);
    snprintf(label,sizeof(label),"%lu",(unsigned long)(maximum/2));text(286,89,label,1,WHITE);
    text(286,141,"0",1,WHITE);
    bool previous=false;int px=0,pySoc=0,pyOut=0,pyCharge=0;
    for(size_t i=0;i<n;++i){const auto &point=graphPoints[i];const auto &r=point.sample;
      if(point.age>GraphHistory::kHorizon||!(r.flags&HistoryFormat::Fresh)){previous=false;continue;}
      int x=GraphHistory::x(point.age);
      if(point.unknownBefore){for(int y=41;y<144;y+=5)line(x,y,x,y+1,YELLOW);}
      int ys=144-int(uint32_t(r.soc)*104/100),yo=144-int(uint32_t(r.outputWatts)*104/maximum),yc=144-int(uint32_t(r.chargeWatts)*104/maximum);
      bool continuous=previous&&point.joinPrevious;
      if(continuous){line(px,pySoc,x,ys,GREEN);line(px,pyOut,x,yo,WHITE);line(px,pyCharge,x,yc,CYAN);}
      else {rect(x-1,ys-1,3,3,GREEN);rect(x-1,yo-1,3,3,WHITE);rect(x-1,yc-1,3,3,CYAN);}
      px=x;pySoc=ys;pyOut=yo;pyCharge=yc;previous=true;
    }
    // Overlay source transitions at the first observed sample of the new state.
    // A reboot, stale interval or unknown source never fabricates an event.
    for(size_t i=1;i<n;++i){const auto &point=graphPoints[i];
      if(point.age>GraphHistory::kHorizon)continue;
      auto change=GraphHistory::sourceChange(graphPoints[i-1].sample,point.sample);
      if(change!=GraphHistory::SourceChange::None){int x=GraphHistory::x(point.age);
        line(x,40,x,144,change==GraphHistory::SourceChange::ToBattery?RED:GREEN);
      }
    }
    show();
  }
  void renderDiagnostics(const DiagnosticData &data,uint8_t part=0){
    if(!pixels)return;if constexpr(Board::compact){renderDiagnosticsCompact(data,part);return;}screenWidth=320;screenHeight=172;clear();auto view=diagnosticView(data);
    centered(6,view.heading,2,WHITE);line(158,26,158,148,DIM);
    for(unsigned i=0;i<DiagnosticView::Count;++i){auto &r=view.rows[i];int x=i<DiagnosticView::Rows?8:166,y=29+(i%DiagnosticView::Rows)*9;
      uint16_t color=r.tone==DiagnosticTone::Good?GREEN:r.tone==DiagnosticTone::Warning?YELLOW:r.tone==DiagnosticTone::Bad?RED:r.tone==DiagnosticTone::Dim?DIM:WHITE;
      text(x,y,r.label,1,DIM);text(x+(strlen(r.label)+1)*6,y,r.value,1,color);
    }
    text(8,158,view.footer,1,DIM);text(300,158,"3/3",1,DIM);show();
  }
  void renderPicker(const StationPicker::Snapshot &view,bool back,bool scanning,const char *state,const StationIdentity &connected){
    if(!pixels)return;if constexpr(Board::compact){renderPickerCompact(view,back,scanning,state,connected);return;}screenWidth=172;screenHeight=320;clear();char label[40];
    centered(9,"BLUETOOTH",2,WHITE);centered(31,scanning?"SCANNING":"NEARBY DEVICES",1,CYAN);
    unsigned total=view.total+1;unsigned selected=back?view.total:view.selectedIndex<0?0:unsigned(view.selectedIndex);
    snprintf(label,sizeof(label),"%u/%u",selected+1,total);centered(47,label,1,DIM);
    for(int row=0;row<3;++row){unsigned index=view.offset+row;int y=65+row*66;if(index>view.total)break;
      bool chosen=index==selected;
      if(chosen){rect(3,y-4,166,1,GREEN);rect(3,y+56,166,1,GREEN);rect(3,y-4,1,60,GREEN);rect(168,y-4,1,60,GREEN);}
      if(index==view.total){text(10,y+12,"BACK",2,chosen?GREEN:DIM);continue;}
      if(row>=view.count)break;const auto &e=view.entries[row];
      char shortName[25];strlcpy(shortName,e.identity.name,sizeof(shortName));
      text(9,y,shortName,textWidth(shortName,2)<=154?2:1,chosen?GREEN:WHITE);
      text(9,y+20,e.identity.address,1,DIM);
      bool active=StationPicker::sameMAC(e.identity.address,connected.address);
      snprintf(label,sizeof(label),"%d DB %s",e.rssi,active?"CONNECTED":e.connectable?"":"NO CONNECT");
      text(9,y+36,label,1,active?GREEN:DIM);
    }
    centered(270,state,1,DIM);centered(289,"1 NEXT  2 SELECT",1,WHITE);centered(307,"HOLD SELECT",1,DIM);show();
  }
private:
  void compactStart(){screenWidth=Board::height;screenHeight=Board::width;clear();}
  void pageNumber(unsigned page){char b[8];snprintf(b,sizeof(b),"%u/%u",page,Board::screenCount);text(142,72,b,1,DIM);}
  void renderCompact(const Telemetry &s,const StationIdentity &id,bool connected,uint32_t now){
    compactStart();bool fresh=telemetryFresh(s,now);char b[32],name[27];
    strlcpy(name,id.name[0]?id.name:"SEARCHING",sizeof(name));text(2,2,name,1,connected?WHITE:DIM);
    snprintf(b,sizeof(b),s.mainSeen?"%u%%":"--",s.soc);text(2,17,b,3,fresh?WHITE:DIM);
    const char *source=fresh?(s.source==PowerSource::Mains?"AC IN":s.source==PowerSource::Battery?"BATTERY":"SOURCE ?"):"NO DATA";
    text(98,17,source,1,fresh?(s.source==PowerSource::Mains?GREEN:s.source==PowerSource::Battery?YELLOW:CYAN):RED);
    if(!s.mainSeen)strlcpy(b,"--",sizeof(b));
    else if(s.minutes>=1440)snprintf(b,sizeof(b),"%uD %uH",s.minutes/1440,s.minutes%1440/60);
    else snprintf(b,sizeof(b),"%uH %uM",s.minutes/60,s.minutes%60);
    text(98,31,b,1,fresh?WHITE:DIM);
    snprintf(b,sizeof(b),s.mainSeen?"OUT %uW":"OUT --",s.outputWatts);text(2,47,b,1,fresh?WHITE:DIM);
    snprintf(b,sizeof(b),s.mainSeen?"IN %uW":"IN --",s.chargeWatts);text(98,47,b,1,fresh?CYAN:DIM);
    const char *labels[]={"AC","USB","DC"};const uint32_t bits[]={AllpowersModels::AcOutput,AllpowersModels::UsbOutput,AllpowersModels::DcOutput};
    bool on[]={bool(s.mainFlags&2),bool(s.mainFlags&1),bool(s.settingsFlags&0x10)};
    for(int i=0;i<3;++i){bool known=fresh&&(s.known&bits[i]);snprintf(b,sizeof(b),"%s %s",labels[i],known?(on[i]?"ON":"OFF"):"?");text(2+54*i,61,b,1,known&&on[i]?GREEN:DIM);}
    pageNumber(1);show();
  }
  void renderHistoryCompact(HistoryStore &history,const Telemetry &current){
    compactStart();char b[28];auto info=history.status();
    if(!info.ready||!graphPoints){centered(32,"LOG STARTING",1,DIM);show();return;}
    bool unknown=false;size_t n=history.copyGraph(graphPoints,GraphHistory::kCapacity,unknown);
    snprintf(b,sizeof(b),current.mainSeen?"%u%%":"--",current.soc);text(2,2,b,1,GREEN);
    snprintf(b,sizeof(b),"OUT %uW",current.outputWatts);text(38,2,b,1,WHITE);
    snprintf(b,sizeof(b),"IN %uW",current.chargeWatts);text(112,2,b,1,CYAN);
    const int left=22,right=132,top=23,bottom=61;
    auto gx=[&](uint32_t age){return left+(GraphHistory::x(age)-32)*(right-left)/248;};
    uint32_t maximum=100;
    for(size_t i=0;i<n;++i)if(graphPoints[i].age<=GraphHistory::kHorizon&&(graphPoints[i].sample.flags&HistoryFormat::Fresh))
      maximum=max(maximum,uint32_t(max(graphPoints[i].sample.outputWatts,graphPoints[i].sample.chargeWatts)));
    maximum=((maximum*115/100+49)/50)*50;
    text(2,13,unknown?"ON TIME":"AGO",1,DIM);snprintf(b,sizeof(b),"%luW",(unsigned long)maximum);text(160-textWidth(b,1),13,b,1,WHITE);
    for(int y:{top,(top+bottom)/2,bottom})line(left,y,right,y,DIM);
    text(2,top,"100",1,GREEN);text(14,bottom-6,"0",1,GREEN);
    const uint32_t ticks[]={86400,600,10,0};const char *labels[]={"1D","10M","10S","NOW"};
    for(int i=0;i<4;++i){int x=gx(ticks[i]);line(x,top,x,bottom,DIM);text(x-textWidth(labels[i],1)/2,64,labels[i],1,DIM);}
    bool previous=false;int px=0,ps=0,po=0,pc=0;
    for(size_t i=0;i<n;++i){auto &p=graphPoints[i];auto &r=p.sample;
      if(p.age>GraphHistory::kHorizon||!(r.flags&HistoryFormat::Fresh)){previous=false;continue;}
      int x=gx(p.age),ys=bottom-int(uint32_t(r.soc)*(bottom-top)/100),yo=bottom-int(uint32_t(r.outputWatts)*(bottom-top)/maximum),yc=bottom-int(uint32_t(r.chargeWatts)*(bottom-top)/maximum);
      if(previous&&p.joinPrevious){line(px,ps,x,ys,GREEN);line(px,po,x,yo,WHITE);line(px,pc,x,yc,CYAN);}
      else {rect(x,ys,1,1,GREEN);rect(x,yo,1,1,WHITE);rect(x,yc,1,1,CYAN);}
      if(p.unknownBefore)for(int y=top;y<bottom;y+=4)rect(x,y,1,2,YELLOW);
      px=x;ps=ys;po=yo;pc=yc;previous=true;
    }
    for(size_t i=1;i<n;++i)if(graphPoints[i].age<=GraphHistory::kHorizon){auto event=GraphHistory::sourceChange(graphPoints[i-1].sample,graphPoints[i].sample);
      if(event!=GraphHistory::SourceChange::None){int x=gx(graphPoints[i].age);line(x,top,x,bottom,event==GraphHistory::SourceChange::ToBattery?RED:GREEN);}}
    pageNumber(2);show();
  }
  void renderDiagnosticsCompact(const DiagnosticData &data,uint8_t part){
    compactStart();auto view=diagnosticView(data);char b[24];
    snprintf(b,sizeof(b),"DIAG %c %u/4",data.version,part+1);text(2,1,b,1,WHITE);
    for(unsigned row=0;row<7;++row){unsigned i=part*7+row;if(i>=DiagnosticView::Count)break;auto &r=view.rows[i];
      uint16_t color=r.tone==DiagnosticTone::Good?GREEN:r.tone==DiagnosticTone::Warning?YELLOW:r.tone==DiagnosticTone::Bad?RED:r.tone==DiagnosticTone::Dim?DIM:WHITE;
      text(2,12+row*8,r.label,1,DIM);text(2+(strlen(r.label)+1)*6,12+row*8,r.value,1,color);
    }
    pageNumber(3+part);show();
  }
  void renderPickerCompact(const StationPicker::Snapshot &v,bool back,bool scanning,const char *state,const StationIdentity &connected){
    compactStart();char b[28];text(2,1,scanning?"BLE SCANNING":"BLUETOOTH",1,CYAN);
    if(back){text(8,24,"BACK",2,GREEN);}
    else {int at=v.selectedIndex-int(v.offset);
      if(at>=0&&at<v.count){auto &e=v.entries[at];char name[27];strlcpy(name,e.identity.name,sizeof(name));text(2,16,name,1,GREEN);text(2,29,e.identity.address,1,DIM);
        snprintf(b,sizeof(b),"%d DB %s",e.rssi,StationPicker::sameMAC(e.identity.address,connected.address)?"CONNECTED":"");text(2,41,b,1,DIM);}
      else text(2,25,"NO DEVICES",1,DIM);
    }
    text(2,53,state,1,DIM);text(2,69,"1 NEXT  2/HOLD SELECT",1,WHITE);show();
  }
};
