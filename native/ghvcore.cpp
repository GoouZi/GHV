#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include "ghvcodec7.h"
#include "ghvcodec8.h"
#include "ghvcrc.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// GHVC6/GHVC7 native encoder core.
// Pure in-tree codec: no libav, zlib, VPx, x264, etc.
//
// 0.6 speed work:
//   * GBP6 stores independent 256-block chunks, so packing is parallelizable.
//   * P residual delta is local to 64-byte blocks, removing cross-block dependency.
//   * range-0 P frames bypass the motion grid entirely.
//   * quantize/residual/reconstruction loops use OpenMP for HD frames.
//   * direct GHV mux mode removes Python from per-frame video writes.

static constexpr uint32_t BLOCK = 64;
static constexpr uint16_t CHUNK_BLOCKS = 256;
static constexpr uint8_t CHF_DESC_RLE = 1;
static constexpr uint8_t CHF_BLOCK_DELTA = 2;

static void write_u16(std::ostream& o,uint16_t v){ char b[2]={char(v),char(v>>8)};o.write(b,2); }
static void write_u32(std::ostream& o,uint32_t v){ char b[4]={char(v),char(v>>8),char(v>>16),char(v>>24)};o.write(b,4); }
static void write_u64(std::ostream& o,uint64_t v){ char b[8];for(int i=0;i<8;i++)b[i]=char(v>>(8*i));o.write(b,8); }

static std::pair<int,int> quality_steps(int q){
    q=std::clamp(q,1,100);
    if(q>=97)return{1,1}; if(q>=92)return{1,2}; if(q>=86)return{2,3}; if(q>=78)return{2,4};
    if(q>=70)return{3,5}; if(q>=62)return{4,7}; if(q>=54)return{5,8}; if(q>=46)return{6,10}; return{8,12};
}
static int residual_deadzone(int q){
    q=std::clamp(q,1,100); if(q>=86)return 0; if(q>=78)return 1; if(q>=70)return 2; if(q>=60)return 3; return 4;
}
static int residual_step(int q){q=std::clamp(q,1,100);if(q>=82)return 1;if(q>=76)return 2;if(q>=68)return 3;return 4;}
static double repeat_threshold(int q){
    q=std::clamp(q,1,100); if(q>=92)return 0.0; if(q>=84)return .12; if(q>=76)return .32; if(q>=68)return .58; return .85;
}
static inline uint8_t quant1(uint8_t x,int step){ if(step<=1)return x; int v=((int(x)+step/2)/step)*step; return uint8_t(std::min(v,255)); }
static void quantize(std::vector<uint8_t>& f,int w,int h,int q){
    auto [ys,cs]=quality_steps(q); size_t ysz=size_t(w)*h;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(ysz>400000)
#endif
    for(long long i=0;i<(long long)ysz;i++)f[size_t(i)]=quant1(f[size_t(i)],ys);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(f.size()-ysz>200000)
#endif
    for(long long i=(long long)ysz;i<(long long)f.size();i++)f[size_t(i)]=quant1(f[size_t(i)],cs);
}

static void init_crc(){}
static uint32_t crc32(const uint8_t* p,size_t n){return ghvcrc::compute(p,n);}

static double scene_score(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,int w,int h){
    uint64_t sum=0,count=0; for(int y=0;y<h;y+=16){size_t row=size_t(y)*w;for(int x=0;x<w;x+=16){sum+=uint64_t(std::abs(int(a[row+x])-int(b[row+x])));count++;}}
    return count?double(sum)/double(count):0.0;
}

static double source_repeat_score(const std::vector<uint8_t>& frame,const std::vector<uint8_t>& samples,int w,int h){
    if(samples.empty())return 1e30;uint64_t sum=0;size_t n=0;
    for(int y=0;y<h;y+=16)for(int x=0;x<w;x+=16){sum+=uint64_t(std::abs(int(frame[size_t(y)*w+x])-int(samples[n++])));}
    return n?double(sum)/double(n):1e30;
}
static void capture_source_samples(const std::vector<uint8_t>& frame,std::vector<uint8_t>& samples,int w,int h){
    samples.clear();samples.reserve(size_t((w+15)/16)*size_t((h+15)/16));
    for(int y=0;y<h;y+=16)for(int x=0;x<w;x+=16)samples.push_back(frame[size_t(y)*w+x]);
}

static double motion_score(const std::vector<uint8_t>& cur,const std::vector<uint8_t>& old,int w,int h,int dx,int dy,int search){
    const int step=std::clamp(h/54,10,24),margin=search+2;uint64_t sum=0,count=0;
    for(int y=margin;y<h-margin;y+=step){int sy=y-dy;for(int x=margin;x<w-margin;x+=step){int sx=x-dx;sum+=uint64_t(std::abs(int(cur[size_t(y)*w+x])-int(old[size_t(sy)*w+sx])));count++;}}
    return count?double(sum)/double(count):1e30;
}
static std::pair<int,int> estimate_motion(const std::vector<uint8_t>& cur,const std::vector<uint8_t>& old,int w,int h,int search,int quality,int hintx,int hinty){
    if(search<=0||w<64||h<64)return{0,0};search=std::clamp(search,0,31);
    double zero=motion_score(cur,old,w,h,0,0,search),best=zero;int bx=0,by=0;if(best<=0.5)return{0,0};
    struct C{int x,y;};std::vector<C> cand;
    auto add_local=[&](int cx,int cy,int rad){for(int dy=-rad;dy<=rad;dy++)for(int dx=-rad;dx<=rad;dx++){int x=std::clamp(cx+dx,-search,search),y=std::clamp(cy+dy,-search,search);bool dup=false;for(const auto& c:cand)if(c.x==x&&c.y==y){dup=true;break;}if(!dup)cand.push_back({x,y});}};
    add_local(std::clamp(hintx,-search,search),std::clamp(hinty,-search,search),2);add_local(0,0,1);
    std::vector<double> scores(cand.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(cand.size()>16)
#endif
    for(long long i=0;i<(long long)cand.size();i++)scores[size_t(i)]=motion_score(cur,old,w,h,cand[size_t(i)].x,cand[size_t(i)].y,search);
    for(size_t i=0;i<cand.size();i++)if(scores[i]<best){best=scores[i];bx=cand[i].x;by=cand[i].y;}
    if(best<=4.0||best<=zero*.90)return{bx,by};
    int inc=(quality>=86)?2:4;inc=std::min(inc,std::max(1,search));cand.clear();
    for(int dy=-search;dy<=search;dy+=inc)for(int dx=-search;dx<=search;dx+=inc)cand.push_back({dx,dy});
    scores.resize(cand.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(cand.size()>16)
#endif
    for(long long i=0;i<(long long)cand.size();i++)scores[size_t(i)]=motion_score(cur,old,w,h,cand[size_t(i)].x,cand[size_t(i)].y,search);
    for(size_t i=0;i<cand.size();i++)if(scores[i]<best){best=scores[i];bx=cand[i].x;by=cand[i].y;}
    int ox=bx,oy=by;for(int dy=std::max(-search,oy-1);dy<=std::min(search,oy+1);dy++)for(int dx=std::max(-search,ox-1);dx<=std::min(search,ox+1);dx++){double sc=motion_score(cur,old,w,h,dx,dy,search);if(sc<best){best=sc;bx=dx;by=dy;}}
    return{bx,by};
}

static void intra_residual(const std::vector<uint8_t>& f,std::vector<uint8_t>& r,int w,int h){
    r.resize(f.size());int cw=w/2,ch=h/2;size_t pos=0;
    auto plane=[&](int pw,int ph){size_t base=pos;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if((long long)pw*ph>250000)
#endif
        for(int y=0;y<ph;y++){size_t row=base+size_t(y)*pw;r[row]=f[row];for(int x=1;x<pw;x++)r[row+x]=uint8_t(int(f[row+x])-int(f[row+x-1]));}pos+=size_t(pw)*ph;};
    plane(w,h);plane(cw,ch);plane(cw,ch);
}
static inline int clampi(int v,int lo,int hi){return std::max(lo,std::min(v,hi));}
static void motion_residual(const std::vector<uint8_t>& f,const std::vector<uint8_t>& p,std::vector<uint8_t>& r,int w,int h,int dx,int dy,int deadzone){
    r.resize(f.size());size_t pos=0;int cw=w/2,ch=h/2;
    auto plane=[&](int pw,int ph,int mdx,int mdy){size_t base=pos;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if((long long)pw*ph>250000)
#endif
        for(int y=0;y<ph;y++){int sy=clampi(y-mdy,0,ph-1);for(int x=0;x<pw;x++){int sx=clampi(x-mdx,0,pw-1);size_t di=base+size_t(y)*pw+x,si=base+size_t(sy)*pw+sx;int8_t d=int8_t(uint8_t(int(f[di])-int(p[si])));int sd=int(d);if(std::abs(sd)<=deadzone)sd=0;r[di]=uint8_t(int8_t(sd));}}pos+=size_t(pw)*ph;};
    plane(w,h,dx,dy);plane(cw,ch,dx/2,dy/2);plane(cw,ch,dx/2,dy/2);
}
static void motion_restore(const std::vector<uint8_t>& r,const std::vector<uint8_t>& p,std::vector<uint8_t>& f,int w,int h,int dx,int dy){
    f.resize(r.size());size_t pos=0;int cw=w/2,ch=h/2;
    auto plane=[&](int pw,int ph,int mdx,int mdy){size_t base=pos;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if((long long)pw*ph>250000)
#endif
        for(int y=0;y<ph;y++){int sy=clampi(y-mdy,0,ph-1);for(int x=0;x<pw;x++){int sx=clampi(x-mdx,0,pw-1);size_t di=base+size_t(y)*pw+x,si=base+size_t(sy)*pw+sx;f[di]=uint8_t(int(p[si])+int(int8_t(r[di])));}}pos+=size_t(pw)*ph;};
    plane(w,h,dx,dy);plane(cw,ch,dx/2,dy/2);plane(cw,ch,dx/2,dy/2);
}


static std::vector<uint8_t> bitpack6(const std::vector<uint8_t>& r,bool block_delta);
static constexpr int MV_BLOCK = 32;
static inline int mv_score_block(const std::vector<uint8_t>& cur,const std::vector<uint8_t>& old,int w,int h,int bx,int by,int bw,int bh,int dx,int dy,int sample){
    long long sad=0;int n=0;int x0=bx*MV_BLOCK,y0=by*MV_BLOCK;
    for(int y=0;y<bh;y+=sample){int yy=y0+y,sy=clampi(yy-dy,0,h-1);for(int x=0;x<bw;x+=sample){int xx=x0+x,sx=clampi(xx-dx,0,w-1);sad+=std::abs(int(cur[size_t(yy)*w+xx])-int(old[size_t(sy)*w+sx]));n++;}}
    return n?int((sad*256)/n):0;
}
static void estimate_block_motion(const std::vector<uint8_t>& cur,const std::vector<uint8_t>& old,int w,int h,int search,int quality,std::vector<int8_t>& mvx,std::vector<int8_t>& mvy){
    int gw=(w+MV_BLOCK-1)/MV_BLOCK,gh=(h+MV_BLOCK-1)/MV_BLOCK,total=gw*gh;mvx.assign(total,0);mvy.assign(total,0);if(search<=0)return;search=std::clamp(search,0,15);int sample=(quality>=86)?4:8;
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic,8) if(total>128)
#endif
    for(int bi=0;bi<total;bi++){
        int bx=bi%gw,by=bi/gw,x0=bx*MV_BLOCK,y0=by*MV_BLOCK,bw=std::min(MV_BLOCK,w-x0),bh=std::min(MV_BLOCK,h-y0);int zero=mv_score_block(cur,old,w,h,bx,by,bw,bh,0,0,sample),best=zero,bxv=0,byv=0;
        if(zero<=256)continue; // average SAD <= 1
        int inc=(search<=2)?1:2;
        for(int dy=-search;dy<=search;dy+=inc)for(int dx=-search;dx<=search;dx+=inc){if(dx==0&&dy==0)continue;int sc=mv_score_block(cur,old,w,h,bx,by,bw,bh,dx,dy,sample)+24*(std::abs(dx)+std::abs(dy));if(sc<best){best=sc;bxv=dx;byv=dy;}}
        if(inc>1){int ox=bxv,oy=byv;for(int dy=std::max(-search,oy-1);dy<=std::min(search,oy+1);dy++)for(int dx=std::max(-search,ox-1);dx<=std::min(search,ox+1);dx++){int sc=mv_score_block(cur,old,w,h,bx,by,bw,bh,dx,dy,sample)+24*(std::abs(dx)+std::abs(dy));if(sc<best){best=sc;bxv=dx;byv=dy;}}}
        // Require a real win; otherwise zero vectors RLE much better and avoid shimmer.
        if(best+384<zero && best*100<zero*72){mvx[bi]=int8_t(bxv);mvy[bi]=int8_t(byv);}
    }
}
static inline int qres(int sd,int dz,int step){if(std::abs(sd)<=dz)return 0;if(step<=1)return sd;int sign=sd<0?-1:1;int a=std::abs(sd);int q=((a+step/2)/step)*step;q=std::min(q,127);return sign*q;}
static void zero_motion_residual(const std::vector<uint8_t>& f,const std::vector<uint8_t>& p,
                                 std::vector<uint8_t>& r,std::vector<uint8_t>& recon,
                                 int dz,int rstep){
    r.resize(f.size());recon.resize(f.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(f.size()>400000)
#endif
    for(long long i=0;i<(long long)f.size();i++){
        int sd=int(int8_t(uint8_t(int(f[size_t(i)])-int(p[size_t(i)]))));
        sd=qres(sd,dz,rstep);
        r[size_t(i)]=uint8_t(int8_t(sd));
        recon[size_t(i)]=uint8_t(int(p[size_t(i)])+sd);
    }
}
static void block_motion_residual(const std::vector<uint8_t>& f,const std::vector<uint8_t>& p,std::vector<uint8_t>& r,std::vector<uint8_t>& recon,int w,int h,const std::vector<int8_t>& mvx,const std::vector<int8_t>& mvy,int dz,int rstep){
    r.resize(f.size());recon.resize(f.size());int gw=(w+MV_BLOCK-1)/MV_BLOCK;size_t pos=0;int cw=w/2,ch=h/2;
    auto plane=[&](int pw,int ph,bool chroma){size_t base=pos;int pblock=chroma?MV_BLOCK/2:MV_BLOCK;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if((long long)pw*ph>250000)
#endif
        for(int y=0;y<ph;y++){int by=y/pblock;for(int x=0;x<pw;x++){int bx=x/pblock,bi=by*gw+bx;int dx=int(mvx[bi]),dy=int(mvy[bi]);if(chroma){dx/=2;dy/=2;}int sy=clampi(y-dy,0,ph-1),sx=clampi(x-dx,0,pw-1);size_t di=base+size_t(y)*pw+x,si=base+size_t(sy)*pw+sx;uint8_t pred=p[si];int sd=int(int8_t(uint8_t(int(f[di])-int(pred))));sd=qres(sd,dz,rstep);r[di]=uint8_t(int8_t(sd));recon[di]=uint8_t(int(pred)+sd);}}
        pos+=size_t(pw)*ph;};
    plane(w,h,false);plane(cw,ch,true);plane(cw,ch,true);
}
static std::vector<uint8_t> pack_mv_map(const std::vector<int8_t>& x,const std::vector<int8_t>& y,bool& use_rle){
    std::vector<uint8_t> raw;raw.reserve(x.size()*2);for(size_t i=0;i<x.size();i++){raw.push_back(uint8_t(x[i]));raw.push_back(uint8_t(y[i]));}
    std::vector<uint8_t> rle;rle.reserve(x.size());size_t i=0;while(i<x.size()){size_t j=i+1,maxj=std::min(x.size(),i+256);while(j<maxj&&x[j]==x[i]&&y[j]==y[i])j++;rle.push_back(uint8_t(x[i]));rle.push_back(uint8_t(y[i]));rle.push_back(uint8_t((j-i)-1));i=j;}
    use_rle=rle.size()<raw.size();return use_rle?std::move(rle):std::move(raw);
}
static std::vector<uint8_t> pack_p6(const std::vector<int8_t>& mvx,const std::vector<int8_t>& mvy,int w,int h,const std::vector<uint8_t>& residual){
    bool rle=false;auto mv=pack_mv_map(mvx,mvy,rle);auto rb=bitpack6(residual,true);int gw=(w+MV_BLOCK-1)/MV_BLOCK,gh=(h+MV_BLOCK-1)/MV_BLOCK;std::vector<uint8_t> out;out.reserve(20+mv.size()+rb.size());auto p16=[&](uint16_t v){out.push_back(uint8_t(v));out.push_back(uint8_t(v>>8));};auto p32=[&](uint32_t v){for(int i=0;i<4;i++)out.push_back(uint8_t(v>>(8*i)));};out.insert(out.end(),{'G','P','M','6'});p16(MV_BLOCK);p16(rle?1:0);p16(uint16_t(gw));p16(uint16_t(gh));p32(uint32_t(mv.size()));p32(uint32_t(rb.size()));out.insert(out.end(),mv.begin(),mv.end());out.insert(out.end(),rb.begin(),rb.end());return out;
}

static inline uint8_t zigzag8(uint8_t u){int v=int(int8_t(u));int z=v<0?((-v)*2-1):(v*2);return uint8_t(std::min(z,255));}
static inline uint8_t bit_width(uint8_t v){uint8_t n=0;while(v){n++;v>>=1;}return n;}
static std::vector<uint8_t> pack_desc_raw(const std::vector<uint8_t>& modes){std::vector<uint8_t> out;out.reserve((modes.size()+1)/2);for(size_t i=0;i<modes.size();i+=2){uint8_t a=modes[i]&15,b=(i+1<modes.size())?(modes[i+1]&15):0;out.push_back(uint8_t(a|(b<<4)));}return out;}
static std::vector<uint8_t> pack_desc_rle(const std::vector<uint8_t>& modes){std::vector<uint8_t> out;out.reserve(modes.size()/4+8);size_t i=0;while(i<modes.size()){uint8_t m=modes[i];size_t j=i+1,maxj=std::min(modes.size(),i+256);while(j<maxj&&modes[j]==m)j++;out.push_back(m);out.push_back(uint8_t((j-i)-1));i=j;}return out;}

struct EncChunk{uint16_t n=0;uint8_t flags=0;std::vector<uint8_t> desc,payload;};
struct BitWriter{
    std::vector<uint8_t>& out;uint64_t acc=0;int bits=0;
    explicit BitWriter(std::vector<uint8_t>& o):out(o){}
    void put(uint32_t v,int n){if(n<=0)return;acc|=(uint64_t(v)&((n==32)?0xffffffffull:((1ull<<n)-1ull)))<<bits;bits+=n;while(bits>=8){out.push_back(uint8_t(acc&255));acc>>=8;bits-=8;}}
    void zeros_then_one(int q){while(q>=24){put(0,24);q-=24;}if(q)put(0,q);put(1,1);}
    void flush(){if(bits){out.push_back(uint8_t(acc&255));acc=0;bits=0;}}
};
static inline uint8_t source_u(const std::vector<uint8_t>& r,size_t base,uint32_t j,bool delta){size_t i=base+j;if(i>=r.size())return 0;uint8_t cur=r[i];if(!delta||j==0)return cur;return uint8_t(int(cur)-int(r[i-1]));}
static EncChunk encode_chunk(const std::vector<uint8_t>& r,uint32_t first,uint16_t nblocks,bool delta){
    EncChunk c;c.n=nblocks;c.flags=delta?CHF_BLOCK_DELTA:0;std::vector<uint8_t> modes(nblocks,0);c.payload.reserve(size_t(nblocks)*18);
    for(uint16_t li=0;li<nblocks;li++){
        size_t base=size_t(first+li)*BLOCK;uint8_t mx=0,nz=0;uint32_t sumz=0;uint8_t codes[BLOCK];
        for(uint32_t j=0;j<BLOCK;j++){uint8_t code=zigzag8(source_u(r,base,j,delta));codes[j]=code;mx=std::max(mx,code);if(code)nz++;sumz+=code;}
        uint8_t w=bit_width(mx),mode=0;size_t best=(w?size_t(8*w):0);if(w==0){mode=0;best=0;}else{mode=w;if(size_t(8+nz)<best){mode=9;best=size_t(8+nz);}uint32_t mean=(sumz+32)/64;int k0=std::clamp(int(bit_width(uint8_t(std::min<uint32_t>(mean,255))))-2,0,5);for(int dk=-1;dk<=1;dk++){int k=std::clamp(k0+dk,0,5);uint32_t qsum=0;for(uint8_t z:codes)qsum+=(z>>k);size_t bytes=(size_t(qsum)+64u*size_t(1+k)+7)/8;if(bytes<best){best=bytes;mode=uint8_t(10+k);}}}
        modes[li]=mode;
        if(mode>=1&&mode<=8){uint64_t acc=0;int bits=0;for(uint32_t j=0;j<BLOCK;j++){acc|=(uint64_t(codes[j])<<bits);bits+=mode;while(bits>=8){c.payload.push_back(uint8_t(acc&255));acc>>=8;bits-=8;}}if(bits)c.payload.push_back(uint8_t(acc&255));}
        else if(mode==9){uint8_t mask[8]={0};for(uint32_t j=0;j<BLOCK;j++)if(codes[j])mask[j>>3]|=uint8_t(1u<<(j&7));c.payload.insert(c.payload.end(),mask,mask+8);for(uint32_t j=0;j<BLOCK;j++)if(codes[j])c.payload.push_back(codes[j]);}
        else if(mode>=10&&mode<=15){int k=mode-10;BitWriter bw(c.payload);uint32_t mask=(k==0)?0u:((1u<<k)-1u);for(uint8_t z:codes){int q=z>>k;bw.zeros_then_one(q);if(k)bw.put(z&mask,k);}bw.flush();}
    }
    auto raw=pack_desc_raw(modes),rle=pack_desc_rle(modes);if(rle.size()<raw.size()){c.desc=std::move(rle);c.flags|=CHF_DESC_RLE;}else c.desc=std::move(raw);return c;
}
static std::vector<uint8_t> bitpack6(const std::vector<uint8_t>& r,bool block_delta){
    uint32_t raw_size=uint32_t(r.size()),blocks=(raw_size+BLOCK-1)/BLOCK,chunks=(blocks+CHUNK_BLOCKS-1)/CHUNK_BLOCKS;std::vector<EncChunk> cv(chunks);
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic,1) if(chunks>4)
#endif
    for(long long ci=0;ci<(long long)chunks;ci++){uint32_t first=uint32_t(ci)*CHUNK_BLOCKS;uint16_t n=uint16_t(std::min<uint32_t>(CHUNK_BLOCKS,blocks-first));cv[size_t(ci)]=encode_chunk(r,first,n,block_delta);}
    std::vector<uint8_t> out;out.reserve(r.size()/2+4096);auto p16=[&](uint16_t v){out.push_back(uint8_t(v));out.push_back(uint8_t(v>>8));};auto p32=[&](uint32_t v){for(int i=0;i<4;i++)out.push_back(uint8_t(v>>(8*i)));};
    out.insert(out.end(),{'G','B','P','6'});p16(BLOCK);p16(0);p32(raw_size);p32(blocks);p16(CHUNK_BLOCKS);p16(0);
    for(auto& c:cv){p16(c.n);p16(uint16_t(c.desc.size()));p32(uint32_t(c.payload.size()));out.push_back(c.flags);out.push_back(0);out.push_back(0);out.push_back(0);out.insert(out.end(),c.desc.begin(),c.desc.end());out.insert(out.end(),c.payload.begin(),c.payload.end());}
    return out;
}


static std::vector<uint8_t> zrun_wrap(const std::vector<uint8_t>& in){
    if(in.size()<64)return in;std::vector<uint8_t> body;body.reserve(in.size());size_t i=0,n=in.size();
    while(i<n){
        if(in[i]==0){size_t j=i+1;while(j<n&&in[j]==0&&j-i<130)j++;size_t run=j-i;if(run>=3){body.push_back(uint8_t(0x80u|(run-3)));i=j;continue;}}
        size_t start=i;i++;
        while(i<n&&i-start<128){if(in[i]==0){size_t j=i+1;while(j<n&&in[j]==0&&j-i<3)j++;if(j-i>=3)break;}i++;}
        size_t len=i-start;body.push_back(uint8_t(len-1));body.insert(body.end(),in.begin()+start,in.begin()+start+len);
    }
    if(body.size()+8>=in.size())return in;std::vector<uint8_t> out;out.reserve(body.size()+8);out.insert(out.end(),{'Z','P','0','6'});uint32_t raw=uint32_t(in.size());for(int k=0;k<4;k++)out.push_back(uint8_t(raw>>(8*k)));out.insert(out.end(),body.begin(),body.end());return out;
}

int main(int argc,char** argv){
    if(argc<9){
        std::cerr<<"usage: ghvcore WIDTH HEIGHT QUALITY KEYINT SCENE_THRESHOLD MOTION_RANGE OUTPUT|- EXPECTED_FRAMES [--ghv FPS_NUM FPS_DEN] [--codec 6|7|8] [--profile]\n";
        return 2;
    }
    std::ios::sync_with_stdio(false);std::cin.tie(nullptr);
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    int w=std::stoi(argv[1]),h=std::stoi(argv[2]),quality=std::stoi(argv[3]),keyint=std::max(1,std::stoi(argv[4]));
    double scene_threshold=std::stod(argv[5]);
    int motion_range=std::clamp(std::stoi(argv[6]),0,31);
    std::string outpath=argv[7];
    uint64_t expected=std::stoull(argv[8]);
    bool ghv_mode=false,profile_enabled=false;int codec=6;
    uint32_t fps_num=30,fps_den=1;
    for(int ai=9;ai<argc;ai++){
        std::string a=argv[ai];
        if(a=="--ghv"&&ai+2<argc){ghv_mode=true;fps_num=uint32_t(std::stoul(argv[++ai]));fps_den=uint32_t(std::stoul(argv[++ai]));}
        else if(a=="--codec"&&ai+1<argc)codec=std::stoi(argv[++ai]);
        else if(a=="--profile")profile_enabled=true;
        else {std::cerr<<"invalid option: "<<a<<"\n";return 2;}
    }
    if((codec!=6&&codec!=7&&codec!=8)||(ghv_mode&&(fps_num==0||fps_den==0||outpath=="-"))){std::cerr<<"invalid codec/--ghv arguments\n";return 2;}
    if((w&1)||(h&1)||w<=0||h<=0){std::cerr<<"invalid dimensions\n";return 2;}
    size_t frame_size=size_t(w)*h*3/2;

    std::ofstream fout;std::ostream* outp=nullptr;
    if(outpath=="-") outp=&std::cout;
    else{
        fout.open(outpath,std::ios::binary|std::ios::trunc);
        if(!fout){std::cerr<<"cannot open output\n";return 3;}
        outp=&fout;
    }
    std::ostream& out=*outp;
    if(ghv_mode){
        char zero[96]={0}; out.write(zero,96);
    }else{
        out.write(codec==8?"GHS8":(codec==7?"GHS7":"GHS6"),4);write_u32(out,uint32_t(codec));write_u32(out,uint32_t(w));write_u32(out,uint32_t(h));write_u32(out,uint32_t(frame_size));
    }

    init_crc();
    std::vector<uint8_t> frame(frame_size),prev,res,recon,source_samples;
    std::vector<std::pair<uint64_t,uint8_t>> index;
    if(ghv_mode && expected>0 && expected<100000000ull) index.reserve(size_t(expected));
    uint64_t count=0,repeats=0,pframes=0,iframes=0,mv_nonzero=0,mv_total=0,zero_blocks=0;
    auto start=std::chrono::steady_clock::now();ghvc8::Profile codec_profile;
    uint64_t input_ns=0,decision_ns=0,codec_ns=0,intra_ns=0,crc_ns=0,io_ns=0;
    const int dz=residual_deadzone(quality),rstep=residual_step(quality);
    const double rpt=repeat_threshold(quality);

    while(true){
        auto input_t0=std::chrono::steady_clock::now();
        std::cin.read(reinterpret_cast<char*>(frame.data()),std::streamsize(frame_size));
        input_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-input_t0).count());
        std::streamsize got=std::cin.gcount();
        if(got==0)break;
        if(size_t(got)!=frame_size){std::cerr<<"truncated raw frame\n";return 4;}

        if(codec==6)quantize(frame,w,h,quality);
        auto decision_t0=std::chrono::steady_clock::now();bool force_i=(count%uint64_t(keyint)==0)||prev.empty();
        uint8_t type=0;int dx=0,dy=0;std::vector<uint8_t> packed;
        double sc=prev.empty()?1e30:scene_score(frame,prev,w,h);
        double repeat_sc=source_repeat_score(frame,source_samples,w,h);
        if(!force_i&&repeat_sc<=rpt){
            type=2;repeats++;recon=prev;
        }else if(!force_i&&sc<scene_threshold){
            type=1;pframes++;
            if(codec==8){
                auto codec_t0=std::chrono::steady_clock::now();packed=ghvc8::encode(frame,prev,w,h,quality,motion_range,recon,&zero_blocks,&mv_nonzero,profile_enabled?&codec_profile:nullptr);
                codec_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-codec_t0).count());
                mv_total+=uint64_t((w+15)/16)*uint64_t((h+15)/16);
            }else if(codec==7){
                packed=ghvc7::encode(frame,&prev,w,h,quality,false,recon,&zero_blocks);
            }else if(motion_range<=0){
                // Common fast path for Balanced/Fast: same-position prediction.
                // Avoid allocating/scanning a full motion grid when every vector is zero.
                zero_motion_residual(frame,prev,res,recon,dz,rstep);
                packed=bitpack6(res,true);
            }else{
                std::vector<int8_t> mvx,mvy;
                estimate_block_motion(frame,prev,w,h,motion_range,quality,mvx,mvy);
                for(size_t mi=0;mi<mvx.size();mi++){mv_total++;if(mvx[mi]||mvy[mi])mv_nonzero++;}
                block_motion_residual(frame,prev,res,recon,w,h,mvx,mvy,dz,rstep);
                packed=pack_p6(mvx,mvy,w,h,res);
            }
        }else{
            type=0;iframes++;
            if(codec==8||codec==7){auto intra_t0=std::chrono::steady_clock::now();packed=ghvc7::encode(frame,nullptr,w,h,quality,true,recon,&zero_blocks);intra_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-intra_t0).count());}
            else {intra_residual(frame,res,w,h);packed=bitpack6(res,false);recon=frame;}
        }
        if(type!=2&&codec==6)packed=zrun_wrap(packed);
        capture_source_samples(frame,source_samples,w,h);
        auto crc_t0=std::chrono::steady_clock::now();uint32_t chk=crc32(recon.data(),recon.size());
        crc_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-crc_t0).count());
        decision_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-decision_t0).count());

        auto io_t0=std::chrono::steady_clock::now();
        if(ghv_mode){
            uint64_t off=uint64_t(out.tellp());
            index.push_back({off,type});
            out.write("VFRM",4);
            write_u32(out,uint32_t(count));
            uint64_t pts=(count*uint64_t(fps_den)*1000000ull)/uint64_t(fps_num);
            write_u64(out,pts);
            out.put(char(type));out.put(char(codec));write_u16(out,0);
            write_u32(out,uint32_t(frame_size));write_u32(out,uint32_t(packed.size()));write_u32(out,chk);
        }else{
            out.put(char(type));out.put(char(int8_t(dx)));out.put(char(int8_t(dy)));out.put(0);
            write_u32(out,uint32_t(frame_size));write_u32(out,uint32_t(packed.size()));write_u32(out,chk);
        }
        if(!packed.empty())out.write(reinterpret_cast<const char*>(packed.data()),std::streamsize(packed.size()));
        io_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-io_t0).count());
        if(!out){std::cerr<<"output write failed\n";return 5;}
        prev.swap(recon);count++;

        if(count%15==0){
            double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            double encfps=count/std::max(sec,1e-6);
            double pct=expected?100.0*double(count)/double(expected):0.0;
            double eta=(expected&&encfps>0&&count<expected)?double(expected-count)/encfps:-1.0;
            uint64_t bytes=uint64_t(out.tellp());
            std::cerr<<"GHV_PROGRESS frame="<<count<<" total="<<expected<<" pct="<<pct
                     <<" fps="<<encfps<<" eta="<<eta<<" size_mib="<<(double(bytes)/(1024.0*1024.0))
                     <<" repeats="<<repeats<<"\n";
        }
    }

    if(ghv_mode){
        uint64_t index_offset=uint64_t(out.tellp());
        out.write("INDX",4);write_u32(out,uint32_t(index.size()));
        for(const auto& e:index){
            write_u64(out,e.first);out.put(char(e.second));
            char pad[7]={0};out.write(pad,7);
        }
        uint64_t duration_us=count?(count*uint64_t(fps_den)*1000000ull)/uint64_t(fps_num):0;
        out.seekp(0,std::ios::beg);
        out.write("GHV1",4);out.put(char(0));out.put(char(codec));write_u16(out,96);
        write_u32(out,0);
        write_u32(out,uint32_t(w));write_u32(out,uint32_t(h));
        write_u32(out,fps_num);write_u32(out,fps_den);
        write_u32(out,uint32_t(count));write_u32(out,uint32_t(keyint));write_u32(out,uint32_t(quality));
        write_u32(out,0);write_u16(out,0);write_u16(out,0);
        write_u64(out,0);
        write_u64(out,96);
        write_u64(out,0);
        write_u64(out,index_offset);
        write_u64(out,duration_us);
        char reserved[8]={0};out.write(reserved,8);
        out.seekp(0,std::ios::end);
    }
    out.flush();

    double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cerr<<"GHVC"<<codec<<"_RESULT threads="
#ifdef _OPENMP
    <<omp_get_max_threads()
#else
    <<1
#endif
    <<" frames="<<count<<" i="<<iframes<<" p="<<pframes<<" repeats="<<repeats
    <<" deadzone="<<(codec==6?dz:0)<<" rstep="<<(codec==6?rstep:0)<<" mv_used="<<mv_nonzero<<"/"<<mv_total
    <<" zero_blocks="<<zero_blocks
    <<" elapsed="<<sec<<" fps="<<(count/std::max(sec,1e-6))
    <<" mode="<<(ghv_mode?"ghv-direct":"stream")<<"\n";
    if(profile_enabled){
        auto ms=[](uint64_t ns){return double(ns)/1000000.0;};
        std::cerr<<"GHV_ENCODE_PROFILE input_ms="<<ms(input_ns)
                 <<" decision_total_ms="<<ms(decision_ns)<<" codec_p_ms="<<ms(codec_ns)
                 <<" codec_i_ms="<<ms(intra_ns)<<" crc_ms="<<ms(crc_ns)
                 <<" motion_cpu_ms="<<ms(codec_profile.motion_search_ns.load())
                 <<" rd_cpu_ms="<<ms(codec_profile.rd_ns.load())
                 <<" mv_entropy_ms="<<ms(codec_profile.mv_entropy_ns)
                 <<" transform_quant_ms="<<ms(codec_profile.transform_quant_ns)
                 <<" coeff_entropy_ms="<<ms(codec_profile.coeff_entropy_ns)
                 <<" reconstruction_ms="<<ms(codec_profile.reconstruction_ns)
                 <<" io_ms="<<ms(io_ns)<<" p_frames="<<codec_profile.p_frames<<"\n";
    }
    return 0;
}
