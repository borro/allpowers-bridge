#pragma once
#include <math.h>
#include "HistoryRing.h"

namespace GraphHistory {
constexpr uint32_t kLiveKeep=3600,kCapacity=HistoryFormat::kKeep+kLiveKeep,kHorizon=48*60*60;
struct Point {HistoryFormat::Record sample{};uint32_t age=0;bool joinPrevious=false,unknownBefore=false;};
// log(1 + age) gives a defined position for "now" and expands recent seconds.
inline int x(uint32_t age){
  if(age>=kHorizon)return 32;
  return 280-int(lroundf(248.0f*log1pf(float(age))/log1pf(float(kHorizon))));
}
inline bool join(const HistoryFormat::Record &a,const HistoryFormat::Record &b){
  uint32_t limit=!a.sequence&&!b.sequence?2:90;
  return (a.flags&HistoryFormat::Fresh)&&(b.flags&HistoryFormat::Fresh)&&!(b.flags&HistoryFormat::Gap)&&
    a.session==b.session&&b.uptimeSeconds>=a.uptimeSeconds&&b.uptimeSeconds-a.uptimeSeconds<=limit;
}
enum class SourceChange:uint8_t {None,ToBattery,ToMains};
inline SourceChange sourceChange(const HistoryFormat::Record &a,const HistoryFormat::Record &b){
  if(!join(a,b))return SourceChange::None;
  const uint8_t mask=HistoryFormat::Mains|HistoryFormat::Battery;
  uint8_t from=a.flags&mask,to=b.flags&mask;
  if(from==HistoryFormat::Mains&&to==HistoryFormat::Battery)return SourceChange::ToBattery;
  if(from==HistoryFormat::Battery&&to==HistoryFormat::Mains)return SourceChange::ToMains;
  return SourceChange::None;
}
inline bool assignAges(Point *p,size_t n,uint32_t now,uint32_t session){
  if(!n)return false;
  for(size_t i=0;i<n;++i){p[i].unknownBefore=false;p[i].joinPrevious=false;}
  bool unknown=p[n-1].sample.session!=session;
  p[n-1].age=!unknown&&now>=p[n-1].sample.uptimeSeconds?now-p[n-1].sample.uptimeSeconds:now;
  p[n-1].unknownBefore=unknown;
  for(size_t i=n-1;i>0;--i){
    auto &a=p[i-1].sample;auto &b=p[i].sample;uint32_t delta=0;
    if(a.session==b.session&&b.uptimeSeconds>=a.uptimeSeconds)delta=b.uptimeSeconds-a.uptimeSeconds;
    else if((a.flags&HistoryFormat::Clock)&&(b.flags&HistoryFormat::Clock)&&b.epochSeconds>=a.epochSeconds)
      delta=b.epochSeconds-a.epochSeconds;
    else {delta=b.uptimeSeconds;p[i].unknownBefore=true;unknown=true;}
    // Unknown downtime adds no invented interval. The footer marks ON TIME,
    // and a visible separator replaces any connection across that boundary.
    uint64_t age=uint64_t(p[i].age)+delta;
    p[i-1].age=age>kHorizon?kHorizon+1:uint32_t(age);
    p[i].joinPrevious=join(a,b);
  }
  p[0].joinPrevious=false;return unknown;
}
class LiveRing {
  HistoryFormat::Record *records;uint32_t head=0,count=0;
public:
  explicit LiveRing(HistoryFormat::Record *p):records(p){}
  uint32_t size()const{return count;}
  const HistoryFormat::Record &at(uint32_t i)const{return records[(head+i)%kLiveKeep];}
  void push(const HistoryFormat::Record &r){
    if(count<kLiveKeep)records[(head+count++)%kLiveKeep]=r;
    else {records[head]=r;head=(head+1)%kLiveKeep;}
  }
};
// Same implementation runs on the board and in the host C++ check.
inline bool selfTest(HistoryFormat::Record *memory){
  if(x(0)!=280||x(kHorizon)!=32||x(1)<=x(10)||x(10)<=x(60))return false;
  for(uint32_t age=1;age<=kHorizon;age+=337)if(x(age)>x(age-1)||x(age)<32||x(age)>280)return false;
  LiveRing live(memory);auto r=HistoryFormat::example();
  for(uint32_t i=0;i<kLiveKeep+5;++i){r.uptimeSeconds=i;live.push(r);}
  if(live.size()!=kLiveKeep||live.at(0).uptimeSeconds!=5||live.at(kLiveKeep-1).uptimeSeconds!=kLiveKeep+4)return false;
  Point p[3]={};
  for(int i=0;i<3;++i){p[i].sample=HistoryFormat::example();p[i].sample.sequence=i+1;p[i].sample.uptimeSeconds=uint32_t(i*60);}
  if(assignAges(p,3,125,1)||p[0].age!=125||p[1].age!=65||p[2].age!=5||!p[1].joinPrevious)return false;
  p[2].sample.session=2;p[2].sample.uptimeSeconds=10;
  if(!assignAges(p,3,15,2)||p[1].age!=15||!p[2].unknownBefore||p[2].joinPrevious)return false;
  for(auto &v:p){v.unknownBefore=false;v.sample.flags|=HistoryFormat::Clock;}
  p[1].sample.epochSeconds=1000;p[0].sample.epochSeconds=940;p[2].sample.epochSeconds=1300;
  if(assignAges(p,3,15,2)||p[1].age!=305||p[0].age!=365||p[2].joinPrevious)return false;
  r=HistoryFormat::example();auto b=r;b.uptimeSeconds=3;
  if(join(r,b))return false;b.uptimeSeconds=1;if(!join(r,b))return false;
  b.flags|=HistoryFormat::Gap;if(join(r,b))return false;
  b.flags=0;if(join(r,b))return false;
  // Only a contiguous pair with two unambiguous sources marks a transition.
  r=HistoryFormat::example();r.sequence=1;b=r;b.sequence=2;b.uptimeSeconds=60;
  b.flags=(b.flags&~HistoryFormat::Mains)|HistoryFormat::Battery;
  if(sourceChange(r,b)!=SourceChange::ToBattery)return false;
  auto c=b;c.sequence=3;c.uptimeSeconds=120;c.flags=(c.flags&~HistoryFormat::Battery)|HistoryFormat::Mains;
  if(sourceChange(b,c)!=SourceChange::ToMains||sourceChange(r,c)!=SourceChange::None)return false;
  c=b;c.session++;if(sourceChange(r,c)!=SourceChange::None)return false;
  c=b;c.flags|=HistoryFormat::Gap;if(sourceChange(r,c)!=SourceChange::None)return false;
  c=b;c.flags&=~HistoryFormat::Fresh;if(sourceChange(r,c)!=SourceChange::None)return false;
  c=b;c.flags|=HistoryFormat::Mains;if(sourceChange(r,c)!=SourceChange::None)return false;
  c=b;c.flags&=~HistoryFormat::Battery;if(sourceChange(r,c)!=SourceChange::None)return false;
  c=b;c.uptimeSeconds=91;if(sourceChange(r,c)!=SourceChange::None)return false;
  c=r;c.uptimeSeconds=60;if(sourceChange(r,c)!=SourceChange::None)return false;
  r.sequence=b.sequence=0;b.uptimeSeconds=1;
  if(sourceChange(r,b)!=SourceChange::ToBattery)return false;
  b.uptimeSeconds=3;return sourceChange(r,b)==SourceChange::None;
}
}
