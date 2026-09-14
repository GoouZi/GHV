#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// GHVC3 native reference encoder core.
// Reads raw YUV420p from stdin and writes a GHS3 intermediate stream.
// No libav, zlib, image library or existing video codec is used here.

static constexpr uint32_t BLOCK = 64;

static void write_u16(std::ostream& o, uint16_t v){ char b[2]={char(v),char(v>>8)}; o.write(b,2); }
static void write_u32(std::ostream& o, uint32_t v){ char b[4]={char(v),char(v>>8),char(v>>16),char(v>>24)}; o.write(b,4); }
static void write_u64(std::ostream& o, uint64_t v){ char b[8]; for(int i=0;i<8;i++) b[i]=char(v>>(8*i)); o.write(b,8); }

static std::pair<int,int> quality_steps(int q){
    q=std::clamp(q,1,100);
    if(q>=97)return{1,1}; if(q>=92)return{1,2}; if(q>=86)return{2,3}; if(q>=78)return{2,4};
    if(q>=70)return{3,5}; if(q>=62)return{4,7}; if(q>=54)return{5,8}; if(q>=46)return{6,10}; return{8,12};
}
static inline uint8_t quant1(uint8_t x,int step){ if(step<=1)return x; int v=((int(x)+step/2)/step)*step; return uint8_t(std::min(v,255)); }
static void quantize(std::vector<uint8_t>& f,int w,int h,int q){
    auto [ys,cs]=quality_steps(q); size_t ysz=size_t(w)*h;
    for(size_t i=0;i<ysz;i++)f[i]=quant1(f[i],ys); for(size_t i=ysz;i<f.size();i++)f[i]=quant1(f[i],cs);
}

static uint32_t crc_table[256];
static void init_crc(){ for(uint32_t i=0;i<256;i++){ uint32_t c=i; for(int j=0;j<8;j++) c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1); crc_table[i]=c; } }
static uint32_t crc32(const uint8_t* p,size_t n){ uint32_t c=0xFFFFFFFFu; for(size_t i=0;i<n;i++)c=crc_table[(c^p[i])&255]^(c>>8); return c^0xFFFFFFFFu; }

static double scene_score(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,int w,int h){
    uint64_t sum=0,count=0; for(int y=0;y<h;y+=16){ size_t row=size_t(y)*w; for(int x=0;x<w;x+=16){ sum+=uint64_t(std::abs(int(a[row+x])-int(b[row+x]))); count++; } }
    return count?double(sum)/double(count):0.0;
}

static double motion_score(const std::vector<uint8_t>& cur,const std::vector<uint8_t>& old,int w,int h,int dx,int dy,int search){
    const int step=10, margin=search+2; uint64_t sum=0,count=0;
    for(int y=margin;y<h-margin;y+=step){
        int sy=y-dy;
        for(int x=margin;x<w-margin;x+=step){ int sx=x-dx; sum+=uint64_t(std::abs(int(cur[size_t(y)*w+x])-int(old[size_t(sy)*w+sx]))); count++; }
    }
    return count?double(sum)/double(count):1e30;
}
static std::pair<int,int> estimate_motion(const std::vector<uint8_t>& cur,const std::vector<uint8_t>& old,int w,int h,int search){
    if(search<=0||w<64||h<64)return{0,0}; search=std::clamp(search,0,31);
    double best=1e30; int bx=0,by=0; int inc=search>=2?2:1;
    for(int dy=-search;dy<=search;dy+=inc)for(int dx=-search;dx<=search;dx+=inc){ double s=motion_score(cur,old,w,h,dx,dy,search); if(s<best){best=s;bx=dx;by=dy;} }
    int ox=bx,oy=by;
    for(int dy=std::max(-search,oy-1);dy<=std::min(search,oy+1);dy++)for(int dx=std::max(-search,ox-1);dx<=std::min(search,ox+1);dx++){
        double s=motion_score(cur,old,w,h,dx,dy,search); if(s<best){best=s;bx=dx;by=dy;}
    }
    return{bx,by};
}

static void intra_residual(const std::vector<uint8_t>& f,std::vector<uint8_t>& r,int w,int h){
    r.resize(f.size()); int cw=w/2,ch=h/2; size_t pos=0;
    auto plane=[&](int pw,int ph){ for(int y=0;y<ph;y++){ size_t row=pos+size_t(y)*pw; r[row]=f[row]; for(int x=1;x<pw;x++)r[row+x]=uint8_t(int(f[row+x])-int(f[row+x-1])); } pos+=size_t(pw)*ph; };
    plane(w,h);plane(cw,ch);plane(cw,ch);
}
static inline int clampi(int v,int lo,int hi){return std::max(lo,std::min(v,hi));}
static void motion_residual(const std::vector<uint8_t>& f,const std::vector<uint8_t>& p,std::vector<uint8_t>& r,int w,int h,int dx,int dy){
    r.resize(f.size()); size_t pos=0; int cw=w/2,ch=h/2;
    auto plane=[&](int pw,int ph,int mdx,int mdy){
        for(int y=0;y<ph;y++){ int sy=clampi(y-mdy,0,ph-1); for(int x=0;x<pw;x++){ int sx=clampi(x-mdx,0,pw-1); size_t di=pos+size_t(y)*pw+x, si=pos+size_t(sy)*pw+sx; r[di]=uint8_t(int(f[di])-int(p[si])); } }
        pos+=size_t(pw)*ph;
    };
    plane(w,h,dx,dy); plane(cw,ch,dx/2,dy/2); plane(cw,ch,dx/2,dy/2);
}

static inline uint8_t zigzag8(uint8_t u){ int v=int(int8_t(u)); return uint8_t(v<0?((-v)*2-1):(v*2)); }
static inline uint8_t bit_width(uint8_t v){ uint8_t n=0; while(v){n++;v>>=1;} return n; }

static std::vector<uint8_t> bitpack(const std::vector<uint8_t>& r){
    uint32_t raw_size=uint32_t(r.size()), blocks=(raw_size+BLOCK-1)/BLOCK;
    std::vector<uint8_t> modes(blocks,0), widths(blocks,0);
    std::vector<uint8_t> nzcounts(blocks,0);
    for(uint32_t bi=0;bi<blocks;bi++){
        uint8_t mx=0,nz=0; size_t base=size_t(bi)*BLOCK;
        for(uint32_t j=0;j<BLOCK;j++){
            uint8_t u=(base+j<r.size())?r[base+j]:0; uint8_t z=zigzag8(u);
            mx=std::max(mx,z); if(z)nz++;
        }
        uint8_t w=bit_width(mx); widths[bi]=w; nzcounts[bi]=nz;
        if(w==0)modes[bi]=0;
        else if((8+int(nz)) < (8*int(w)))modes[bi]=9; // sparse bitmap + nonzero bytes
        else modes[bi]=w;
    }
    std::vector<uint8_t> out; out.reserve(r.size()/2+64);
    out.insert(out.end(),{'G','B','P','3'});
    auto push16=[&](uint16_t v){out.push_back(uint8_t(v));out.push_back(uint8_t(v>>8));};
    auto push32=[&](uint32_t v){for(int i=0;i<4;i++)out.push_back(uint8_t(v>>(8*i)));};
    push16(BLOCK);push16(0);push32(raw_size);push32(blocks);
    for(uint32_t i=0;i<blocks;i+=2){ uint8_t a=modes[i]&15,b=(i+1<blocks)?(modes[i+1]&15):0; out.push_back(uint8_t(a|(b<<4))); }
    for(uint8_t w=1;w<=8;w++){
        for(uint32_t bi=0;bi<blocks;bi++) if(modes[bi]==w){
            uint64_t acc=0; int bits=0; size_t base=size_t(bi)*BLOCK;
            for(uint32_t j=0;j<BLOCK;j++){
                uint8_t u=(base+j<r.size())?r[base+j]:0; uint8_t code=zigzag8(u);
                acc |= (uint64_t(code) << bits); bits += w;
                while(bits>=8){ out.push_back(uint8_t(acc&255)); acc>>=8; bits-=8; }
            }
            if(bits) out.push_back(uint8_t(acc&255));
        }
    }
    // Sparse blocks are kept in block order after all fixed-width groups.
    for(uint32_t bi=0;bi<blocks;bi++) if(modes[bi]==9){
        size_t base=size_t(bi)*BLOCK; uint8_t mask[8]={0,0,0,0,0,0,0,0};
        for(uint32_t j=0;j<BLOCK;j++){
            uint8_t u=(base+j<r.size())?r[base+j]:0; uint8_t code=zigzag8(u);
            if(code)mask[j>>3]|=uint8_t(1u<<(j&7));
        }
        out.insert(out.end(),mask,mask+8);
        for(uint32_t j=0;j<BLOCK;j++){
            uint8_t u=(base+j<r.size())?r[base+j]:0; uint8_t code=zigzag8(u);
            if(code)out.push_back(code);
        }
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

    std::ofstream fout;
    std::ostream* outp=nullptr;
    if(outpath=="-"){
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        outp=&std::cout;
    }else{
        fout.open(outpath,std::ios::binary|std::ios::trunc);
        if(!fout){std::cerr<<"cannot open output\n";return 3;}
        outp=&fout;
    }
    std::ostream& out=*outp;

    init_crc();
    // GHS4 is an uncounted streaming intermediate: a fixed 20-byte header,
    // followed by 16-byte frame records + payloads until EOF. This allows the
    // wrapper to write .ghv directly without a giant temporary video file.
    out.write("GHS4",4);write_u32(out,3);write_u32(out,uint32_t(w));write_u32(out,uint32_t(h));write_u32(out,uint32_t(frame_size));
    std::vector<uint8_t> frame(frame_size),prev,res; uint64_t count=0,repeats=0; auto start=std::chrono::steady_clock::now();
    while(true){
        std::cin.read(reinterpret_cast<char*>(frame.data()),std::streamsize(frame_size));
        std::streamsize got=std::cin.gcount(); if(got==0)break; if(size_t(got)!=frame_size){std::cerr<<"truncated raw frame\n";return 4;}
        quantize(frame,w,h,quality); bool force_i=(count%uint64_t(keyint)==0)||prev.empty(); uint8_t type=0; int dx=0,dy=0; std::vector<uint8_t> packed;
        if(!force_i && frame==prev){ type=2; repeats++; }
        else if(!force_i && scene_score(frame,prev,w,h)<scene_threshold){ type=1; auto mv=estimate_motion(frame,prev,w,h,motion_range);dx=mv.first;dy=mv.second;motion_residual(frame,prev,res,w,h,dx,dy);packed=bitpack(res); }
        else { type=0;intra_residual(frame,res,w,h);packed=bitpack(res); }
        uint32_t chk=crc32(frame.data(),frame.size());
        out.put(char(type));out.put(char(int8_t(dx)));out.put(char(int8_t(dy)));out.put(0);
        write_u32(out,uint32_t(frame_size));write_u32(out,uint32_t(packed.size()));write_u32(out,chk);
        if(!packed.empty())out.write(reinterpret_cast<const char*>(packed.data()),std::streamsize(packed.size()));
        if(!out){std::cerr<<"output write failed\n";return 5;}
        prev=frame;count++;
    }
    out.flush();
    double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cerr<<"GHVC3_RESULT frames="<<count<<" repeats="<<repeats<<" elapsed="<<sec<<" fps="<<(count/std::max(sec,1e-6))<<"\n";
    return 0;
}
