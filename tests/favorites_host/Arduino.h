#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
using std::min;using std::max;
class String {
 std::string s;
public:
 String()=default;String(const char *v):s(v?v:""){}String(const std::string &v):s(v){}String(int v):s(std::to_string(v)){}String(unsigned v):s(std::to_string(v)){}
 const char *c_str()const{return s.c_str();}size_t length()const{return s.size();}bool startsWith(const char *v)const{return s.rfind(v,0)==0;}
 friend String operator+(const String&a,const String&b){return String(a.s+b.s);}friend bool operator==(const String&a,const String&b){return a.s==b.s;}friend bool operator!=(const String&a,const String&b){return !(a==b);}
};
struct FakeSerial {template<class... T> void printf(const char *fmt,T...args){std::printf(fmt,args...);}};inline FakeSerial Serial;
struct FakeEsp {uint32_t getFreePsram(){return 6*1024*1024;}};inline FakeEsp ESP;
inline uint32_t esp_random(){static uint32_t next=100;return ++next;}
inline void delay(unsigned){}
#define PROGMEM
