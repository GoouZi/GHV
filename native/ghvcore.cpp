#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// GHVC4 native encoder core (GHV 0.5).
// Reads raw YUV420p from stdin, writes GHS5 streaming records to stdout/file.
// Entire prediction/packing path is original GHV code: no libav/zlib/VPx/etc.
//
// Key 0.5 changes:
//  * hierarchical global-motion search instead of square exhaustive search
//  * low-motion early-out
//  * quality-controlled P residual dead-zone with closed-loop reconstruction
//  * GBP4 descriptor RLE (adaptive: raw nibble map or RLE, whichever is smaller)
//  * approximate repeat-frame decision for near-static frames

static constexpr uint32_t BLOCK = 64;
static constexpr uint16_t GBP4_FLAG_DESC_RLE = 1;

static void write_u16(std::ostream& o, uint16_t v){ char b[2]={char(v),char(v>>8)}; o.write(b,2); }
static void write_u32(std::ostream& o, uint32_t v){ char b[4]={char(v),char(v>>8),char(v>>16),char(v>>24)}; o.write(b,4); }

static std::pair<int,int> quality_steps(int q){
    q=std::clamp(q,1,100);
    if(q>=97)return{1,1}; if(q>=92)return{1,2}; if(q>=86)return{2,3}; if(q>=78)return{2,4};
    if(q>=70)return{3,5}; if(q>=62)return{4,7}; if(q>=54)return{5,8}; if(q>=46)return{6,10}; return{8,12};
}
static int residual_deadzone(int q){
    q=std::clamp(q,1,100); if(q>=76)return 0; if(q>=68)return 2; if(q>=56)return 3; return 4;
}
static double repeat_threshold(int q){
    q=std::clamp(q,1,100); if(q>=90)return 0.0; if(q>=82)return .15; if(q>=76)return .35; if(q>=68)return .60; return .85;
}
static inline uint8_t quant1(uint8_t x,int step){ if(step<=1)return x; int v=((int(x)+step/2)/step)*step; return uint8_t(std::min(v,255)); }
static void quantize(std::vector<uint8_t>& f,int w,int h,int q){
    auto [ys,cs]=quality_steps(q); size_t ysz=size_t(w)*h;
    for(long long i=0;i<(long long)ysz;i++)f[size_t(i)]=quant1(f[size_t(i)],ys);
    for(long long i=(long long)ysz;i<(long long)f.size();i++)f[size_t(i)]=quant1(f[size_t(i)],cs);
}

static uint32_t crc_table[256];
static void init_crc(){ for(uint32_t i=0;i<256;i++){ uint32_t c=i; for(int j=0;j<8;j++) c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1); crc_table[i]=c; } }
static uint32_t crc32(const uint8_t* p,size_t n){ uint32_t c=0xFFFFFFFFu; for(size_t i=0;i<n;i++)c=crc_table[(c^p[i])&255]^(c>>8); return c^0xFFFFFFFFu; }

static double scene_score(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,int w,int h){
    uint64_t sum=0,count=0;
    for(int y=0;y<h;y+=16){ size_t row=size_t(y)*w; for(int x=0;x<w;x+=16){ sum+=uint64_t(std::abs(int(a[row+x])-int(b[row+x]))); count++; } }
    return count?double(sum)/double(count):0.0;
}

static double motion_score(const std::vector<uint8_t>& cur,const std::vector<uint8_t>& old,int w,int h,int dx,int dy,int search){
    const int step=std::clamp(h/54,10,24), margin=search+2; uint64_t sum=0,count=0;
    for(int y=margin;y<h-margin;y+=step){ int sy=y-dy;
        for(int x=margin;x<w-margin;x+=step){ int sx=x-dx; sum+=uint64_t(std::abs(int(cur[size_t(y)*w+x])-int(old[size_t(sy)*w+sx]))); count++; }
    }
    return count?double(sum)/double(count):1e30;
}

static std::pair<int,int> estimate_motion(const std::vector<uint8_t>& cur,const std::vector<uint8_t>& old,int w,int h,int search,int quality,int hintx,int hinty){
    if(search<=0||w<64||h<64)return{0,0}; search=std::clamp(search,0,31);
    double zero=motion_score(cur,old,w,h,0,0,search),best=zero; int bx=0,by=0;
    if(best<=0.5)return{0,0};
    struct C{int x,y;}; std::vector<C> cand;
    auto add_local=[&](int cx,int cy,int rad){
        for(int dy=-rad;dy<=rad;dy++)for(int dx=-rad;dx<=rad;dx++){
            int x=std::clamp(cx+dx,-search,search),y=std::clamp(cy+dy,-search,search);
            bool dup=false; for(const auto& c:cand)if(c.x==x&&c.y==y){dup=true;break;} if(!dup)cand.push_back({x,y});
        }
    };
    add_local(std::clamp(hintx,-search,search),std::clamp(hinty,-search,search),2);
    add_local(0,0,1);
    std::vector<double> scores(cand.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(cand.size()>16)
#endif
    for(long long i=0;i<(long long)cand.size();i++)scores[size_t(i)]=motion_score(cur,old,w,h,cand[size_t(i)].x,cand[size_t(i)].y,search);
    for(size_t i=0;i<cand.size();i++)if(scores[i]<best){best=scores[i];bx=cand[i].x;by=cand[i].y;}
    // Motion is temporally coherent in most video. If the hinted/local search
    // clearly improves the zero vector, avoid an expensive full search.
    if(best<=4.0 || best<=zero*0.90)return{bx,by};
    int inc=(quality>=86)?2:4; inc=std::min(inc,std::max(1,search)); cand.clear();
    for(int dy=-search;dy<=search;dy+=inc)for(int dx=-search;dx<=search;dx+=inc)cand.push_back({dx,dy});
    for(int v=-search;v<=search;v+=inc){cand.push_back({search,v});cand.push_back({v,search});}
    scores.resize(cand.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(cand.size()>16)
#endif
    for(long long i=0;i<(long long)cand.size();i++)scores[size_t(i)]=motion_score(cur,old,w,h,cand[size_t(i)].x,cand[size_t(i)].y,search);
    for(size_t i=0;i<cand.size();i++)if(scores[i]<best){best=scores[i];bx=cand[i].x;by=cand[i].y;}
    int ox=bx,oy=by;
    for(int dy=std::max(-search,oy-1);dy<=std::min(search,oy+1);dy++)for(int dx=std::max(-search,ox-1);dx<=std::min(search,ox+1);dx++){
        double sc=motion_score(cur,old,w,h,dx,dy,search);if(sc<best){best=sc;bx=dx;by=dy;}
    }
    return{bx,by};
}

static void intra_residual(const std::vector<uint8_t>& f,std::vector<uint8_t>& r,int w,int h){
    r.resize(f.size()); int cw=w/2,ch=h/2; size_t pos=0;
    auto plane=[&](int pw,int ph){
        for(int y=0;y<ph;y++){ size_t row=pos+size_t(y)*pw; r[row]=f[row];
            for(int x=1;x<pw;x++)r[row+x]=uint8_t(int(f[row+x])-int(f[row+x-1])); }
        pos+=size_t(pw)*ph;
    };
    plane(w,h);plane(cw,ch);plane(cw,ch);
}
static void intra_restore(const std::vector<uint8_t>& r,std::vector<uint8_t>& f,int w,int h){
    f.resize(r.size()); int cw=w/2,ch=h/2; size_t pos=0;
    auto plane=[&](int pw,int ph){
        for(int y=0;y<ph;y++){ size_t row=pos+size_t(y)*pw; uint8_t acc=r[row]; f[row]=acc;
            for(int x=1;x<pw;x++){ acc=uint8_t(int(acc)+int(int8_t(r[row+x]))); f[row+x]=acc; } }
        pos+=size_t(pw)*ph;
    };
    plane(w,h);plane(cw,ch);plane(cw,ch);
}
static inline int clampi(int v,int lo,int hi){return std::max(lo,std::min(v,hi));}
static void motion_residual(const std::vector<uint8_t>& f,const std::vector<uint8_t>& p,std::vector<uint8_t>& r,int w,int h,int dx,int dy,int deadzone){
    r.resize(f.size()); size_t pos=0; int cw=w/2,ch=h/2;
    auto plane=[&](int pw,int ph,int mdx,int mdy){
        for(int y=0;y<ph;y++){ int sy=clampi(y-mdy,0,ph-1);
            for(int x=0;x<pw;x++){ int sx=clampi(x-mdx,0,pw-1); size_t di=pos+size_t(y)*pw+x, si=pos+size_t(sy)*pw+sx;
                int d=int(f[di])-int(p[si]); if(std::abs(d)<=deadzone)d=0; r[di]=uint8_t(d); }
        }
        pos+=size_t(pw)*ph;
    };
    plane(w,h,dx,dy); plane(cw,ch,dx/2,dy/2); plane(cw,ch,dx/2,dy/2);
}
static void motion_restore(const std::vector<uint8_t>& r,const std::vector<uint8_t>& p,std::vector<uint8_t>& f,int w,int h,int dx,int dy){
    f.resize(r.size()); size_t pos=0; int cw=w/2,ch=h/2;
    auto plane=[&](int pw,int ph,int mdx,int mdy){
        for(int y=0;y<ph;y++){ int sy=clampi(y-mdy,0,ph-1);
            for(int x=0;x<pw;x++){ int sx=clampi(x-mdx,0,pw-1); size_t di=pos+size_t(y)*pw+x, si=pos+size_t(sy)*pw+sx;
                int v=int(p[si])+int(int8_t(r[di])); f[di]=uint8_t(v); }
        }
        pos+=size_t(pw)*ph;
    };
    plane(w,h,dx,dy); plane(cw,ch,dx/2,dy/2); plane(cw,ch,dx/2,dy/2);
}

static inline uint8_t zigzag8(uint8_t u){ int v=int(int8_t(u)); int z=v<0?((-v)*2-1):(v*2); return uint8_t(std::min(z,255)); }
static inline uint8_t bit_width(uint8_t v){ uint8_t n=0; while(v){n++;v>>=1;} return n; }

static std::vector<uint8_t> pack_desc_raw(const std::vector<uint8_t>& modes){
    std::vector<uint8_t> out; out.reserve((modes.size()+1)/2);
    for(size_t i=0;i<modes.size();i+=2){ uint8_t a=modes[i]&15,b=(i+1<modes.size())?(modes[i+1]&15):0; out.push_back(uint8_t(a|(b<<4))); }
    return out;
}
static std::vector<uint8_t> pack_desc_rle(const std::vector<uint8_t>& modes){
    std::vector<uint8_t> out; out.reserve(modes.size()/4+16); size_t i=0;
    while(i<modes.size()){
        uint8_t mode=modes[i]; size_t j=i+1, maxj=std::min(modes.size(),i+256);
        while(j<maxj&&modes[j]==mode)j++;
        out.push_back(mode); out.push_back(uint8_t((j-i)-1)); i=j;
    }
    return out;
}

static std::vector<uint8_t> bitpack(const std::vector<uint8_t>& rin,bool byte_delta=false){
    uint32_t raw_size=uint32_t(rin.size()), blocks=(raw_size+BLOCK-1)/BLOCK;
    std::vector<uint8_t> transformed; const std::vector<uint8_t>* rp=&rin;
    if(byte_delta && !rin.empty()){
        transformed.resize(rin.size()); transformed[0]=rin[0];
        for(size_t i=1;i<rin.size();i++) transformed[i]=uint8_t(int(rin[i])-int(rin[i-1]));
        rp=&transformed;
    }
    const auto& r=*rp;
    std::vector<uint8_t> modes(blocks,0);
    for(long long bii=0;bii<(long long)blocks;bii++){ uint32_t bi=uint32_t(bii);
        uint8_t mx=0,nz=0; size_t base=size_t(bi)*BLOCK;
        for(uint32_t j=0;j<BLOCK;j++){ uint8_t u=(base+j<r.size())?r[base+j]:0; uint8_t z=zigzag8(u); mx=std::max(mx,z); if(z)nz++; }
        uint8_t w=bit_width(mx); if(w==0)modes[bi]=0; else if((8+int(nz))<(8*int(w)))modes[bi]=9; else modes[bi]=w;
    }
    auto rawdesc=pack_desc_raw(modes); auto rledesc=pack_desc_rle(modes);
    bool use_rle=rledesc.size()<rawdesc.size(); const auto& desc=use_rle?rledesc:rawdesc;
    uint16_t flags=(use_rle?GBP4_FLAG_DESC_RLE:0) | (byte_delta?2:0);
    std::vector<uint8_t> out; out.reserve(r.size()/2+64); out.insert(out.end(),{'G','B','P','4'});
    auto push16=[&](uint16_t v){out.push_back(uint8_t(v));out.push_back(uint8_t(v>>8));}; auto push32=[&](uint32_t v){for(int i=0;i<4;i++)out.push_back(uint8_t(v>>(8*i)));};
    push16(BLOCK); push16(flags); push32(raw_size); push32(blocks); push32(uint32_t(desc.size())); out.insert(out.end(),desc.begin(),desc.end());
    for(uint8_t w=1;w<=8;w++)for(uint32_t bi=0;bi<blocks;bi++)if(modes[bi]==w){
        uint64_t acc=0; int bits=0; size_t base=size_t(bi)*BLOCK;
        for(uint32_t j=0;j<BLOCK;j++){ uint8_t u=(base+j<r.size())?r[base+j]:0; uint8_t code=zigzag8(u); acc|=(uint64_t(code)<<bits); bits+=w; while(bits>=8){out.push_back(uint8_t(acc&255));acc>>=8;bits-=8;} }
        if(bits)out.push_back(uint8_t(acc&255));
    }
    for(uint32_t bi=0;bi<blocks;bi++)if(modes[bi]==9){
        size_t base=size_t(bi)*BLOCK; uint8_t mask[8]={0,0,0,0,0,0,0,0};
        for(uint32_t j=0;j<BLOCK;j++){uint8_t u=(base+j<r.size())?r[base+j]:0;uint8_t code=zigzag8(u);if(code)mask[j>>3]|=uint8_t(1u<<(j&7));}
        out.insert(out.end(),mask,mask+8);
        for(uint32_t j=0;j<BLOCK;j++){uint8_t u=(base+j<r.size())?r[base+j]:0;uint8_t code=zigzag8(u);if(code)out.push_back(code);}
    }
    return out;
}

int main(int argc,char** argv){
    if(argc<9){std::cerr<<"usage: ghvcore WIDTH HEIGHT QUALITY KEYINT SCENE_THRESHOLD MOTION_RANGE OUTPUT|- EXPECTED_FRAMES\n";return 2;}
    int w=std::stoi(argv[1]),h=std::stoi(argv[2]),quality=std::stoi(argv[3]),keyint=std::max(1,std::stoi(argv[4]));
    double scene_threshold=std::stod(argv[5]); int motion_range=std::clamp(std::stoi(argv[6]),0,31); std::string outpath=argv[7]; uint64_t expected=std::stoull(argv[8]);
    (void)expected;
    if((w&1)||(h&1)||w<=0||h<=0){std::cerr<<"invalid dimensions\n";return 2;}
    size_t frame_size=size_t(w)*h*3/2;

    std::ofstream fout; std::ostream* outp=nullptr;
    if(outpath=="-"){
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
        outp=&std::cout;
    }else{ fout.open(outpath,std::ios::binary|std::ios::trunc); if(!fout){std::cerr<<"cannot open output\n";return 3;} outp=&fout; }
    std::ostream& out=*outp;

    init_crc();
    out.write("GHS5",4); write_u32(out,4); write_u32(out,uint32_t(w)); write_u32(out,uint32_t(h)); write_u32(out,uint32_t(frame_size));
    std::vector<uint8_t> frame(frame_size),prev,res,recon; uint64_t count=0,repeats=0,pframes=0,iframes=0; int last_dx=0,last_dy=0; auto start=std::chrono::steady_clock::now();
    const int dz=residual_deadzone(quality); const double rpt=repeat_threshold(quality);
    while(true){
        std::cin.read(reinterpret_cast<char*>(frame.data()),std::streamsize(frame_size)); std::streamsize got=std::cin.gcount();
        if(got==0)break; if(size_t(got)!=frame_size){std::cerr<<"truncated raw frame\n";return 4;}
        quantize(frame,w,h,quality); bool force_i=(count%uint64_t(keyint)==0)||prev.empty(); uint8_t type=0; int dx=0,dy=0; std::vector<uint8_t> packed;
        double sc=prev.empty()?1e30:scene_score(frame,prev,w,h);
        if(!force_i && sc<=rpt){ type=2; repeats++; recon=prev; }
        else if(!force_i && sc<scene_threshold){ type=1; pframes++; auto mv=estimate_motion(frame,prev,w,h,motion_range,quality,last_dx,last_dy); dx=mv.first;dy=mv.second; last_dx=dx; last_dy=dy; motion_residual(frame,prev,res,w,h,dx,dy,dz); packed=bitpack(res,true); motion_restore(res,prev,recon,w,h,dx,dy); }
        else { type=0; iframes++; last_dx=last_dy=0; intra_residual(frame,res,w,h); packed=bitpack(res); intra_restore(res,recon,w,h); }
        uint32_t chk=crc32(recon.data(),recon.size());
        out.put(char(type));out.put(char(int8_t(dx)));out.put(char(int8_t(dy)));out.put(0); write_u32(out,uint32_t(frame_size));write_u32(out,uint32_t(packed.size()));write_u32(out,chk);
        if(!packed.empty())out.write(reinterpret_cast<const char*>(packed.data()),std::streamsize(packed.size()));
        if(!out){std::cerr<<"output write failed\n";return 5;}
        prev.swap(recon); count++;
    }
    out.flush(); double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cerr<<"GHVC4_RESULT threads="
#ifdef _OPENMP
        <<omp_get_max_threads()
#else
        <<1
#endif
        <<" frames="<<count<<" i="<<iframes<<" p="<<pframes<<" repeats="<<repeats<<" deadzone="<<dz<<" elapsed="<<sec<<" fps="<<(count/std::max(sec,1e-6))<<"\n";
    return 0;
}
