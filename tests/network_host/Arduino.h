#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <algorithm>
class String {
 std::string text;
public:
 String()=default;String(const char *v):text(v?v:""){}String(const std::string &v):text(v){}String(int v):text(std::to_string(v)){}
 const char *c_str()const{return text.c_str();}size_t length()const{return text.size();}
 void reserve(size_t n){text.reserve(n);}int indexOf(char c)const{auto p=text.find(c);return p==std::string::npos?-1:int(p);}
 String &operator+=(const String &s){text+=s.text;return *this;}
 friend String operator+(const String&a,const String&b){return a.text+b.text;}
 friend bool operator==(const String&a,const String&b){return a.text==b.text;}
 friend bool operator!=(const String&a,const String&b){return !(a==b);}
};
inline uint32_t nowMs=0;inline uint32_t millis(){return nowMs;}
inline uint32_t esp_random(){static uint32_t n=123;n=n*1664525+1013904223;return n;}
struct FakeEsp {uint64_t getEfuseMac(){return 0x123456;}};inline FakeEsp ESP;
struct FakeSerial {template<class... T>void printf(const char*,T...) {}};inline FakeSerial Serial;
class IPAddress {
 uint32_t n;
public:
 IPAddress(int a=0,int b=0,int c=0,int d=0):n(uint32_t(a)|(uint32_t(b)<<8)|(uint32_t(c)<<16)|(uint32_t(d)<<24)){}
 operator uint32_t()const{return n;}
};

#ifdef _WIN32
#include <time.h>
inline tm *localtime_r(const time_t *clock,tm *out){return localtime_s(out,clock)?nullptr:out;}
#endif
