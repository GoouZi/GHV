#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// IEEE CRC-32 used by every historical GHV video-frame record.  Slicing by
// eight preserves the on-disk checksum while avoiding a table dependency for
// every individual byte on large 1080p/4K reconstructed frames.
namespace ghvcrc {

struct Tables {
    uint32_t t[8][256]{};
    Tables(){
        for(uint32_t i=0;i<256;i++){
            uint32_t c=i;
            for(int j=0;j<8;j++)c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1);
            t[0][i]=c;
        }
        for(int n=1;n<8;n++)for(uint32_t i=0;i<256;i++)
            t[n][i]=t[0][t[n-1][i]&255]^(t[n-1][i]>>8);
    }
};

inline uint32_t compute(const uint8_t* p,size_t n){
    static const Tables tables;
    const auto& t=tables.t;uint32_t c=0xFFFFFFFFu;
    while(n>=8){
        uint32_t a,b;std::memcpy(&a,p,4);std::memcpy(&b,p+4,4);a^=c;
        c=t[7][a&255]^t[6][(a>>8)&255]^t[5][(a>>16)&255]^t[4][a>>24]
         ^t[3][b&255]^t[2][(b>>8)&255]^t[1][(b>>16)&255]^t[0][b>>24];
        p+=8;n-=8;
    }
    while(n--){c=t[0][(c^*p++)&255]^(c>>8);}
    return c^0xFFFFFFFFu;
}

} // namespace ghvcrc
