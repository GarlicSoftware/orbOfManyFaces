#pragma once
#include <stdint.h>
#include <stdlib.h>

inline int wrapFace(int current, int delta,int count=2) {
    return int(((int64_t(current)+delta)%count+count)%count);
}

inline int stepVolume(int current,int32_t detents) {
    const int64_t next=int64_t(current)-int64_t(detents)*5;
    return next<0 ? 0 : next>100 ? 100 : int(next);
}

// A long hold never also performs the short-press favorite toggle on release.
struct ClockButton {
    static constexpr uint32_t HOLD_MS=1000;
    enum Event {NONE,SHORT_PRESS,LONG_PRESS};
    bool observed=false,stable=false,longFired=false;
    uint32_t changed=0,pressedAt=0;
    Event update(bool raw,uint32_t now) {
        if (raw!=observed) {observed=raw;changed=now;}
        if (stable!=observed && now-changed>=30) {
            stable=observed;
            if (stable) {pressedAt=now;longFired=false;}
            else if (!longFired) {
                // Release can arrive between loop samples of the hold deadline.
                if (now-pressedAt>=HOLD_MS) {longFired=true;return LONG_PRESS;}
                return SHORT_PRESS;
            }
        }
        if (stable && !longFired && now-pressedAt>=HOLD_MS) {
            longFired=true;return LONG_PRESS;
        }
        return NONE;
    }
};

struct TickGate {
    bool initialized=false;
    int64_t last=0;
    bool update(int64_t second,bool valid,bool enabled) {
        if (!valid) {initialized=false;return false;}
        if (!initialized) {last=second;initialized=true;return false;}
        const bool changed=second!=last;last=second;
        return changed&&enabled;
    }
};

// Ring only on a live hour transition, never at startup or after a clock jump.
// A short grace window covers a busy renderer crossing second zero.
struct HourlyChimeGate {
    bool initialized=false;
    int64_t last=0;
    bool update(int64_t second,int minute,int sec,bool valid,bool enabled) {
        if(!valid){initialized=false;return false;}
        if(!initialized){last=second;initialized=true;return false;}
        const int64_t elapsed=second-last;last=second;
        return enabled&&elapsed>0&&elapsed<=10&&minute==0&&sec>=0&&sec<=9&&elapsed>sec;
    }
};

// Full-detent hysteresis: contact bounce must not change the selected face.
struct Encoder {
    int32_t raw = 0, anchor = 0, detents = 0;
    uint8_t previous = 0;
    void edge(uint8_t state) {
        static const int8_t table[16] = {
            0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0
        };
        raw += table[((previous << 2) | state) & 15];
        previous = state;
        while (raw-anchor >= 4) { anchor += 4; ++detents; }
        while (anchor-raw >= 4) { anchor -= 4; --detents; }
    }
};

// Complete on release. A drag or hold produces at most one face change.
struct Swipe {
    bool down = false;
    int startX = 0, startY = 0, lastX = 0, lastY = 0;
    uint32_t started = 0;
    int update(bool pressed, int x, int y, uint32_t now) {
        if (pressed) {
            if (!down) { down=true; startX=x; startY=y; started=now; }
            lastX=x; lastY=y;
            return 0;
        }
        if (!down) return 0;
        down=false;
        const int dx=lastX-startX, dy=lastY-startY;
        if (now-started > 1500 || abs(dx) < 65 || abs(dx) < abs(dy)*2) return 0;
        return dx < 0 ? 1 : -1;
    }
};

// UTC civil date to Unix seconds, independent of the configured local timezone.
inline int64_t utcEpoch(int year, unsigned month, unsigned day, int hour, int minute, int second) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year-399)/400;
    const unsigned yoe = unsigned(year-era*400);
    const unsigned doy = (153*(month+(month>2 ? -3 : 9))+2)/5+day-1;
    const unsigned doe = yoe*365+yoe/4-yoe/100+doy;
    return (int64_t(era)*146097+doe-719468)*86400+hour*3600+minute*60+second;
}
