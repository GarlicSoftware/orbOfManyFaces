#include "network_setup.h"
#include "setup_logic.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <memory>
#include <time.h>

namespace {
Preferences *preferences=nullptr;
std::unique_ptr<WebServer> server;
std::unique_ptr<DNSServer> dns;
bool active=false,checking=false,closeRequested=false,needsSetup=false;
bool (*zoneSaver)(const String &)=nullptr;
String apName,apPassword,token,message,zone,oldSsid,oldPassword;
orbsetup::Network candidate;
uint32_t started=0,checkStarted=0,revision=0;
const IPAddress apAddress(192,168,4,1);
orbsetup::Network configuredNetwork(){
    wifi_config_t config={};orbsetup::Network n;
    if(esp_wifi_get_config(WIFI_IF_STA,&config)==ESP_OK){
        n.ssid.assign((const char*)config.sta.ssid,strnlen((const char*)config.sta.ssid,32));
        n.password.assign((const char*)config.sta.password,strnlen((const char*)config.sta.password,64));n.open=n.password.empty();
    }
    return n;
}
void note(const char *text){message=text;++revision;}
std::string value(const char *key){String v=server->arg(key);return std::string(v.c_str(),v.length());}
String escaped(const String &s){return String(orbsetup::escape(std::string(s.c_str(),s.length())).c_str());}
void headers(){server->sendHeader("Cache-Control","no-store");server->sendHeader("X-Frame-Options","DENY");server->sendHeader("Referrer-Policy","no-referrer");}
void reject(const char *why,int status=400){headers();server->send(status,"text/plain; charset=utf-8",why);}
bool authorized(){if(server->arg("token")!=token){reject("This setup form expired. Reload the setup page.",403);return false;}return true;}
void redirect(){headers();server->sendHeader("Location","/");server->send(303,"text/plain","");}
void restore(){
    WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);
    if(oldSsid.length())WiFi.begin(oldSsid.c_str(),oldPassword.c_str());
    WiFi.setAutoReconnect(true);checking=false;candidate={};
}
bool savedNetwork(orbsetup::Network &network){
    size_t n=preferences->getBytesLength("wifi");if(n<12||n>108)return false;
    uint8_t bytes[108];return preferences->getBytes("wifi",bytes,n)==n&&orbsetup::decode(bytes,n,network);
}
bool saveNetwork(const orbsetup::Network &network){
    const size_t oldSize=preferences->getBytesLength("wifi");
    if(oldSize>108)return false;
    std::vector<uint8_t> previous(oldSize);
    if(oldSize&&preferences->getBytes("wifi",previous.data(),oldSize)!=oldSize)return false;
    auto bytes=orbsetup::encode(network);
    if(preferences->putBytes("wifi",bytes.data(),bytes.size())!=bytes.size())return false;
    orbsetup::Network verify;
    if(savedNetwork(verify)&&verify.ssid==network.ssid&&verify.password==network.password&&verify.open==network.open)return true;
    if(oldSize)preferences->putBytes("wifi",previous.data(),previous.size());else preferences->remove("wifi");
    return false;
}
void page(){
    String html;html.reserve(7500);
    html="<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    if(checking)html+="<meta http-equiv='refresh' content='3'>";
    html+="<title>Orb Wi-Fi &amp; time</title><style>body{font:17px system-ui;max-width:36em;margin:2em auto;padding:0 1em;color:#eee;background:#17191b}input,select,button{box-sizing:border-box;width:100%;font:inherit;padding:.65em;margin:.4em 0 1em}input[type=checkbox]{width:auto}fieldset{margin:1.5em 0;padding:1em}code{overflow-wrap:anywhere}.status{color:#e9ce88}small{display:block}</style></head><body><h1>Orb Wi-Fi &amp; time</h1>";
    html+="<p class='status'>"+escaped(message)+"</p><p>This temporary setup network closes after 10 minutes. Stay connected even if your phone says there is no internet.</p>";
    html+="<fieldset><legend>Wi-Fi (2.4 GHz)</legend><p>Connection: ";html+=WiFi.status()==WL_CONNECTED?"connected":"not connected";
    html+="</p><form method='post' action='/wifi'><input type='hidden' name='token' value='"+token+"'><label>Network name (SSID)<input name='ssid' maxlength='32' required autocomplete='off'></label><label>Network password<input name='password' type='password' maxlength='64' autocomplete='new-password'></label><label><input name='open' type='checkbox' value='1'> This is an open network with no password</label><p>Your previous network stays saved until the new connection succeeds. Passwords are never displayed.</p><button";
    if(checking)html+=" disabled";html+=">Connect and save Wi-Fi</button></form></fieldset>";
    const time_t now=time(nullptr);tm local={};localtime_r(&now,&local);char stamp[32];strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S",&local);
    html+="<fieldset><legend>Time zone</legend><p>Orb time: "+String(now>1704067200?stamp:"not synchronized yet")+"<br>Current rule: <code>"+escaped(zone)+"</code></p>";
    if(orbsetup::fixedZone(zone.c_str()))html+="<p>This is a fixed-offset rule. If your region observes daylight saving, choose its regional time zone below. A fixed offset is correct for regions without daylight saving.</p>";
    html+="<form method='post' action='/time'><input type='hidden' name='token' value='"+token+"'><label>Region<select name='zone'><option value='keep'>Keep current time zone</option>";
    for(int i=0;i<orbsetup::zoneCount;++i){html+="<option value='"+String(i)+"'";if(zone==orbsetup::zones[i].rule)html+=" selected";html+=">"+escaped(orbsetup::zones[i].label)+"</option>";}
    html+="</select></label><details><summary>Other region / advanced POSIX rule</summary><p>Enter a full regional rule, including DST transitions if applicable. This overrides the region above.</p><input name='custom' maxlength='80' autocomplete='off' placeholder='EST5EDT,M3.2.0,M11.1.0'></details><button>Save time zone</button></form></fieldset><form method='post' action='/close'><input type='hidden' name='token' value='"+token+"'><button>Finish setup</button></form></body></html>";
    headers();server->send(200,"text/html; charset=utf-8",html);
}
void wifiSubmit(){
    if(!authorized())return;if(checking){reject("A connection is already being checked. Please wait.",409);return;}
    orbsetup::Network next;next.ssid=value("ssid");next.password=value("password");next.open=server->arg("open")=="1";
    if(!orbsetup::validNetwork(next.ssid,next.password,next.open)){reject("Use a 1-32 byte network name and an 8-63 character password (or 64 hex digits). For an open network, check the open-network box and leave the password empty.");return;}
    auto old=configuredNetwork();oldSsid=old.ssid.c_str();oldPassword=old.password.c_str();candidate=next;
    WiFi.setAutoReconnect(false);
    if(!WiFi.disconnect(false,false)){candidate={};WiFi.setAutoReconnect(true);reject("Could not leave the previous connection. Please retry.",503);return;}
    WiFi.begin(candidate.ssid.c_str(),candidate.password.c_str());
    checking=true;checkStarted=millis();note("Checking the new Wi-Fi connection...");redirect();
}
void timeSubmit(){
    if(!authorized())return;
    auto custom=value("custom");String chosen;
    if(!custom.empty())chosen=custom.c_str();
    else{auto index=value("zone");if(index=="keep")chosen=zone;else{const char *p=index.c_str();char *end=nullptr;long i=strtol(p,&end,10);if(end==p||*end||i<0||i>=orbsetup::zoneCount){reject("Choose a valid time zone.");return;}chosen=orbsetup::zones[i].rule;}}
    if(!orbsetup::validZone(std::string(chosen.c_str(),chosen.length()))){reject("Invalid POSIX time-zone rule. Choose a region or provide a complete rule with explicit daylight-saving transitions.");return;}
    if(!zoneSaver(chosen)){reject("Time-zone settings could not be saved. The previous setting is retained.",500);return;}
    zone=chosen;note("Time zone saved. The clock updates immediately.");redirect();
}
}
void networkBegin(Preferences &prefs){
    preferences=&prefs;WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
    orbsetup::Network n;
    if(savedNetwork(n)){WiFi.begin(n.ssid.c_str(),n.password.c_str());needsSetup=false;}
    else{WiFi.begin();needsSetup=configuredNetwork().ssid.empty();}
}
bool networkNeedsSetup(){return needsSetup;}
bool networkSetupStart(const String &currentZone,bool (*saveZone)(const String &)){
    if(active)return true;if(!preferences||!saveZone)return false;
    zone=currentZone;zoneSaver=saveZone;char name[24],secret[13],nonce[33];
    snprintf(name,sizeof(name),"OrbSetup-%06lX",(unsigned long)(ESP.getEfuseMac()&0xffffff));
    const char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for(unsigned i=0;i<12;++i)secret[i]=alphabet[esp_random()%(sizeof(alphabet)-1)];secret[12]=0;
    for(unsigned i=0;i<4;++i)snprintf(nonce+i*8,9,"%08lx",(unsigned long)esp_random());
    apName=name;apPassword=secret;token=nonce;
    WiFi.mode(WIFI_AP_STA);
    if(!WiFi.softAPConfig(apAddress,apAddress,IPAddress(255,255,255,0))||!WiFi.softAP(apName.c_str(),apPassword.c_str(),1,0,2)){
        WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);note("Could not start setup hotspot");return false;
    }
    server.reset(new WebServer(apAddress,80));dns.reset(new DNSServer());
    server->on("/",HTTP_GET,page);server->on("/wifi",HTTP_POST,wifiSubmit);server->on("/time",HTTP_POST,timeSubmit);
    server->on("/close",HTTP_POST,[]{if(!authorized())return;headers();server->send(200,"text/html","<p>Setup closed. Reconnect your phone to your normal Wi-Fi.</p>");closeRequested=true;});
    server->onNotFound([]{headers();server->sendHeader("Location","http://192.168.4.1/");server->send(302,"text/plain","");});
    if(!dns->start(53,"*",apAddress)){server.reset();dns.reset();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);note("Could not start setup network");return false;}
    server->begin();active=true;closeRequested=false;started=millis();note("Connect your phone to this network");return true;
}
void networkSetupStop(){
    if(!active)return;if(checking)restore();
    server->stop();dns->stop();server.reset();dns.reset();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);
    active=false;closeRequested=false;token="";apPassword="";oldPassword="";oldSsid="";note("Setup closed");
}
void networkSetupPoll(){
    if(!active)return;dns->processNextRequest();server->handleClient();
    if(checking){
        if(WiFi.status()==WL_CONNECTED&&WiFi.SSID()==candidate.ssid.c_str()&&uint32_t(WiFi.localIP())!=0){
            bool ok=saveNetwork(candidate);
            if(ok){checking=false;candidate={};oldSsid="";oldPassword="";needsSetup=false;WiFi.setAutoReconnect(true);note("Wi-Fi connected and saved");}
            else{restore();note("Save failed; previous Wi-Fi restored");}
        }else if(millis()-checkStarted>=30000){restore();note("Connection failed; previous Wi-Fi kept");}
    }
    if(closeRequested||millis()-started>=600000)networkSetupStop();
}
bool networkSetupActive(){return active;}
const char *networkSetupSsid(){return apName.c_str();}
const char *networkSetupPassword(){return apPassword.c_str();}
const char *networkSetupMessage(){return message.c_str();}
uint32_t networkSetupRevision(){return revision;}
void networkSetupStatus(){
    Serial.printf("!clock {\"ok\":true,\"setup\":true,\"active\":%s,\"checking\":%s,\"wifi_connected\":%s,\"saved_custom_network\":%s}\n",active?"true":"false",checking?"true":"false",WiFi.status()==WL_CONNECTED?"true":"false",preferences->isKey("wifi")?"true":"false");
}
