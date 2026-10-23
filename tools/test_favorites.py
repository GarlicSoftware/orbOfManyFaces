"""Exercise the actual C++ serializer and RAM partitioner with all public faces."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "face_serialization.h"
#include "favorites_logic.h"
#include "group_format.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
struct Memory:clockpack::Output {
 std::vector<uint8_t> data;
 void bytes(const uint8_t *p,size_t n)override{if(n)data.insert(data.end(),p,p+n);}
};
void roundtrip(const Face &face){
 Memory original;clockpack::writeFace(original,face);
 Memory body;body.str("Favorites",60);body.bytes(original.data.data(),original.data.size());
 Memory pack;pack.bytes((const uint8_t*)"ORBGRP01",8);pack.u32(1);pack.u32(body.data.size());pack.u32(clockpack::crc32(body.data.data(),body.data.size()));pack.u32(clockpack::decodedBytes(face));pack.bytes(body.data.data(),body.data.size());
 auto group=clockpack::parse(pack.data.data(),pack.data.size());assert(group->faces.size()==1);
 Memory again;clockpack::writeFace(again,group->faces[0]);assert(original.data==again.data);
 assert(clockpack::decodedBytes(group->faces[0])==clockpack::decodedBytes(face));
 assert(clockpack::metadataBytes(group->faces[0])==clockpack::metadataBytes(face));
}
int main(int argc,char **argv){
 unsigned tested=0;
 for(auto &face:faces){roundtrip(face);++tested;}
 for(int i=1;i<argc;++i){std::ifstream input(argv[i],std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());auto group=clockpack::parse(data.data(),data.size());for(auto &face:group->faces){roundtrip(face);++tested;}}
 assert(tested==42);
 using favorites::Size;using favorites::partition;
 constexpr uint32_t ram=6*1024*1024,heap=180000;
 std::vector<Size> small(24,{1000,500000,1000});assert(partition(small,ram,heap).size()==1);
 small.resize(33,{1000,500000,1000});assert(partition(small,ram,heap).size()==2); // format bound only
 assert(partition(std::vector<Size>(3,{1800000,500000,1000}),ram,heap).size()==3); // compressed bytes
 assert(partition(std::vector<Size>(2,{40000,3600000,1000}),ram,heap).size()==2); // decoded bytes
 assert(partition(std::vector<Size>(2,{1000,500000,80000}),ram,heap).size()==2); // font/metadata heap
 assert(partition(std::vector<Size>(4,{1200000,500000,1000}),ram,heap).size()==2);
 assert(partition(std::vector<Size>(2,{1200000,500000,1000}),ram,heap).size()==1); // consolidate
 assert(partition({},ram,heap).empty());
 bool rejected=false;try{partition({{3200000,500000,1000}},ram,heap);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
 assert(favorites::needsPrompt(3,2,0));assert(!favorites::needsPrompt(3,2,3));assert(favorites::needsPrompt(4,2,3));assert(!favorites::needsPrompt(4,4,3));
 const char *expected[]={"Favorites","More Favorites","More More Favorites","Too Many Favorites","More Too Many Favorites","We Need to Talk"};for(int i=0;i<6;++i)assert(favorites::groupName(i)==expected[i]);for(int i=6;i<512;++i)assert(favorites::groupName(i).size()<=60);
 std::printf("PASS: %u real faces round-trip through standard .ocg serialization\n",tested);
 std::puts("PASS: compressed/decoded/font memory splitting, format bound, consolidation, empty favorites, scoped deferral and whimsical names");
}
'''


def main():
    env = os.environ.copy()
    with tempfile.TemporaryDirectory(prefix='orb-favorites-test-') as temporary:
        work = Path(temporary)
        (work / 'test.cpp').write_text(SOURCE, encoding='utf-8')
        (work / 'Arduino.h').write_text('#pragma once\n#define PROGMEM\n', encoding='utf-8')
        if os.name == 'nt':
            vcvars = Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat')
            output = subprocess.check_output(f'cmd.exe /s /c ""{vcvars}" >nul && set"', text=True)
            env = {key.upper(): value for key, value in env.items()}
            for line in output.splitlines():
                if '=' in line and not line.startswith('='):
                    key, value = line.split('=', 1)
                    env[key.upper()] = value
            compiler = Path(env['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
            command = [str(compiler), '/nologo', '/EHsc', '/std:c++17', '/I' + str(ROOT / 'include'), '/I' + str(work), 'test.cpp', str(ROOT / 'src/assets.cpp'), '/Fe:test.exe']
            executable = work / 'test.exe'
        else:
            command = [shutil.which('g++') or 'c++', '-std=c++17', '-I' + str(ROOT / 'include'), '-I' + str(work), 'test.cpp', str(ROOT / 'src/assets.cpp'), '-o', 'test']
            executable = work / 'test'
        subprocess.run(command, cwd=work, env=env, check=True)
        subprocess.run([str(executable), *map(str, sorted((ROOT / 'sd-groups').glob('*.ocg')))], check=True)
        fake = ROOT / 'tests/favorites_host'
        if os.name == 'nt':
            storage = [str(compiler), '/nologo', '/EHsc', '/std:c++17', '/I' + str(fake), '/I' + str(ROOT / 'include'), str(fake / 'main.cpp'), str(ROOT / 'src/favorites.cpp'), str(ROOT / 'src/assets.cpp'), '/Fe:storage.exe']
            executable = work / 'storage.exe'
        else:
            storage = [shutil.which('g++') or 'c++', '-std=c++17', '-I' + str(fake), '-I' + str(ROOT / 'include'), str(fake / 'main.cpp'), str(ROOT / 'src/favorites.cpp'), str(ROOT / 'src/assets.cpp'), '-o', 'storage']
            executable = work / 'storage'
        subprocess.run(storage, cwd=work, env=env, check=True)
        subprocess.run([str(executable), str(work / 'card')], check=True)



if __name__ == '__main__':
    main()
