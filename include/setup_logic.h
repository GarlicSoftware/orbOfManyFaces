#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <cstring>

namespace orbsetup {
struct Zone {const char *label,*rule;};
// Explicit regional rules; never infer a region from a legacy fixed offset.
constexpr Zone zones[]={
 {"UTC","UTC0"},
 {"New York / Toronto","EST5EDT,M3.2.0,M11.1.0"},
 {"Chicago / Winnipeg","CST6CDT,M3.2.0,M11.1.0"},
 {"Denver / Edmonton","MST7MDT,M3.2.0,M11.1.0"},
 {"Los Angeles (DST)","PST8PDT,M3.2.0,M11.1.0"},
 {"Arizona / Yukon / Vancouver","MST7"},
 {"Saskatchewan (no DST)","CST6"},
 {"Alaska","AKST9AKDT,M3.2.0,M11.1.0"},
 {"Hawaii (no DST)","HST10"},
 {"Canada Atlantic","AST4ADT,M3.2.0,M11.1.0"},
 {"Newfoundland","NST3:30NDT,M3.2.0,M11.1.0"},
 {"UK / Ireland","GMT0BST,M3.5.0/1,M10.5.0"},
 {"Central Europe","CET-1CEST,M3.5.0,M10.5.0/3"},
 {"Eastern Europe","EET-2EEST,M3.5.0/3,M10.5.0/4"},
 {"Japan / Korea","JST-9"},
 {"China / Singapore","CST-8"},
 {"India","IST-5:30"},
 {"Australia Sydney/Melbourne","AEST-10AEDT,M10.1.0,M4.1.0/3"},
 {"Australia Brisbane","AEST-10"},
 {"Australia Adelaide","ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
 {"Australia Darwin","ACST-9:30"},
 {"Australia Perth","AWST-8"},
 {"New Zealand","NZST-12NZDT,M9.5.0,M4.1.0/3"}
};
constexpr int zoneCount=sizeof(zones)/sizeof(zones[0]);
inline int zoneIndex(const char *rule){for(int i=0;i<zoneCount;++i)if(!strcmp(rule,zones[i].rule))return i;return -1;}
inline bool alpha(char c){return(c>='A'&&c<='Z')||(c>='a'&&c<='z');}
inline bool digit(char c){return c>='0'&&c<='9';}
inline bool number(const char *&p,int min,int max){if(!digit(*p))return false;int n=0;while(digit(*p)){n=n*10+(*p++-'0');if(n>max)return false;}return n>=min;}
inline bool abbreviation(const char *&p){int n=0;if(*p=='<'){++p;while(alpha(*p)||digit(*p)||*p=='+'||*p=='-'){++n;++p;}if(*p++!='>')return false;}else while(alpha(*p)){++n;++p;}return n>=3;}
inline bool offset(const char *&p,int max){if(*p=='+'||*p=='-')++p;if(!number(p,0,max))return false;for(int i=0;i<2&&*p==':';++i){++p;if(!number(p,0,59))return false;}return true;}
inline bool transition(const char *&p){
 if(*p=='M'){++p;if(!number(p,1,12)||*p++!='.'||!number(p,1,5)||*p++!='.'||!number(p,0,6))return false;}
 else if(*p=='J'){++p;if(!number(p,1,365))return false;}
 else if(!number(p,0,365))return false;
 if(*p=='/'){++p;if(!offset(p,167))return false;}return true;
}
inline bool validZone(const std::string &s){
 if(s.empty()||s.size()>80||s.find('\0')!=std::string::npos)return false;
 const char *p=s.c_str();if(!abbreviation(p)||!offset(p,24))return false;if(!*p)return true;
 if(!abbreviation(p))return false;if(*p!=','&&!offset(p,24))return false;
 if(*p++!=','||!transition(p)||*p++!=','||!transition(p))return false;return !*p;
}
inline bool fixedZone(const std::string &s){if(s.empty()||s.size()>80||s.find('\0')!=std::string::npos)return false;const char *p=s.c_str();return abbreviation(p)&&offset(p,24)&&!*p;}
inline bool validNetwork(const std::string &ssid,const std::string &password,bool open){
 if(ssid.empty()||ssid.size()>32||ssid.find('\0')!=std::string::npos||password.find('\0')!=std::string::npos)return false;
 if(open)return password.empty();if(password.size()==64){for(char c:password)if(!digit(c)&&!(c>='a'&&c<='f')&&!(c>='A'&&c<='F'))return false;return true;}
 return password.size()>=8&&password.size()<=63;
}
inline std::string escape(const std::string &s){std::string out;for(char c:s){switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&#39;";break;default:if(uint8_t(c)>=32||c=='\n')out+=c;}}return out;}
inline uint32_t crc(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
struct Network {std::string ssid,password;bool open=false;};
inline std::vector<uint8_t> encode(const Network &n){
 if(!validNetwork(n.ssid,n.password,n.open))return {};
 std::vector<uint8_t> b={'O','R','B','W',1,uint8_t(n.ssid.size()),uint8_t(n.password.size()),uint8_t(n.open)};
 b.insert(b.end(),n.ssid.begin(),n.ssid.end());b.insert(b.end(),n.password.begin(),n.password.end());uint32_t c=crc(b.data(),b.size());for(int i=0;i<4;++i)b.push_back(c>>(i*8));return b;
}
inline bool decode(const uint8_t *b,size_t size,Network &n){
 if(size<12||size>108||memcmp(b,"ORBW",4)||b[4]!=1||b[7]>1||size!=size_t(12+b[5]+b[6]))return false;
 uint32_t c=0;for(int i=0;i<4;++i)c|=uint32_t(b[size-4+i])<<(i*8);if(c!=crc(b,size-4))return false;
 Network next;next.ssid.assign((const char*)b+8,b[5]);next.password.assign((const char*)b+8+b[5],b[6]);next.open=b[7];if(!validNetwork(next.ssid,next.password,next.open))return false;n=next;return true;
}
}
