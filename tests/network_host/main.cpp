#include "network_setup.h"
#include "setup_logic.h"
#include "WiFi.h"
#include "WebServer.h"
#include "DNSServer.h"
#include <cassert>
#include <iostream>
#include <random>
#include "clock_logic.h"
String savedZone="EST5EDT,M3.2.0,M11.1.0";bool failZone=false;
bool saveZone(const String &zone){if(failZone)return false;savedZone=zone;return true;}
std::string token(){auto *web=WebServer::instance;web->request("/",HTTP_GET);std::string html=web->body.c_str();auto start=html.find("name='token' value='");assert(start!=std::string::npos);start+=20;auto end=html.find('\'',start);return html.substr(start,end-start);}
void post(const char *path,std::map<std::string,String> args={}){args["token"]=token();WebServer::instance->request(path,HTTP_POST,args);}
int main(){
 using namespace orbsetup;
 for(const auto &zone:zones)assert(validZone(zone.rule));
 assert(fixedZone("<-04>4")&&fixedZone("UTC0")&&!fixedZone(zones[1].rule)&&!fixedZone("EST5EDT"));
#ifndef _WIN32
 for(int i=1;i<=4;++i){
  setenv("TZ",zones[i].rule,1);tzset();const int standard=-(i+4)*3600;
  for(int month:{1,7}){time_t at=utcEpoch(2026,month,1,12,0,0);tm local={};localtime_r(&at,&local);assert(local.tm_hour==12+standard/3600+(month==7));assert(bool(local.tm_isdst)==(month==7));}
  time_t fall=utcEpoch(2026,11,1,2,0,0)-(standard+3600),spring=utcEpoch(2026,3,8,2,0,0)-standard;
  tm local={};time_t at=fall-1;localtime_r(&at,&local);assert(local.tm_hour==1&&local.tm_min==59&&local.tm_isdst>0);
  localtime_r(&fall,&local);assert(local.tm_hour==1&&local.tm_min==0&&!local.tm_isdst);
  at=spring-1;localtime_r(&at,&local);assert(local.tm_hour==1&&local.tm_min==59&&!local.tm_isdst);
  localtime_r(&spring,&local);assert(local.tm_hour==3&&local.tm_min==0&&local.tm_isdst>0);
 }
#endif
 for(const char *rule:{"UTC0","<-04>4","<+0530>-5:30","EST5EDT,M3.2.0,M11.1.0","ABC0DEF,J60/0,J300/24"})assert(validZone(rule));
 for(const char *rule:{"","UTC","EST5EDT","UTC99","UTC0;bad","CET-1CEST,M13.1.0,M10.5.0","CET-1CEST,M3.0.0,M10.5.0","CET-1CEST,M3.1.7,M10.5.0","UTC0<script>","EST5EDT,M3.2.0/M11.1.0","<ab>0"})assert(!validZone(rule));
 std::mt19937 rng(123);for(int i=0;i<10000;++i){std::string s;for(int j=0,n=rng()%90;j<n;++j)s+=char(rng()%128);validZone(s);}
 assert(!validNetwork("", "12345678",false));assert(!validNetwork(std::string(33,'a'),"12345678",false));assert(!validNetwork("N","short",false));assert(!validNetwork("N","",false));
 assert(validNetwork("N","",true));assert(!validNetwork("N","password",true));assert(validNetwork("N",std::string(64,'a'),false));assert(!validNetwork("N",std::string(64,'z'),false));
 assert(!validNetwork(std::string("a\0b",3),"12345678",false));
 Network n={"A & <Network>","a-secret-password",false},out;
 auto encoded=encode(n);assert(decode(encoded.data(),encoded.size(),out));assert(out.ssid==n.ssid&&out.password==n.password);
 for(size_t i=0;i<encoded.size();++i){auto bad=encoded;bad[i]^=1;assert(!decode(bad.data(),bad.size(),out));}
 for(size_t i=0;i<encoded.size();++i)assert(!decode(encoded.data(),i,out));
 assert(escape("<script>\"'&")=="&lt;script&gt;&quot;&#39;&amp;");
 Preferences prefs;networkBegin(prefs);assert(!WiFi.persist&&WiFi.SSID()==WiFi.stockSsid&&!networkNeedsSetup());
 assert(networkSetupStart(savedZone,saveZone));assert(networkSetupActive()&&WiFi.ap&&WebServer::instance->running);assert(uint32_t(WebServer::instance->address)==uint32_t(IPAddress(192,168,4,1)));
 assert(strlen(networkSetupPassword())==12);
 auto *web=WebServer::instance;web->request("/wifi",HTTP_POST,{{"ssid","N"},{"password","12345678"}});assert(web->status==403&&!prefs.isKey("wifi"));
 post("/wifi",{{"ssid","N"},{"password","short"}});assert(web->status==400&&!prefs.isKey("wifi"));
 post("/time",{{"zone","1"},{"custom","UTC0<script>"}});assert(web->status==400&&savedZone==zones[1].rule);
 failZone=true;post("/time",{{"zone","0"}});assert(web->status==500);failZone=false;
 post("/time",{{"zone","0"}});assert(web->status==303&&savedZone=="UTC0");
 const String old=WiFi.SSID();
 post("/wifi",{{"ssid",n.ssid},{"password",n.password}});assert(web->status==303&&!prefs.isKey("wifi"));
 web->request("/",HTTP_GET);assert(std::string(web->body.c_str()).find(n.password)==std::string::npos);
 post("/wifi",{{"ssid","Other"},{"password","12345678"}});assert(web->status==409);
 nowMs+=30001;networkSetupPoll();assert(WiFi.SSID()==old&&!prefs.isKey("wifi"));
 post("/wifi",{{"ssid",n.ssid},{"password",n.password}});WiFi.connected=true;networkSetupPoll();assert(prefs.isKey("wifi"));auto original=prefs.data["wifi"];
 networkSetupStop();assert(!networkSetupActive()&&!WiFi.ap&&WebServer::instance==nullptr);
 networkBegin(prefs);assert(WiFi.SSID()==n.ssid&&WiFi.psk()==n.password);
 assert(networkSetupStart(savedZone,saveZone));
 post("/wifi",{{"ssid","New"},{"password","12345678"}});prefs.failWrite=true;WiFi.connected=true;networkSetupPoll();prefs.failWrite=false;
 assert(prefs.data["wifi"]==original&&WiFi.SSID()==n.ssid);
 post("/wifi",{{"ssid","Readback"},{"password","12345678"}});prefs.corruptAfterWrite=true;WiFi.connected=true;networkSetupPoll();
 assert(prefs.data["wifi"]==original&&WiFi.SSID()==n.ssid);
 post("/wifi",{{"ssid","Cancel"},{"password","12345678"}});networkSetupStop();assert(prefs.data["wifi"]==original&&WiFi.SSID()==n.ssid);
 networkSetupStart(savedZone,saveZone);nowMs+=600001;networkSetupPoll();assert(!networkSetupActive());
 WiFi.failAp=true;assert(!networkSetupStart(savedZone,saveZone));assert(!networkSetupActive());WiFi.failAp=false;
 DNSServer::fail=true;assert(!networkSetupStart(savedZone,saveZone));assert(!networkSetupActive());DNSServer::fail=false;
 assert(networkSetupStart(savedZone,saveZone));post("/close");networkSetupPoll();assert(!networkSetupActive());
 Preferences blank;WiFi.stockSsid="";WiFi.stockPassword="";networkBegin(blank);assert(networkNeedsSetup());
 // Damaged owned records fall back to the stock network without modifying it.
 prefs.data["wifi"][0]^=1;WiFi.stockSsid="StillOriginal";networkBegin(prefs);assert(WiFi.SSID()==WiFi.stockSsid);
 std::cout<<"PASS: actual setup server: tokens, validation, connection-before-save, timeout/cancel rollback, restart, storage failure, AP cleanup and timezone controls\n";
}
