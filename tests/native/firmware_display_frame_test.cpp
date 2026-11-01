#include <array>
#include <cassert>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include "DisplayFrame.h"

template <unsigned W, unsigned H, bool Little> static void displayFrames() {
  DisplayFrame<W,H,Little> frame;
  std::vector<uint16_t> pixels(W*H),shadow(W*H,0xdead),panel(W*H,0xbeef),expected(W*H);
  unsigned windows=0;
  for(unsigned i=0;i<W*H;++i)pixels[i]=uint16_t(i*37+11);
  auto present=[&](bool rotated,bool buffered=true){
    windows=0;
    // Independent forward coordinate transform of the logical image.
    const unsigned width=rotated?H:W,height=rotated?W:H;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
      uint16_t c=pixels[y*width+x];if(!Little)c=uint16_t(c<<8|c>>8);
      expected[rotated?x*W+(W-1-y):y*W+x]=c;
    }
    unsigned written=0,end=0;
    const unsigned count=frame.present(pixels.data(),width,buffered?shadow.data():nullptr,
      [&](unsigned first,unsigned rows,const uint16_t *data){
        ++windows;
        assert(rows>0&&rows<=8&&first>=end&&first+rows<=H);end=first+rows;
        for(unsigned i=0;i<rows*W;++i){assert(data[i]==expected[first*W+i]);panel[first*W+i]=data[i];}
        written+=rows;
      });
    assert(count==written&&panel==expected);
    if(buffered)assert(shadow==expected);
    return written;
  };
  assert(present(false)==H);assert(windows==(H+7)/8);assert(present(false)==0);
  pixels[0]^=0x07e0;assert(present(false)==1);
  pixels[(H-1)*W+W-1]=0;assert(present(false)==1);
  for(unsigned y=0;y<9;++y)pixels[y*W]^=0xffff;
  assert(present(false)==9);assert(present(false)==0);
  for(unsigned y=0;y<H;y+=2)pixels[y*W]^=0xf800;
  assert(present(false)==(H+1)/2);
  present(true);assert(present(true)==0);
  pixels[0]^=0xffff;assert(present(true)==1);
  pixels[W*H-1]^=0x1234;assert(present(true)==1);
  present(false);assert(present(false)==0);
  assert(present(false,false)==H);assert(present(false,false)==H);
  assert(present(false)==H);assert(present(false)==0);
  // Sparse changes, clearing and page/rotation transitions must reconstruct the
  // entire correct panel, including stripes that end at the last partial row.
  uint32_t random=12345;
  for(unsigned run=0;run<40;++run){
    for(unsigned i=0;i<run%17;++i){random=random*1664525u+1013904223u;pixels[random%(W*H)]=uint16_t(random>>16);}
    present(run%3==0);assert(present(run%3==0)==0);
  }
}

int main() {
  displayFrames<172,320,true>();
  displayFrames<80,160,false>();
  displayFrames<5,19,true>();
  displayFrames<5,19,false>();
}
