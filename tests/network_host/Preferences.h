#pragma once
#include "Arduino.h"
#include <vector>
#include <map>
struct Preferences {
 std::map<std::string,std::vector<uint8_t>> data;bool failWrite=false,corruptNextRead=false,corruptAfterWrite=false;
 bool isKey(const char *key){return data.count(key);}
 size_t getBytesLength(const char *key){return data.count(key)?data[key].size():0;}
 size_t getBytes(const char *key,void *p,size_t n){if(!data.count(key))return 0;auto &bytes=data[key];n=std::min(n,bytes.size());memcpy(p,bytes.data(),n);if(corruptNextRead&&n){((uint8_t*)p)[0]^=1;corruptNextRead=false;}return n;}
 size_t putBytes(const char *key,const void *p,size_t n){if(failWrite)return 0;data[key]=std::vector<uint8_t>((const uint8_t*)p,(const uint8_t*)p+n);if(corruptAfterWrite){corruptNextRead=true;corruptAfterWrite=false;}return n;}
 bool remove(const char *key){if(failWrite)return false;data.erase(key);return true;}
};
