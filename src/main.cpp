// Dedicated clock firmware. Board pin/protocol details derived from Orb OS
// (MIT; see LICENSE). Clock-only renderer with optional RAM-cached SD groups.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <sys/time.h>
#include "assets.h"
#include "clock_logic.h"
#include "renderer.h"
#include "tick_audio.h"
#include "face_groups.h"
#include "favorites.h"
#include <vector>

namespace {
constexpr char VERSION[]="Meridian Clock 0.6.0-dev";
constexpr uint8_t RTC=0x51,TOUCH=0x5A;
Preferences clockPrefs;
String tzRule;
int face=1,brightness=200,rotationDeg=0;
bool rtcPresent=false,touchPresent=false,forceFrame=true,saveFace=false;
volatile bool ntpEvent=false;
bool synced=false;
time_t lastDrawn=0,lastRtcWrite=0;
uint32_t lastFaceChange=0,lastTouchPoll=0,lastTouchGood=0;
uint32_t faceChanges=0,touchPackets=0,swipes=0,switchUs=0;
uint32_t prepareUs=0;
uint32_t firstFrameMs=0;
int touchX=-1,touchY=-1;
bool touchDown=false;
volatile bool touchPending=false;
Swipe swipe;
volatile int32_t rawPosition=0,anchor=0,detents=0;
volatile uint8_t previousAB=0;
int32_t consumedDetents=0;
DRAM_ATTR int8_t quad[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
char commandLine[160];size_t commandLength=0;
ClockButton button;
TickGate tickGate;
HourlyChimeGate chimeGate;
bool ticking=true;
int tickVolume=35;
int lastAudibleVolume=60;
bool saveVolume=false;
uint32_t lastVolumeChange=0,volumeChanges=0;
uint32_t soundToggles=0;
bool menuOpen=false,menuDirty=false;
int menuChoice=0;
String menuMessage;
uint32_t menuActivity=0,lastMenuFrame=0,starStarted=0,lastStarFrame=0;
bool menuPrompt=false,starActive=false;int starStyle=1;
void notifyFavorite(){favoritesFace(face);starActive=favoritesCurrent();starStarted=millis();starStyle=1;rendererFavorite(starActive?255:0,starStyle,false);}
bool toggleFavorite(){bool ok=favoritesToggle();starActive=true;starStarted=millis();starStyle=ok?(favoritesCurrent()?1:-1):2;rendererFavorite(255,starStyle);return ok;}
int menuCount(){return menuPrompt?2:groupsCount()+(favoritesDirty()?1:0);}

void drawMenu() {
    std::vector<const char*> names;
    if(menuPrompt){names.push_back("Build Now");names.push_back("Later");}
    else{for(int i=0;i<groupsCount();++i)names.push_back(groupName(i));if(favoritesDirty())names.push_back("Update Favorites");}
    menuChoice=wrapFace(menuChoice,0,names.size());
    rendererMenu(names.data(),names.size(),menuChoice,menuMessage.length()?menuMessage.c_str():menuPrompt?"Favorites have changed":nullptr);
    menuDirty=false;lastMenuFrame=millis();
}
void openMenu() {
    groupsScan();menuOpen=true;menuDirty=true;menuPrompt=favoritesPrompt();menuChoice=menuPrompt?0:groupCurrent();menuMessage="";menuActivity=millis();swipe=Swipe();
}
void chooseGroup(int target) {
    rendererMenu(nullptr,0,0,"Loading group...");
    bool ok=groupSelect(target);face=0;forceFrame=true;saveFace=true;lastFaceChange=millis();
    // Failure is a safe fallback, not a half-loaded rotation.
    clockPrefs.putString("group",groupId(groupCurrent()));
    if(ok){menuOpen=false;menuMessage="";}else{menuOpen=true;menuMessage=groupError();menuChoice=0;menuDirty=true;}
    notifyFavorite();menuActivity=millis();
}

void serviceClockAudio(){
    const time_t now=time(nullptr);tm local={};localtime_r(&now,&local);
    const bool hour=chimeGate.update(now,local.tm_min,local.tm_sec,now>1704067200,ticking);
    const bool second=tickGate.update(now,now>1704067200,ticking);
    if(hour)tickAudioChime();else if(second)tickAudioPlay();
}
void buildProgress(const char *message){serviceClockAudio();rendererMenu(nullptr,0,0,message);Serial.printf("[favorites] %s\n",message);}
bool rebuildFavorites(){
    String oldId=groupId(groupCurrent());int oldFace=face;
    bool ok=favoritesBuild(buildProgress);String why=favoritesError();
    groupsScan();int restore=0;for(int i=1;i<groupsCount();++i)if(oldId==groupId(i)){restore=i;break;}
    if(!groupSelect(restore))groupSelect(0);face=wrapFace(oldFace,0,groupFaceCount());if(!rendererPrepare(face))face=0;
    notifyFavorite();forceFrame=true;saveFace=true;lastFaceChange=millis();
    menuPrompt=false;menuChoice=groupCurrent();menuOpen=!ok;menuMessage=why;menuDirty=!ok;menuActivity=millis();return ok;
}
void chooseMenu(){
    if(menuPrompt){if(!menuChoice)rebuildFavorites();else if(favoritesLater()){menuPrompt=false;menuChoice=groupCurrent();menuMessage="";menuDirty=true;}else{menuMessage=favoritesError();menuDirty=true;}menuActivity=millis();}
    else if(menuChoice==groupsCount())rebuildFavorites();else chooseGroup(menuChoice);
}

void setVolume(int value) {
    value=constrain(value,0,100);
    if (value==tickVolume) return;
    tickVolume=value;++volumeChanges;
    const bool enabled=value>0;
    if (enabled!=ticking) ++soundToggles;
    ticking=enabled;
    if (enabled) lastAudibleVolume=value;
    tickAudioVolume(value);
    if (enabled) tickAudioEnable();else tickAudioSilence();
    saveVolume=true;lastVolumeChange=millis();
}

void IRAM_ATTR encoderEdge() {
    const uint8_t state=(digitalRead(18)<<1)|digitalRead(17);
    rawPosition+=quad[((previousAB<<2)|state)&15];previousAB=state;
    while (rawPosition-anchor>=4) {anchor+=4;++detents;}
    while (anchor-rawPosition>=4) {anchor-=4;--detents;}
}
void IRAM_ATTR touchInterrupt() {touchPending=true;}

bool probe(uint8_t address) {
    Wire.beginTransmission(address);return Wire.endTransmission()==0;
}
int fromBcd(uint8_t value) {return (value>>4)*10+(value&15);}
uint8_t toBcd(int value) {return uint8_t((value/10)*16+value%10);}

void seedRtc() {
    rtcPresent=probe(RTC);
    if (!rtcPresent) return;
    Wire.beginTransmission(RTC);Wire.write(0x04);
    if (Wire.endTransmission(false)!=0 || Wire.requestFrom(int(RTC),7)!=7) return;
    const uint8_t sec=Wire.read();
    const int minute=fromBcd(Wire.read()&127),hour=fromBcd(Wire.read()&63);
    const int day=fromBcd(Wire.read()&63);Wire.read();
    const int month=fromBcd(Wire.read()&31),year=2000+fromBcd(Wire.read());
    if ((sec&128)||year<2024||month<1||month>12||day<1||day>31||hour>23||minute>59||fromBcd(sec)>59) return;
    timeval tv={time_t(utcEpoch(year,month,day,hour,minute,fromBcd(sec))),0};
    settimeofday(&tv,nullptr);
}

void writeRtc(time_t now) {
    if (!rtcPresent) return;
    tm utc={};gmtime_r(&now,&utc);
    Wire.beginTransmission(RTC);Wire.write(0x04);
    Wire.write(toBcd(utc.tm_sec));Wire.write(toBcd(utc.tm_min));Wire.write(toBcd(utc.tm_hour));
    Wire.write(toBcd(utc.tm_mday));Wire.write(toBcd(utc.tm_wday));
    Wire.write(toBcd(utc.tm_mon+1));Wire.write(toBcd(utc.tm_year-100));
    if (Wire.endTransmission()==0) lastRtcWrite=now;
}

void changeFace(int target) {
    target=wrapFace(target,0,groupFaceCount());
    if (target==face) return;
    uint32_t started=micros();
    if(!rendererPrepare(target)) return;
    prepareUs=micros()-started;
    face=target;forceFrame=true;saveFace=true;
    lastFaceChange=millis();++faceChanges;notifyFavorite();
}

void pollTouch() {
    if (!touchPresent || millis()-lastTouchPoll<20) return;
    lastTouchPoll=millis();
    if (!touchDown && !touchPending && digitalRead(11)!=LOW) return;
    touchPending=false;
    uint8_t data[10];
    Wire.beginTransmission(TOUCH);Wire.write(0xD0);Wire.write(0x00);
    bool ok=Wire.endTransmission(true)==0;
    if (ok) {
        delayMicroseconds(500);
        ok=Wire.requestFrom(int(TOUCH),10)==10;
        if (ok) for (auto &value:data) value=Wire.read();
    }
    if (!ok || data[6]!=0xAB) {
        if (touchDown && millis()-lastTouchGood>150) {
            touchDown=false;
            const int step=swipe.update(false,0,0,millis());
            if (step && !menuOpen) {changeFace(wrapFace(face,step,groupFaceCount()));++swipes;}
        }
        return;
    }
    ++touchPackets;lastTouchGood=millis();
    const int status=data[0]&15;
    touchDown=(data[5]&127)>0 && (status==6 || status==7);
    if (touchDown) {
        touchX=465-constrain((int(data[1])<<4)|(data[3]>>4),0,465);
        touchY=465-constrain((int(data[2])<<4)|(data[3]&15),0,465);
    }
    const int step=swipe.update(touchDown,touchX,touchY,millis());
    if (step && !menuOpen) {changeFace(wrapFace(face,step,groupFaceCount()));++swipes;}
}

void replyStatus() {
    const time_t now=time(nullptr);tm local={};localtime_r(&now,&local);
    char stamp[32];strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S",&local);
    Serial.printf("!clock {\"ok\":true,\"fw\":\"%s\",\"face\":%d,\"name\":\"%s\","
        "\"local_time\":\"%s\",\"time_valid\":%s,\"timezone\":\"%s\","
        "\"wifi\":%s,\"ntp_synced\":%s,\"rtc\":%s,\"touch\":%s,\"touch_down\":%s,"
        "\"touch_x\":%d,\"touch_y\":%d,\"touch_packets\":%lu,\"swipes\":%lu,"
        "\"detents\":%ld,\"face_changes\":%lu,\"frames\":%lu,\"render_us\":%lu,"
        "\"switch_us\":%lu,\"first_frame_ms\":%lu,\"heap\":%lu,\"psram\":%lu,\"uptime_ms\":%lu,"
        "\"ticking\":%s,\"audio_ready\":%s,\"tick_volume\":%d,\"ticks_played\":%lu,"
        "\"ticks_dropped\":%lu,\"sound_toggles\":%lu,\"volume_changes\":%lu,\"button_down\":%s,\"face_count\":%d}\n",
        VERSION,face,groupFace(face).name,stamp,now>1704067200 ? "true":"false",tzRule.c_str(),
        WiFi.status()==WL_CONNECTED ? "true":"false",synced ? "true":"false",
        rtcPresent ? "true":"false",touchPresent ? "true":"false",touchDown ? "true":"false",
        touchX,touchY,(unsigned long)touchPackets,(unsigned long)swipes,(long)detents,
        (unsigned long)faceChanges,(unsigned long)rendererFrames(),(unsigned long)rendererLastUs(),
        (unsigned long)switchUs,(unsigned long)firstFrameMs,(unsigned long)ESP.getFreeHeap(),
        (unsigned long)ESP.getFreePsram(),(unsigned long)millis(),
        ticking ? "true":"false",tickAudioReady() ? "true":"false",tickVolume,
        (unsigned long)tickAudioCount(),(unsigned long)tickAudioDropped(),
        (unsigned long)soundToggles,(unsigned long)volumeChanges,button.stable ? "true":"false",groupFaceCount());
}

void handleCommand(char *line) {
    if (strncmp(line,"?clock ",7)!=0) return;
    const char *cmd=line+7;
    if (!strcmp(cmd,"status") || !strcmp(cmd,"hello")) replyStatus();
    else if (!strcmp(cmd,"groups")) {
        Serial.printf("!clock {\"ok\":true,\"current\":%d,\"menu\":%s,\"groups\":[",groupCurrent(),menuOpen?"true":"false");
        for(int i=0;i<groupsCount();++i)Serial.printf("%s{\"index\":%d,\"id\":\"%s\",\"name\":\"%s\"}",i?",":"",i,groupId(i),groupName(i));
        Serial.printf("],\"cached_bytes\":%lu,\"sd_pack_read_bytes\":%lu,\"favorites_prompt\":%s,\"update_favorites\":%s,\"menu_choice\":%d}\n",(unsigned long)groupCachedBytes(),(unsigned long)groupSdReadBytes(),menuPrompt?"true":"false",favoritesDirty()?"true":"false",menuChoice);
    } else if (!strcmp(cmd,"menu")) {openMenu();Serial.println("!clock {\"ok\":true}");}
    else if (!strncmp(cmd,"menu choose ",12)){
        char *end=nullptr;long choice=strtol(cmd+12,&end,10);
        if(!menuOpen||end==cmd+12||*end||choice<0||choice>=menuCount()){Serial.println("!clock {\"ok\":false,\"error\":\"invalid menu choice\"}");return;}
        menuChoice=choice;chooseMenu();Serial.println("!clock {\"ok\":true}");
    }
    else if (!strcmp(cmd,"menu cancel")) {menuOpen=false;forceFrame=true;Serial.println("!clock {\"ok\":true}");}
    else if (!strncmp(cmd,"group ",6)) {
        char *end=nullptr;long target=strtol(cmd+6,&end,10);
        if(end==cmd+6||*end||target<0||target>=groupsCount()){Serial.println("!clock {\"ok\":false,\"error\":\"invalid group\"}");return;}
        chooseGroup(target);Serial.printf("!clock {\"ok\":%s,\"group\":%d,\"error\":\"%s\"}\n",menuOpen?"false":"true",groupCurrent(),groupError());
    } else if (!strcmp(cmd,"sd status")) {groupSdStatus();
    } else if (!strncmp(cmd,"sd info ",8)) {groupSdInfo(cmd+8);
    } else if (!strncmp(cmd,"sd begin ",9)) {
        char filename[64],hash[65]={};unsigned long bytes=0;bool ok=sscanf(cmd+9,"%63s %lu %64s",filename,&bytes,hash)>=2&&groupUploadBegin(filename,bytes,hash);
        Serial.printf("!clock {\"ok\":%s,\"error\":\"%s\"}\n",ok?"true":"false",groupError());
    } else if (!strncmp(cmd,"sd chunk ",9)) {
        unsigned long offset=0,bytes=0;bool ok=sscanf(cmd+9,"%lu %lu",&offset,&bytes)==2&&groupUploadChunk(offset,bytes);
        Serial.printf("!clock {\"ok\":%s,\"offset\":%lu,\"error\":\"%s\"}\n",ok?"true":"false",offset,groupError());
    } else if (!strcmp(cmd,"sd finish")) {
        bool ok=groupUploadFinish();Serial.printf("!clock {\"ok\":%s,\"error\":\"%s\"}\n",ok?"true":"false",groupError());
    } else if (!strcmp(cmd,"sd abort")) {groupUploadAbort();Serial.println("!clock {\"ok\":true}");}
    else if (!strcmp(cmd,"favorites status")) favoritesStatus();
    else if (!strcmp(cmd,"favorites toggle")) {bool ok=toggleFavorite();Serial.printf("!clock {\"ok\":%s,\"favorite\":%s,\"error\":\"%s\"}\n",ok?"true":"false",favoritesCurrent()?"true":"false",favoritesError());}
    else if (!strcmp(cmd,"favorites later")) {bool ok=favoritesLater();Serial.printf("!clock {\"ok\":%s,\"error\":\"%s\"}\n",ok?"true":"false",favoritesError());}
    else if (!strcmp(cmd,"favorites build")) {bool ok=rebuildFavorites();Serial.printf("!clock {\"ok\":%s,\"error\":\"%s\"}\n",ok?"true":"false",favoritesError());}
    else if (!strcmp(cmd,"audio status")) tickAudioDiagnostics();
    else if (!strcmp(cmd,"chime test")) {
        bool ok=tickAudioChime();Serial.printf("!clock {\"ok\":%s,\"error\":\"%s\"}\n",ok?"true":"false",ok?"":"Chime muted or audio unavailable");
    } else if (!strcmp(cmd,"chime status")) {
        Serial.printf("!clock {\"ok\":true,\"hourly_chime\":true,\"chimes_played\":%lu,\"muted\":%s}\n",(unsigned long)tickAudioChimeCount(),tickVolume?"false":"true");
    }
    else if (!strcmp(cmd,"audio test")) {
        const bool ok=tickAudioTest();
        Serial.printf("!clock {\"ok\":%s,\"audio_test\":true}\n",ok ? "true":"false");
    } else if (!strncmp(cmd,"face ",5)) {
        char *end=nullptr;const long choice=strtol(cmd+5,&end,10);
        if (end==cmd+5 || *end || choice<0 || choice>=groupFaceCount()) {Serial.println("!clock {\"ok\":false,\"error\":\"invalid face\"}");return;}
        changeFace(int(choice));Serial.printf("!clock {\"ok\":true,\"face\":%d}\n",face);
    } else if (!strcmp(cmd,"next")) {
        changeFace(wrapFace(face,1,groupFaceCount()));Serial.printf("!clock {\"ok\":true,\"face\":%d}\n",face);
    } else if (!strcmp(cmd,"ticking on") || !strcmp(cmd,"ticking off")) {
        setVolume(!strcmp(cmd,"ticking on") ? lastAudibleVolume:0);
        Serial.printf("!clock {\"ok\":true,\"ticking\":%s}\n",ticking ? "true":"false");
    } else if (!strncmp(cmd,"volume ",7)) {
        char *end=nullptr;const long value=strtol(cmd+7,&end,10);
        if (end==cmd+7 || *end || value<0 || value>100) {
            Serial.println("!clock {\"ok\":false,\"error\":\"volume must be 0..100\"}");return;
        }
        setVolume(int(value));
        Serial.printf("!clock {\"ok\":true,\"tick_volume\":%d}\n",tickVolume);
    } else if (!strcmp(cmd,"capture")) {
        Serial.setTxTimeoutMs(2000); // diagnostics: tolerate a paused host during large transfers
        Serial.println("!clock {\"ok\":true,\"bytes\":434312}");
        Serial.write(reinterpret_cast<const uint8_t *>(rendererCapture()),466*466*2);
        Serial.flush();Serial.setTxTimeoutMs(100);
    } else if (!strncmp(cmd,"timezone ",9)) {
        const char *value=cmd+9;
        if (!*value || strlen(value)>80 || strspn(value,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789,+-<>:/.")!=strlen(value)) {
            Serial.println("!clock {\"ok\":false,\"error\":\"invalid POSIX timezone\"}");
            return;
        }
        tzRule=value;clockPrefs.putString("tz",tzRule);
        configTzTime(tzRule.c_str(),"pool.ntp.org","time.nist.gov");
        forceFrame=true;Serial.println("!clock {\"ok\":true}");
    } else if (!strcmp(cmd,"reboot")) {
        Serial.println("!clock {\"ok\":true}");Serial.flush();ESP.restart();
    } else Serial.println("!clock {\"ok\":false,\"error\":\"unknown command\"}");
}

void pollSerial() {
    while (Serial.available()) {
        const char c=Serial.read();
        if (c=='\r') continue;
        if (c=='\n') {commandLine[commandLength]=0;handleCommand(commandLine);commandLength=0;Serial.flush();}
        else if (commandLength<sizeof(commandLine)-1) commandLine[commandLength++]=c;
        else commandLength=0;
    }
}
}

void setup() {
    Serial.begin(115200);
    // Let USB drain long diagnostic replies instead of truncating them when
    // several requests arrive back to back. No work is added while unplugged.
    Serial.setTxTimeoutMs(100);
    // Read the previous owner's time/brightness/orientation; never modify its
    // namespace, WiFi credentials, partition layout or SD themes.
    Preferences previous;previous.begin("capsuleradar",true);
    tzRule=previous.getString("tz","EST5EDT,M3.2.0,M11.1.0");
    brightness=constrain(previous.getInt("bright",200),0,255);
    rotationDeg=previous.isKey("rotDeg") ? previous.getInt("rotDeg",0) : previous.getInt("rot",0)*90;
    previous.end();
    clockPrefs.begin("orb-clock",false);face=clockPrefs.getUChar("face",1)%FACE_COUNT;
    tickVolume=constrain(clockPrefs.getInt("tickVol",35),0,100);
    if (clockPrefs.getUChar("controls",0)<2) {
        if (!clockPrefs.getBool("tickOn",true)) tickVolume=0;
        clockPrefs.putInt("tickVol",tickVolume);clockPrefs.putUChar("controls",2);
    }
    lastAudibleVolume=constrain(clockPrefs.getInt("tickLast",tickVolume>0 ? tickVolume:60),1,100);
    ticking=tickVolume>0;
    // Keep the recipient timezone; no owner-specific offset migration.
    tzRule=clockPrefs.getString("tz",tzRule);
    setenv("TZ",tzRule.c_str(),1);tzset();
    Wire.begin(15,14,400000);Wire.setTimeOut(10);seedRtc();
    pinMode(18,INPUT_PULLUP);pinMode(17,INPUT_PULLUP);pinMode(16,INPUT_PULLUP);
    previousAB=(digitalRead(18)<<1)|digitalRead(17);
    attachInterrupt(digitalPinToInterrupt(18),encoderEdge,CHANGE);
    attachInterrupt(digitalPinToInterrupt(17),encoderEdge,CHANGE);
    // Keep the speaker amplifier off until audio initialization.
    pinMode(46,OUTPUT);digitalWrite(46,LOW);
    if (!rendererBegin(brightness,rotationDeg)) {
        Serial.println("[clock] display allocation/init failed");
        while (true) {pollSerial();delay(10);}
    }
    const time_t now=time(nullptr);tm local={};localtime_r(&now,&local);
    renderClock(face,local,now>1704067200,true);lastDrawn=now;forceFrame=false;
    firstFrameMs=millis();
    pinMode(40,OUTPUT);digitalWrite(40,LOW);delay(10);digitalWrite(40,HIGH);delay(50);
    pinMode(11,INPUT);touchPresent=probe(TOUCH);
    if (touchPresent) attachInterrupt(digitalPinToInterrupt(11),touchInterrupt,FALLING);
    Serial.printf("[clock] tick audio %s\n",tickAudioBegin(tickVolume) ? "ready":"unavailable");
    if (ticking) tickAudioEnable();
    WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
    WiFi.begin(); // existing saved network; returns without waiting for association
    sntp_set_time_sync_notification_cb([](struct timeval *){ntpEvent=true;});
    configTzTime(tzRule.c_str(),"pool.ntp.org","time.nist.gov");
    favoritesBegin();groupsScan();
    String savedGroup=clockPrefs.getString("group","");
    if(savedGroup.length())for(int i=1;i<groupsCount();++i)if(savedGroup==groupId(i)){
        int savedFace=clockPrefs.getUChar("face",0);
        if(groupSelect(i))face=wrapFace(savedFace,0,groupFaceCount());else face=0;
        forceFrame=true;break;
    }
    notifyFavorite();
    Serial.printf("[clock] %s ready, first frame %lu ms, touch %s\n",VERSION,
                  (unsigned long)firstFrameMs,touchPresent ? "present":"absent");
}

void loop() {
    pollSerial();
    groupUploadPoll();
    const int32_t position=detents;
    if (position!=consumedDetents) {
        if(menuOpen){menuChoice=wrapFace(menuChoice,position-consumedDetents,menuCount());menuDirty=true;menuMessage="";menuActivity=millis();}
        else setVolume(stepVolume(tickVolume,position-consumedDetents));
        consumedDetents=position;
    }
    const auto event=button.update(digitalRead(16)==LOW,millis());
    if(event==ClockButton::LONG_PRESS){if(menuOpen){menuOpen=false;forceFrame=true;}else openMenu();}
    else if(event==ClockButton::SHORT_PRESS){if(menuOpen)chooseMenu();else toggleFavorite();}
    if(menuOpen&&millis()-menuActivity>30000){menuOpen=false;forceFrame=true;}
    if(menuOpen&&rendererMenuScrolling()&&millis()-lastMenuFrame>=50)menuDirty=true;
    if(menuOpen&&menuDirty)drawMenu();
    pollTouch();
    const time_t now=time(nullptr);
    if (ntpEvent) {ntpEvent=false;synced=true;writeRtc(now);}
    serviceClockAudio();
    if (!menuOpen && (forceFrame || now!=lastDrawn)) {
        tm local={};localtime_r(&now,&local);
        const bool switched=forceFrame;
        renderClock(face,local,now>1704067200,switched);
        if (switched) {switchUs=prepareUs+rendererLastUs();prepareUs=0;}
        forceFrame=false;lastDrawn=now;
    }
    if(!menuOpen&&starActive&&millis()-lastStarFrame>=50){
        uint32_t age=millis()-starStarted;uint8_t opacity=age<1500?255:age<2000?uint8_t((2000-age)*255/500):0;
        rendererFavorite(opacity,starStyle);lastStarFrame=millis();if(!opacity)starActive=false;
    }
    // Write flash only once the user stops turning, never on every click.
    if (saveFace && millis()-lastFaceChange>=1500) {
        clockPrefs.putUChar("face",face);clockPrefs.putString("group",groupId(groupCurrent()));saveFace=false;
    }
    if (saveVolume && millis()-lastVolumeChange>=1500) {
        clockPrefs.putInt("tickVol",tickVolume);clockPrefs.putInt("tickLast",lastAudibleVolume);
        saveVolume=false;
    }
    delay(1);
}
