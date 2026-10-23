#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <memory>
#define FILE_WRITE "w"
inline std::filesystem::path fakeRoot;
inline bool fakeMounted=true;
inline std::string failWritePath;
inline int failWriteRemaining=-1;
class File {
 std::shared_ptr<std::fstream> stream;std::filesystem::path path;
public:
 File()=default;
 File(std::filesystem::path p,bool write):path(p){stream=std::make_shared<std::fstream>(p,std::ios::binary|(write?std::ios::in|std::ios::out|std::ios::trunc:std::ios::in));}
 explicit operator bool()const{return stream&&stream->is_open();}
 size_t size(){return bool(*this)?std::filesystem::file_size(path):0;}
 size_t read(uint8_t *p,size_t n){if(!*this)return 0;stream->read((char*)p,n);return stream->gcount();}
 size_t write(const uint8_t*p,size_t n){if(!*this)return 0;if(!failWritePath.empty()&&path.string().find(failWritePath)!=std::string::npos&&failWriteRemaining>=0){n=std::min(n,size_t(failWriteRemaining));failWriteRemaining-=n;}stream->write((const char*)p,n);return stream->good()?n:0;}
 bool seek(size_t offset){stream->clear();stream->seekp(offset);return stream->good();}
 bool available(){return bool(*this)&&stream->tellg()>=0&&size_t(stream->tellg())<size();}
 void flush(){if(*this)stream->flush();}
 void close(){if(*this)stream->close();}
};
struct FakeSD {
 std::filesystem::path resolve(const String &name){std::string value=name.c_str();if(!value.empty()&&value[0]=='/')value.erase(0,1);return fakeRoot/value;}
 File open(const String &name,const char *mode="r"){return fakeMounted?File(resolve(name),mode[0]=='w'):File();}
 bool exists(const String &name){return fakeMounted&&std::filesystem::exists(resolve(name));}
 bool mkdir(const String &name){return fakeMounted&&std::filesystem::create_directory(resolve(name));}
 bool remove(const String &name){return fakeMounted&&std::filesystem::remove(resolve(name));}
 bool rmdir(const String &name){std::error_code error;return fakeMounted&&std::filesystem::remove(resolve(name),error);}
};inline FakeSD SD;
