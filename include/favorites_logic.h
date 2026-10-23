#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace favorites {
struct Size {uint32_t packed=0,decoded=0,metadata=0;};
struct Bin {
    std::vector<unsigned> items;
    uint32_t packed=64,largest=0,second=0,metadata=1024;
    void add(unsigned i,const Size &s){items.push_back(i);packed+=s.packed;metadata+=s.metadata;if(s.decoded>largest){second=largest;largest=s.decoded;}else if(s.decoded>second)second=s.decoded;}
    bool fits(const Size &s,uint32_t ram,uint32_t heap)const {
        Bin b=*this;b.add(0,s);
        // 32 is the existing .ocg format's hard bound, not a grouping preference.
        return b.items.size()<=32&&b.packed<=3*1024*1024&&uint64_t(b.packed)+b.largest+b.second+262144<=ram&&b.metadata+49152<=heap;
    }
};
inline std::vector<Bin> partition(const std::vector<Size> &sizes,uint32_t ram,uint32_t heap){
    std::vector<unsigned> order;for(unsigned i=0;i<sizes.size();++i)order.push_back(i);
    std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return sizes[a].packed>sizes[b].packed;});
    std::vector<Bin> bins;
    for(unsigned i:order){
        int best=-1;uint32_t used=0;
        for(unsigned b=0;b<bins.size();++b)if(bins[b].fits(sizes[i],ram,heap)&&bins[b].packed>=used){best=b;used=bins[b].packed;}
        if(best<0){Bin empty;if(!empty.fits(sizes[i],ram,heap))throw std::runtime_error("Favorite exceeds available RAM / pack limit");bins.push_back(empty);best=bins.size()-1;}
        bins[best].add(i,sizes[i]);
    }
    return bins;
}
inline std::string groupName(unsigned index){
    static const char *names[]={"Favorites","More Favorites","More More Favorites","Too Many Favorites","More Too Many Favorites","We Need to Talk","This Is Getting Out of Hand","The Favorites Have Favorites","A Perfectly Reasonable Collection","Definitely the Last Favorites","We Said That Last Time","Send Help","The Favorites Have Formed a Committee"};
    if(index<sizeof(names)/sizeof(*names))return names[index];
    return "The Committee Requests More Favorites " + std::to_string(index+1);
}
// Revision-scoped deferral: additional changes permit a fresh prompt.
inline bool needsPrompt(uint32_t revision,uint32_t built,uint32_t deferred){return revision!=built&&revision!=deferred;}
}
