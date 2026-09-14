#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

// GHVC7 transform codec shared by the native encoder and decoder.
// This is intentionally dependency-free and scalar.  The bitstream is our own:
// 8x8 sequency-ordered integer WHT, frequency-aware quantization, 3-bit block
// descriptors, zig-zag scan, trailing-zero removal, and run/level varints.
namespace ghvc7 {

static constexpr int BS = 8;
static constexpr uint8_t MAGIC[4] = {'G','T','C','7'};
static constexpr int ORDER[8] = {0,4,6,2,3,7,5,1};
static constexpr int ZIGZAG[64] = {
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};

inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i=0;i<4;i++) out.push_back(uint8_t(v >> (8*i)));
}
inline void put_var(std::vector<uint8_t>& out, uint32_t v) {
    while (v >= 0x80) { out.push_back(uint8_t(v) | 0x80); v >>= 7; }
    out.push_back(uint8_t(v));
}
inline uint32_t get_var(const std::vector<uint8_t>& in, size_t& p) {
    uint32_t v=0; int shift=0;
    for (int n=0;n<5;n++) {
        if (p>=in.size()) throw std::runtime_error("truncated GHVC7 varint");
        uint8_t b=in[p++]; v |= uint32_t(b & 0x7f) << shift;
        if (!(b & 0x80)) return v;
        shift += 7;
    }
    throw std::runtime_error("invalid GHVC7 varint");
}
inline uint32_t zig(int v) { return v < 0 ? uint32_t(-2ll*v-1) : uint32_t(2ll*v); }
inline int unzig(uint32_t v) { return (v & 1) ? -int((v+1)>>1) : int(v>>1); }
inline int div_round(int v, int d) {
    return v < 0 ? -((-v + d/2)/d) : (v + d/2)/d;
}
inline int clamp8(int v) { return std::max(0, std::min(255, v)); }

inline void fwht8(int* a) {
    for (int len=1;len<8;len<<=1)
        for (int i=0;i<8;i+=len*2)
            for (int j=0;j<len;j++) {
                int x=a[i+j], y=a[i+j+len];
                a[i+j]=x+y; a[i+j+len]=x-y;
            }
}
inline void transform(const int16_t* src, int* coeff) {
    int tmp[64], row[8], natural[64];
    for (int y=0;y<8;y++) {
        for (int x=0;x<8;x++) row[x]=src[y*8+x];
        fwht8(row);
        for (int u=0;u<8;u++) tmp[y*8+u]=row[u];
    }
    for (int u=0;u<8;u++) {
        for (int y=0;y<8;y++) row[y]=tmp[y*8+u];
        fwht8(row);
        for (int v=0;v<8;v++) natural[v*8+u]=row[v];
    }
    for (int v=0;v<8;v++) for (int u=0;u<8;u++)
        coeff[v*8+u]=natural[ORDER[v]*8+ORDER[u]];
}
inline void inverse(const int* coeff, int16_t* dst) {
    int natural[64]={0}, tmp[64], row[8];
    for (int v=0;v<8;v++) for (int u=0;u<8;u++)
        natural[ORDER[v]*8+ORDER[u]]=coeff[v*8+u];
    for (int u=0;u<8;u++) {
        for (int v=0;v<8;v++) row[v]=natural[v*8+u];
        fwht8(row);
        for (int y=0;y<8;y++) tmp[y*8+u]=row[y];
    }
    for (int y=0;y<8;y++) {
        for (int u=0;u<8;u++) row[u]=tmp[y*8+u];
        fwht8(row);
        for (int x=0;x<8;x++) dst[y*8+x]=int16_t(div_round(row[x],64));
    }
}

inline int base_quant(int quality) {
    quality=std::clamp(quality,1,100);
    if (quality>=96) return 1;
    if (quality>=88) return 2;
    if (quality>=82) return 3;
    if (quality>=79) return 4;
    if (quality>=75) return 5;
    if (quality>=70) return 7;
    if (quality>=62) return 9;
    return 13;
}
inline int qstep(int quality, int plane, int u, int v) {
    int base=base_quant(quality), freq=u+v;
    int step=(base*8*(16+2*freq+(freq>=8?4:0))+8)/16;
    if (plane>0) step=(step*5+2)/4;
    return std::max(1,step);
}
inline uint32_t block_count(int w,int h) {
    int cw=w/2,ch=h/2;
    return uint32_t(((w+7)/8)*((h+7)/8) + 2*((cw+7)/8)*((ch+7)/8));
}

inline int intra_dc(const std::vector<uint8_t>& recon,size_t base,int pw,int ph,int x0,int y0) {
    int sum=0,n=0;
    if (y0>0) for(int x=0;x<8 && x0+x<pw;x++){sum+=recon[base+size_t(y0-1)*pw+x0+x];n++;}
    if (x0>0) for(int y=0;y<8 && y0+y<ph;y++){sum+=recon[base+size_t(y0+y)*pw+x0-1];n++;}
    return n ? (sum+n/2)/n : 128;
}
inline int prediction(uint8_t mode,const std::vector<uint8_t>* prev,const std::vector<uint8_t>& recon,
                      size_t base,int pw,int ph,int x0,int y0,int x,int y,int dc) {
    size_t at=base+size_t(y0+y)*pw+x0+x;
    if (mode==0) return (*prev)[at];
    if (mode==1) return dc;
    if (mode==2) return y0>0 ? recon[base+size_t(y0-1)*pw+x0+x] : 128;
    return x0>0 ? recon[base+size_t(y0+y)*pw+x0-1] : 128;
}
inline size_t var_size(uint32_t v) { size_t n=1; while(v>=0x80){v>>=7;n++;} return n; }

struct Candidate {
    uint8_t mode=0;
    std::array<int,64> q{};
    size_t bytes=0;
    int last=-1;
};

inline Candidate make_candidate(const std::vector<uint8_t>& frame,const std::vector<uint8_t>* prev,
                                const std::vector<uint8_t>& recon,size_t base,int pw,int ph,
                                int x0,int y0,int plane,int quality,uint8_t mode) {
    Candidate c; c.mode=mode; int16_t residual[64]; int coeff[64];
    int dc=intra_dc(recon,base,pw,ph,x0,y0);
    for(int y=0;y<8;y++) for(int x=0;x<8;x++) {
        int sx=std::min(x0+x,pw-1), sy=std::min(y0+y,ph-1);
        int pred=prediction(mode,prev,recon,base,pw,ph,x0,y0,
                            std::min(x,pw-1-x0),std::min(y,ph-1-y0),dc);
        residual[y*8+x]=int16_t(int(frame[base+size_t(sy)*pw+sx])-pred);
    }
    transform(residual,coeff);
    for(int v=0;v<8;v++) for(int u=0;u<8;u++)
        c.q[v*8+u]=div_round(coeff[v*8+u],qstep(quality,plane,u,v));
    for(int i=63;i>=0;i--) if(c.q[ZIGZAG[i]]!=0){c.last=i;break;}
    if(c.last<0){c.bytes=0;return c;}
    c.bytes=1; int pos=0;
    while(pos<=c.last){int run=0;while(pos<=c.last&&c.q[ZIGZAG[pos]]==0){run++;pos++;}
        c.bytes+=var_size(uint32_t(run))+var_size(zig(c.q[ZIGZAG[pos]]));pos++;}
    return c;
}

inline void reconstruct_block(const Candidate& c,const std::vector<uint8_t>* prev,
                              std::vector<uint8_t>& recon,size_t base,int pw,int ph,
                              int x0,int y0,int plane,int quality) {
    int coeff[64];int16_t residual[64];
    for(int v=0;v<8;v++)for(int u=0;u<8;u++)coeff[v*8+u]=c.q[v*8+u]*qstep(quality,plane,u,v);
    inverse(coeff,residual);int dc=intra_dc(recon,base,pw,ph,x0,y0);
    for(int y=0;y<8&&y0+y<ph;y++)for(int x=0;x<8&&x0+x<pw;x++){
        int pred=prediction(c.mode,prev,recon,base,pw,ph,x0,y0,x,y,dc);
        recon[base+size_t(y0+y)*pw+x0+x]=uint8_t(clamp8(pred+residual[y*8+x]));
    }
}

inline std::vector<uint8_t> encode(const std::vector<uint8_t>& frame,const std::vector<uint8_t>* prev,
                                   int w,int h,int quality,bool intra,std::vector<uint8_t>& recon,
                                   uint64_t* zero_blocks=nullptr) {
    uint32_t blocks=block_count(w,h); size_t desc_bytes=(size_t(blocks)*3+7)/8;
    std::vector<uint8_t> desc(desc_bytes,0), body; body.reserve(frame.size()/8);
    recon.assign(frame.size(),0); uint32_t bi=0;size_t base=0;
    auto plane_fn=[&](int pw,int ph,int plane){
        int nx=(pw+7)/8,ny=(ph+7)/8,total=nx*ny;
        auto emit=[&](const Candidate& best){
            uint8_t d=uint8_t(best.mode | (best.last<0?4:0));size_t bit=size_t(bi)*3;
            desc[bit>>3]|=uint8_t(d<<(bit&7));if((bit&7)>5)desc[(bit>>3)+1]|=uint8_t(d>>(8-(bit&7)));
            if(best.last<0){if(zero_blocks)(*zero_blocks)++;}
            else {body.push_back(uint8_t(best.last));int pos=0;while(pos<=best.last){int run=0;while(pos<=best.last&&best.q[ZIGZAG[pos]]==0){run++;pos++;}put_var(body,uint32_t(run));put_var(body,zig(best.q[ZIGZAG[pos]]));pos++;}}
            bi++;
        };
        if(!intra){
            std::vector<Candidate> cv(static_cast<size_t>(total));
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(total>128)
#endif
            for(int i=0;i<total;i++){int x0=(i%nx)*8,y0=(i/nx)*8;cv[size_t(i)]=make_candidate(frame,prev,recon,base,pw,ph,x0,y0,plane,quality,0);}
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(total>128)
#endif
            for(int i=0;i<total;i++){int x0=(i%nx)*8,y0=(i/nx)*8;reconstruct_block(cv[size_t(i)],prev,recon,base,pw,ph,x0,y0,plane,quality);}
            for(const auto& c:cv)emit(c);
        }else{
            for(int y0=0;y0<ph;y0+=8)for(int x0=0;x0<pw;x0+=8){
                Candidate best=make_candidate(frame,nullptr,recon,base,pw,ph,x0,y0,plane,quality,1);
                for(uint8_t m=2;m<=3;m++){Candidate c=make_candidate(frame,nullptr,recon,base,pw,ph,x0,y0,plane,quality,m);if(c.bytes<best.bytes)best=c;}
                reconstruct_block(best,nullptr,recon,base,pw,ph,x0,y0,plane,quality);emit(best);
            }
        }
        base+=size_t(pw)*ph;
    };
    plane_fn(w,h,0);plane_fn(w/2,h/2,1);plane_fn(w/2,h/2,2);
    std::vector<uint8_t> out;out.reserve(16+desc.size()+body.size());
    out.insert(out.end(),MAGIC,MAGIC+4);out.push_back(8);out.push_back(uint8_t(quality));out.push_back(0);out.push_back(0);
    put32(out,uint32_t(frame.size()));put32(out,blocks);out.insert(out.end(),desc.begin(),desc.end());out.insert(out.end(),body.begin(),body.end());
    return out;
}

inline void decode(const std::vector<uint8_t>& in,const std::vector<uint8_t>* prev,int w,int h,
                   int frame_type,size_t expected,std::vector<uint8_t>& recon) {
    if(in.size()<16||!std::equal(MAGIC,MAGIC+4,in.begin())||in[4]!=8||in[6]!=0||in[7]!=0)
        throw std::runtime_error("bad GHVC7 payload");
    int quality=in[5];uint32_t raw=le32(in.data()+8),blocks=le32(in.data()+12),want=block_count(w,h);
    if(raw!=expected||blocks!=want||quality<1||quality>100)throw std::runtime_error("invalid GHVC7 header");
    size_t desc_bytes=(size_t(blocks)*3+7)/8,p=16+desc_bytes;if(p>in.size())throw std::runtime_error("truncated GHVC7 descriptors");
    recon.assign(expected,0);uint32_t bi=0;size_t base=0;
    auto plane_fn=[&](int pw,int ph,int plane){
        int nx=(pw+7)/8,ny=(ph+7)/8,total=nx*ny;
        auto read_candidate=[&](){
            size_t bit=size_t(bi)*3;uint16_t bits=in[16+(bit>>3)];if((bit&7)>5)bits|=uint16_t(in[17+(bit>>3)])<<8;
            uint8_t d=uint8_t((bits>>(bit&7))&7),mode=d&3;bool zero=d&4;
            if((frame_type==1&&mode!=0)||(frame_type==0&&(mode<1||mode>3)))throw std::runtime_error("bad GHVC7 predictor");
            Candidate c;c.mode=mode;c.last=-1;
            if(!zero){if(p>=in.size())throw std::runtime_error("truncated GHVC7 block");c.last=in[p++];if(c.last>63)throw std::runtime_error("bad GHVC7 last coefficient");int pos=0;
                while(pos<=c.last){uint32_t run=get_var(in,p),level=get_var(in,p);if(level==0||level>(1u<<20)||run>uint32_t(c.last-pos))throw std::runtime_error("bad GHVC7 run/level");pos+=int(run);c.q[ZIGZAG[pos]]=unzig(level);pos++;}}
            bi++;return c;
        };
        if(frame_type==1){
            std::vector<Candidate> cv(static_cast<size_t>(total));
            for(int i=0;i<total;i++)cv[size_t(i)]=read_candidate();
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(total>128)
#endif
            for(int i=0;i<total;i++){int x0=(i%nx)*8,y0=(i/nx)*8;reconstruct_block(cv[size_t(i)],prev,recon,base,pw,ph,x0,y0,plane,quality);}
        }else{
            for(int y0=0;y0<ph;y0+=8)for(int x0=0;x0<pw;x0+=8){Candidate c=read_candidate();reconstruct_block(c,nullptr,recon,base,pw,ph,x0,y0,plane,quality);}
        }
        base+=size_t(pw)*ph;
    };
    if(frame_type==1&&!prev)throw std::runtime_error("GHVC7 P frame without reference");
    plane_fn(w,h,0);plane_fn(w/2,h/2,1);plane_fn(w/2,h/2,2);
    if(p!=in.size())throw std::runtime_error("trailing GHVC7 payload bytes");
}

} // namespace ghvc7
