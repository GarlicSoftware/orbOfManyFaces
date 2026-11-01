#pragma once
#include "Arduino.h"
#include <map>
#include <functional>
constexpr int HTTP_GET=0,HTTP_POST=1;
struct WebServer {
 inline static WebServer *instance=nullptr;
 std::map<std::string,std::function<void()>> handlers;std::function<void()> missing;
 std::map<std::string,String> args,headers;String body;int status=0;bool running=false;IPAddress address;
 WebServer(IPAddress ip,int):address(ip){instance=this;}~WebServer(){instance=nullptr;}
 void on(const char *path,int method,std::function<void()> f){handlers[std::to_string(method)+path]=f;}
 void onNotFound(std::function<void()> f){missing=f;}void begin(){running=true;}void stop(){running=false;}void handleClient(){}
 String arg(const char *key){return args[key];}void sendHeader(const char *key,const String &v){headers[key]=v;}
 void send(int s,const char*,const String &v){status=s;body=v;}
 void request(const char *path,int method,std::map<std::string,String> values={}){args=values;headers.clear();body="";auto key=std::to_string(method)+path;if(handlers.count(key))handlers[key]();else missing();}
};
