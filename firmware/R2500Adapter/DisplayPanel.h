#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "BoardProfile.h"
#include "DisplayFrame.h"
#include "DisplayTiles.h"
#include "PanelWindow.h"
#include "RuntimeProfile.h"

// Transport/controller layer. RGB565 framebuffer and layouts stay independent
// of SPI pins, panel RAM offsets, init commands and wire byte order.
class DisplayPanel {
  SPIClass spi{FSPI};
  DisplayTiles<Board::width,Board::height,Board::littleEndian> frame;
  uint16_t *shadow=nullptr;
#if AP_FIRMWARE_PROFILING
  DisplayFrame<Board::width,Board::height,Board::littleEndian> legacyFrame;
  uint16_t *benchmarkShadow=nullptr;
  bool benchmarkPrimed=false;
#endif
  void command(uint8_t c,const uint8_t *data=nullptr,size_t n=0){
    spi.beginTransaction(SPISettings(20000000,MSBFIRST,SPI_MODE0));
    digitalWrite(Board::cs,LOW);digitalWrite(Board::dc,LOW);spi.transfer(c);
    digitalWrite(Board::dc,HIGH);if(n)spi.writeBytes(data,n);
    digitalWrite(Board::cs,HIGH);spi.endTransaction();
  }
  void init(uint8_t c,std::initializer_list<uint8_t> data){command(c,data.begin(),data.size());}
  void writePatch(unsigned left,unsigned first,unsigned width,unsigned count,const uint16_t *pixels,bool legacy=false){
    if(legacy){
      const uint16_t x0=Board::xOffset+left,y0=Board::yOffset+first,x1=x0+width-1,y1=y0+count-1;
      init(0x2a,{uint8_t(x0>>8),uint8_t(x0),uint8_t(x1>>8),uint8_t(x1)});
      init(0x2b,{uint8_t(y0>>8),uint8_t(y0),uint8_t(y1>>8),uint8_t(y1)});command(0x2c);
    }
    spi.beginTransaction(SPISettings(20000000,MSBFIRST,SPI_MODE0));
    digitalWrite(Board::cs,LOW);
    if(!legacy)PanelWindow::address(left,first,width,count,Board::xOffset,Board::yOffset,
      [&](uint8_t c,const uint8_t *data,size_t n){
        digitalWrite(Board::dc,LOW);spi.transfer(c);digitalWrite(Board::dc,HIGH);if(n)spi.writeBytes(data,n);
      });
    digitalWrite(Board::dc,HIGH);
    spi.writeBytes(reinterpret_cast<const uint8_t*>(pixels),width*count*sizeof(uint16_t));
    digitalWrite(Board::cs,HIGH);spi.endTransaction();
  }
  void benchmark(const uint16_t *pixels,unsigned logicalWidth){
#if AP_FIRMWARE_PROFILING
    if(RuntimeProfile::panelRuns>=5||RuntimeProfile::panelAllocationFailed||RuntimeProfile::graphRuns<5||!shadow||!frame.initialized())return;
    constexpr size_t bytes=Board::width*Board::height*sizeof(uint16_t);
    if(!benchmarkShadow)benchmarkShadow=static_cast<uint16_t*>(ps_malloc(bytes));
    if(!benchmarkShadow){RuntimeProfile::panelAllocationFailed=true;return;}
    if(!benchmarkPrimed){legacyFrame.present(pixels,logicalWidth,benchmarkShadow,[](unsigned,unsigned,const uint16_t*){});benchmarkPrimed=true;}
    uint64_t elapsed[2]{};uint32_t hashes[2]{},sentPixels[2]{},rectangles[2]{};
    for(unsigned order=0;order<2;++order){
      const unsigned backend=(order+RuntimeProfile::panelRuns)%2;
      memcpy(benchmarkShadow,shadow,bytes);
      auto probe=frame;
      const uint64_t start=uint64_t(esp_timer_get_time());
      if(!backend)legacyFrame.present(pixels,logicalWidth,benchmarkShadow,[&](unsigned first,unsigned count,const uint16_t *stripe){
        sentPixels[0]+=Board::width*count;++rectangles[0];writePatch(0,first,Board::width,count,stripe,true);
      });
      else {auto result=probe.present(pixels,logicalWidth,benchmarkShadow,[&](unsigned left,unsigned first,unsigned width,unsigned count,const uint16_t *stripe){
        writePatch(left,first,width,count,stripe);
      });sentPixels[1]=result.pixels;rectangles[1]=result.rectangles;}
      elapsed[backend]=uint64_t(esp_timer_get_time())-start;
      hashes[backend]=AllpowersCrc32::compute(benchmarkShadow,bytes);
    }
    RuntimeProfile::panelCompared(elapsed[0],elapsed[1],sentPixels[0],sentPixels[1],rectangles[0],rectangles[1],hashes[0]==hashes[1]);
    if(RuntimeProfile::panelRuns>=5){free(benchmarkShadow);benchmarkShadow=nullptr;}
#endif
  }
  void st7789(){
    command(0x11);delay(120);
    init(0x36,{0});init(0x3a,{5});init(0xb0,{0,0xe8});init(0xb2,{0x0c,0x0c,0,0x33,0x33});
    init(0xb7,{0x35});init(0xbb,{0x35});init(0xc0,{0x2c});init(0xc2,{1});init(0xc3,{0x13});
    init(0xc4,{0x20});init(0xc6,{0x0f});init(0xd0,{0xa4,0xa1});init(0xd6,{0xa1});
    init(0xe0,{0xf0,0,4,4,4,5,0x29,0x33,0x3e,0x38,0x12,0x12,0x28,0x30});
    init(0xe1,{0xf0,7,0x0a,0x0d,0x0b,7,0x28,0x33,0x3e,0x36,0x14,0x14,0x29,0x32});
    command(0x21);command(0x11);delay(120);command(0x29);
  }
  void st7735(){
    command(0x01);delay(150);command(0x11);delay(500);
    init(0xb1,{1,0x2c,0x2d});init(0xb2,{1,0x2c,0x2d});init(0xb3,{1,0x2c,0x2d,1,0x2c,0x2d});
    init(0xb4,{7});init(0xc0,{0xa2,2,0x84});init(0xc1,{0xc5});init(0xc2,{0x0a,0});
    init(0xc3,{0x8a,0x2a});init(0xc4,{0x8a,0xee});init(0xc5,{0x0e});
    init(0x36,{0xcc});init(0x3a,{5}); // MX|MY|MH|BGR, portrait 80x160
    init(0xe0,{2,0x1c,7,0x12,0x37,0x32,0x29,0x2d,0x29,0x25,0x2b,0x39,0,1,3,0x10});
    init(0xe1,{3,0x1d,7,6,0x2e,0x2c,0x29,0x2d,0x2e,0x2e,0x37,0x3f,0,0,2,0x10});
    command(0x21);command(0x13);delay(10);command(0x29);delay(100);
  }
public:
  bool begin(){
    // Optional optimization only: allocation failure retains full-frame writes.
    shadow=static_cast<uint16_t*>(ps_malloc(Board::width*Board::height*sizeof(uint16_t)));
    for(int pin:{Board::cs,Board::dc,Board::reset})pinMode(pin,OUTPUT);
    if constexpr(Board::backlight>=0){pinMode(Board::backlight,OUTPUT);digitalWrite(Board::backlight,!Board::backlightHigh);}
    digitalWrite(Board::cs,HIGH);spi.begin(Board::clock,-1,Board::mosi);
    digitalWrite(Board::reset,LOW);delay(50);digitalWrite(Board::reset,HIGH);delay(50);
    if constexpr(Board::panel==Board::PanelKind::ST7789)st7789();else st7735();
    if constexpr(Board::backlight>=0)digitalWrite(Board::backlight,Board::backlightHigh);
    return true;
  }
  void present(const uint16_t *pixels,int logicalWidth){
    benchmark(pixels,unsigned(logicalWidth));
    RuntimeProfile::Scope profile(RuntimeProfile::Metric::DisplayPresent);
    const auto result=frame.present(pixels,logicalWidth,shadow,[&](unsigned left,unsigned first,unsigned width,unsigned count,const uint16_t *stripe){
      RuntimeProfile::Scope send(RuntimeProfile::Metric::DisplaySend);
      writePatch(left,first,width,count,stripe);
    });
    RuntimeProfile::displayed(result.rows,Board::height,shadow!=nullptr,result.pixels,result.rectangles);
  }
};
