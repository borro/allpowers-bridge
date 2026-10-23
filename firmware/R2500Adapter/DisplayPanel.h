#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "BoardProfile.h"

// Transport/controller layer. RGB565 framebuffer and layouts stay independent
// of SPI pins, panel RAM offsets, init commands and wire byte order.
class DisplayPanel {
  SPIClass spi{FSPI};
  void command(uint8_t c,const uint8_t *data=nullptr,size_t n=0){
    spi.beginTransaction(SPISettings(20000000,MSBFIRST,SPI_MODE0));
    digitalWrite(Board::cs,LOW);digitalWrite(Board::dc,LOW);spi.transfer(c);
    digitalWrite(Board::dc,HIGH);if(n)spi.writeBytes(data,n);
    digitalWrite(Board::cs,HIGH);spi.endTransaction();
  }
  void init(uint8_t c,std::initializer_list<uint8_t> data){command(c,data.begin(),data.size());}
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
    for(int pin:{Board::cs,Board::dc,Board::reset})pinMode(pin,OUTPUT);
    if constexpr(Board::backlight>=0){pinMode(Board::backlight,OUTPUT);digitalWrite(Board::backlight,!Board::backlightHigh);}
    digitalWrite(Board::cs,HIGH);spi.begin(Board::clock,-1,Board::mosi);
    digitalWrite(Board::reset,LOW);delay(50);digitalWrite(Board::reset,HIGH);delay(50);
    if constexpr(Board::panel==Board::PanelKind::ST7789)st7789();else st7735();
    if constexpr(Board::backlight>=0)digitalWrite(Board::backlight,Board::backlightHigh);
    return true;
  }
  void present(const uint16_t *pixels,int logicalWidth){
    uint16_t x0=Board::xOffset,y0=Board::yOffset,x1=x0+Board::width-1,y1=y0+Board::height-1;
    init(0x2a,{uint8_t(x0>>8),uint8_t(x0),uint8_t(x1>>8),uint8_t(x1)});
    init(0x2b,{uint8_t(y0>>8),uint8_t(y0),uint8_t(y1>>8),uint8_t(y1)});command(0x2c);
    spi.beginTransaction(SPISettings(20000000,MSBFIRST,SPI_MODE0));
    digitalWrite(Board::cs,LOW);digitalWrite(Board::dc,HIGH);
    uint16_t row[Board::width];
    for(int y=0;y<Board::height;++y){
      for(int x=0;x<Board::width;++x){
        uint16_t c=logicalWidth==Board::width?pixels[y*Board::width+x]:pixels[(Board::width-1-x)*Board::height+y];
        row[x]=Board::littleEndian?c:uint16_t(c<<8|c>>8);
      }
      spi.writeBytes(reinterpret_cast<uint8_t*>(row),sizeof(row));
    }
    digitalWrite(Board::cs,HIGH);spi.endTransaction();
  }
};
