#include "ghvcodec9.h"
#include <iostream>
#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace ghvc9;
struct Counts {
    uint64_t last=0,run=0,level=0,pad=0,alternative=0,short_levels=0,tokens=0;
};
static size_t position(const BitReader& b) {return b.p*8-size_t(b.bits);}
static void tally(const std::vector<uint8_t>& data,size_t start,size_t end,
                  size_t desc,unsigned first,unsigned limit,bool intra,Counts& c) {
    BitReader b(data,start,end);
    const unsigned flags=intra?0:data[7];
    for(unsigned i=first;i<limit;++i) {
        if(intra?!intra_has_coefficients(data,i):bool(data[desc+i/8]&(1u<<(i%8)))) continue;
        auto mark=position(b);
        int last=(flags&2) && !b.get(1)?0:int(b.get(6));
        c.last+=position(b)-mark;
        for(int pos=0;pos<=last;) {
            mark=position(b);
            unsigned run=get_run_bits(b);
            c.run+=position(b)-mark;
            if(run>unsigned(last-pos)) throw std::runtime_error("coefficient overrun");
            bool dc=pos==0 && run==0;
            int k=intra?(dc?4:2):((flags&4)?(dc?3:1):2);
            mark=position(b);
            unsigned z=(intra||(flags&8))?read_compact_level(b,k):get_level_bits(b,k);
            c.level+=position(b)-mark;
            c.alternative+=compact_level_cost(z,dc?3:0);
            ++c.tokens;
            if(z<=16) ++c.short_levels;
            pos+=run+1;
        }
    }
    if(b.p!=end || b.bits>=8 || b.acc) throw std::runtime_error("chunk padding mismatch");
    c.pad+=b.bits;
}
int main() {
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);
#endif
    try {
        uint32_t n;
        while(std::cin.read(reinterpret_cast<char*>(&n),4)) {
            std::vector<uint8_t> d(n);
            if(!std::cin.read(reinterpret_cast<char*>(d.data()),n)||n<16) return 2;
            Counts c;
            auto blocks=ghvc7::le32(d.data()+12);
            if(std::equal(IMAGIC,IMAGIC+4,d.begin())) {
                tally(d,16+(size_t(blocks)*3+7)/8,n,16,0,blocks,true,c);
            } else {
                if(n<24||!std::equal(BMAGIC,BMAGIC+4,d.begin())) return 2;
                size_t desc=24+size_t(ghvc7::le32(d.data()+20));
                size_t table=desc+(size_t(blocks)+7)/8;
                if(table+4>n) return 2;
                auto chunks=ghvc7::le32(d.data()+table);
                size_t pos=table+4+4ull*chunks;
                if(pos>n||chunks!=(size_t(blocks)+255)/256) return 2;
                for(unsigned i=0;i<chunks;++i) {
                    size_t end=pos+ghvc7::le32(d.data()+table+4+i*4);
                    if(end>n) return 2;
                    tally(d,pos,end,desc,i*256,std::min(blocks,(i+1)*256),false,c);
                    pos=end;
                }
                if(pos!=n) return 2;
            }
            std::cout<<c.last<<' '<<c.run<<' '<<c.level<<' '<<c.pad<<' '<<c.alternative<<' '<<c.short_levels<<' '<<c.tokens<<std::endl;
        }
    } catch(const std::exception& e) { std::cerr<<e.what(); return 1; }
    return 0;
}
