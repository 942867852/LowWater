#include "src/simcore/simcore.h"
#include <cstdio>
static void hexstr(unsigned long long v, char* out){ unsigned a=(unsigned)(v>>32), b=(unsigned)(v&0xFFFFFFFFu); std::snprintf(out,24,"%08X%08X",a,b); }
int main(){
  simcore::selfCheck();
  simcore::World w; w.init(20250910ULL,120);
  char buf[24]; hexstr((unsigned long long)w.worldHash(0), buf);
  std::printf("ok hash=%s full=%d\n", buf, (int)w.fullCount());
  simcore::EventBus bus; simcore::AttributionLog alog; simcore::WriteQueue wq;
  simcore::Clock c; c.reset(1,0);
  for (int i=0;i<1440;++i) w.tickOnce(c.absTick(), &bus, &alog, &wq), c.advanceMs(1000,[&](long long){});
  hexstr((unsigned long long)w.worldHash(c.absTick()), buf);
  std::printf("day2 hash=%s events=%u links=%u viol=%u unattr=%u full=%d\n", buf,
    (unsigned)bus.eventCount(), (unsigned)alog.size(), (unsigned)alog.violations().size(),
    (unsigned)alog.unattributedCount(), (int)w.fullCount());
  return 0;
}
