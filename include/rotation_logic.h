#pragma once
#include <stdint.h>
#include <vector>
#include <algorithm>

namespace rotation {
constexpr uint32_t intervals[]={0,300,900,1800,3600,7200,21600,43200,86400};
constexpr const char *labels[]={"Off","5 minutes","15 minutes","30 minutes","1 hour","2 hours","6 hours","12 hours","24 hours"};
constexpr int intervalCount=sizeof(intervals)/sizeof(intervals[0]);
struct Settings {
    uint8_t face=0,group=0;bool faceShuffle=false,groupShuffle=false;
    uint32_t packed()const{return face|(uint32_t(group)<<8)|(uint32_t(faceShuffle)<<16)|(uint32_t(groupShuffle)<<17);}
    static Settings unpack(uint32_t n){Settings s;s.face=n&255;s.group=(n>>8)&255;s.faceShuffle=n&(1u<<16);s.groupShuffle=n&(1u<<17);if(s.face>=intervalCount)s.face=0;if(s.group>=intervalCount)s.group=0;return s;}
};

// Wall-clock slots are anchored at local midnight. The high-water mark prevents
// duplicate slots in a repeated DST hour. Discontinuous clocks are rebased without
// replaying missed boundaries. A blocked UI coalesces ticks into one pending step.
struct Boundary {
    bool initialized=false,pending=false,syncPending=false,fold=false;
    int64_t lastUtc=0,lastWall=0,high=0;
    uint32_t period=0;
    void reset(){initialized=false;pending=false;syncPending=false;fold=false;}
    void synchronize(){syncPending=true;pending=false;}
    bool update(int64_t utc,int64_t wall,uint32_t seconds,bool valid,bool blocked){
        if(!valid||!seconds){reset();period=seconds;return false;}
        const int64_t slot=wall/seconds;
        if(!initialized||period!=seconds){initialized=true;pending=false;syncPending=false;fold=false;period=seconds;lastUtc=utc;lastWall=wall;high=slot;return false;}
        const int64_t elapsed=utc-lastUtc,localElapsed=wall-lastWall;
        if(syncPending||elapsed<0||elapsed>90||localElapsed!=elapsed){
            pending=false;
            // Retain the high-water mark for a DST fallback, but allow recovery
            // after unrelated clock corrections to an earlier date/time.
            if(localElapsed-elapsed==-3600)fold=true;
            else if(!syncPending||elapsed<0||elapsed>90||localElapsed!=elapsed)fold=false;
            if(!fold||slot>high)high=slot;
            syncPending=false;
        }else if(slot>high){high=slot;pending=true;fold=false;}
        lastUtc=utc;lastWall=wall;
        if(blocked||!pending)return false;
        pending=false;return true;
    }
};

// Allocation occurs only when the group/catalog size changes. The remaining
// cycle is shuffled in place; manual visits remove that item from this cycle.
struct Shuffle {
    std::vector<int> bag;size_t remaining=0;
    template<class Random>void reset(int count,int current,Random random){bag.resize(count>0?count:0);refill(current,random);visit(current);}
    template<class Random>void refill(int current,Random random){
        for(size_t i=0;i<bag.size();++i)bag[i]=int(i);
        for(size_t i=bag.size();i>1;--i)std::swap(bag[i-1],bag[random()%i]);
        remaining=bag.size();
        if(remaining>1&&bag[remaining-1]==current)std::swap(bag[remaining-1],bag[0]);
    }
    void visit(int value){for(size_t i=0;i<remaining;++i)if(bag[i]==value){std::swap(bag[i],bag[--remaining]);break;}}
    template<class Random>int next(int current,Random random){if(bag.size()<2)return current;if(!remaining)refill(current,random);return bag[--remaining];}
};
}
