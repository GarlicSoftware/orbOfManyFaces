#pragma once
#include "Arduino.h"
constexpr int WIFI_STA=1,WIFI_AP_STA=3,WL_CONNECTED=3;
struct FakeWiFi {
 String stockSsid="OriginalNetwork",stockPassword="original-password",ssid,password;
 int wifiMode=0;bool connected=false,ap=false,persist=true,autoReconnect=true,failAp=false;unsigned beginCount=0;
 void persistent(bool value){persist=value;}void mode(int value){wifiMode=value;}
 void setAutoReconnect(bool value){autoReconnect=value;}
 void begin(){begin(stockSsid.c_str(),stockPassword.c_str());}
 void begin(const char *s,const char *p){ssid=s;password=p;connected=false;++beginCount;}
 String SSID(){return ssid;}String psk(){return password;}
 int status(){return connected?WL_CONNECTED:0;}
 IPAddress localIP(){return connected?IPAddress(192,168,1,4):IPAddress();}
 bool disconnect(bool,bool){connected=false;return true;}
 bool softAPConfig(IPAddress,IPAddress,IPAddress){return !failAp;}
 bool softAP(const char*,const char*,int,int,int){ap=!failAp;return ap;}
 void softAPdisconnect(bool){ap=false;}
};inline FakeWiFi WiFi;
