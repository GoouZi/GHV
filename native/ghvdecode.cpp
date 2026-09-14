#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "ghvcodec7.h"
#include "ghvcodec8.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static uint16_t le16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}static uint32_t le32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}static uint64_t le64(const uint8_t* p){uint64_t v=0;for(int i=0;i<8;i++)v|=uint64_t(p[i])<<(8*i);return v;}static inline int clampi(int v,int lo,int hi){return std::max(lo,std::min(v,hi));}
static inline int popcount8(uint8_t x){int n=0;while(x){n+=x&1u;x>>=1;}return n;}
static uint32_t crc_table[256];static void init_crc(){for(uint32_t i=0;i<256;i++){uint32_t c=i;for(int j=0;j<8;j++)c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1);crc_table[i]=c;}}static uint32_t crc32(const uint8_t* p,size_t n){uint32_t c=0xFFFFFFFFu;for(size_t i=0;i<n;i++)c=crc_table[(c^p[i])&255]^(c>>8);return c^0xFFFFFFFFu;}static int8_t unzig(uint8_t z){int v=(int(z)>>1)^-int(z&1);return int8_t(v);}


static void unwrap_zp06(const std::vector<uint8_t>& in,std::vector<uint8_t>& out){
    if(in.size()<8||std::memcmp(in.data(),"ZP06",4)!=0){out=in;return;}uint32_t raw=le32(in.data()+4);out.clear();out.reserve(raw);size_t p=8;
    while(p<in.size()&&out.size()<raw){uint8_t t=in[p++];if(t&0x80){size_t run=size_t(t&0x7f)+3;if(out.size()+run>raw)throw std::runtime_error("bad ZP06 zero run");out.insert(out.end(),run,0);}else{size_t len=size_t(t)+1;if(p+len>in.size()||out.size()+len>raw)throw std::runtime_error("bad ZP06 literal");out.insert(out.end(),in.begin()+p,in.begin()+p+len);p+=len;}}
    if(p!=in.size()||out.size()!=raw)throw std::runtime_error("ZP06 length mismatch");
}

// Legacy GBP4 decode (for GHV 0.5 files).
static void unpack_gbp4(const std::vector<uint8_t>& data,std::vector<uint8_t>& out,size_t expected){
    if(data.size()<20||std::memcmp(data.data(),"GBP4",4)!=0)throw std::runtime_error("bad GBP4 payload");uint16_t block=le16(data.data()+4),flags=le16(data.data()+6);uint32_t raw=le32(data.data()+8),blocks=le32(data.data()+12),desc_bytes=le32(data.data()+16);if(block!=64||(flags&~3u)||raw!=expected)throw std::runtime_error("unsupported GBP4 header");size_t p=20;if(p+desc_bytes>data.size())throw std::runtime_error("truncated GBP4 descriptors");std::vector<uint8_t> modes(blocks,0);
    if(flags&1){size_t end=p+desc_bytes,pos=0;while(p<end){if(p+2>end)throw std::runtime_error("bad GBP4 RLE");uint8_t m=data[p++];size_t run=size_t(data[p++])+1;if(m>9||pos+run>blocks)throw std::runtime_error("bad GBP4 RLE mode");std::fill(modes.begin()+pos,modes.begin()+pos+run,m);pos+=run;}if(pos!=blocks)throw std::runtime_error("short GBP4 RLE");}else{if(desc_bytes!=(blocks+1)/2)throw std::runtime_error("bad GBP4 descriptor size");for(uint32_t i=0;i<blocks;i++){uint8_t b=data[p+i/2];modes[i]=(i&1)?(b>>4):(b&15);if(modes[i]>9)throw std::runtime_error("bad GBP4 mode");}p+=desc_bytes;}
    out.assign(size_t(blocks)*64,0);for(int w=1;w<=8;w++){size_t rowbytes=size_t(8*w);for(uint32_t bi=0;bi<blocks;bi++)if(modes[bi]==w){if(p+rowbytes>data.size())throw std::runtime_error("truncated GBP4 bitstream");uint64_t acc=0;int bits=0;size_t q=p;for(int j=0;j<64;j++){while(bits<w){acc|=uint64_t(data[q++])<<bits;bits+=8;}uint8_t code=uint8_t(acc&((1u<<w)-1u));acc>>=w;bits-=w;out[size_t(bi)*64+j]=uint8_t(unzig(code));}p+=rowbytes;}}
    for(uint32_t bi=0;bi<blocks;bi++)if(modes[bi]==9){if(p+8>data.size())throw std::runtime_error("truncated GBP4 sparse bitmap");uint8_t mask[8];std::memcpy(mask,data.data()+p,8);p+=8;for(int j=0;j<64;j++)if(mask[j>>3]&(1u<<(j&7))){if(p>=data.size())throw std::runtime_error("truncated GBP4 sparse values");out[size_t(bi)*64+j]=uint8_t(unzig(data[p++]));}}
    if(p!=data.size())throw std::runtime_error("trailing GBP4 bytes");out.resize(expected);if((flags&2u)&&!out.empty()){uint8_t acc=out[0];for(size_t i=1;i<out.size();i++){acc=uint8_t(int(acc)+int(out[i]));out[i]=acc;}}
}

struct BitReader{
    const uint8_t* p;size_t bytes;size_t bit=0;
    BitReader(const uint8_t* pp,size_t n):p(pp),bytes(n){}
    int get1(){if(bit>=bytes*8)throw std::runtime_error("truncated GBP6 Rice stream");int v=(p[bit>>3]>>(bit&7))&1;bit++;return v;}
    uint32_t getn(int n){uint32_t v=0;for(int i=0;i<n;i++)v|=uint32_t(get1())<<i;return v;}
    size_t used_bytes()const{return (bit+7)/8;}
};
struct DecChunk{uint32_t first=0;uint16_t n=0,desc_bytes=0;uint32_t payload_bytes=0;uint8_t flags=0;size_t desc_pos=0,payload_pos=0;};
static void unpack_gbp56(const std::vector<uint8_t>& data,std::vector<uint8_t>& out,size_t expected){
    bool rice6=data.size()>=4&&std::memcmp(data.data(),"GBP6",4)==0;bool old5=data.size()>=4&&std::memcmp(data.data(),"GBP5",4)==0;if(data.size()<20||(!rice6&&!old5))throw std::runtime_error("bad GBP5/6 payload");uint16_t block=le16(data.data()+4),gflags=le16(data.data()+6);uint32_t raw=le32(data.data()+8),blocks=le32(data.data()+12);uint16_t chunk_blocks=le16(data.data()+16);if(block!=64||gflags!=0||raw!=expected||chunk_blocks==0)throw std::runtime_error("unsupported GBP5 header");
    size_t p=20;uint32_t first=0;std::vector<DecChunk> chunks;while(first<blocks){if(p+12>data.size())throw std::runtime_error("truncated GBP5 chunk header");DecChunk c;c.first=first;c.n=le16(data.data()+p);c.desc_bytes=le16(data.data()+p+2);c.payload_bytes=le32(data.data()+p+4);c.flags=data[p+8];p+=12;if(c.n==0||c.n>chunk_blocks||(c.flags&~3u))throw std::runtime_error("bad GBP5 chunk");c.desc_pos=p;if(p+c.desc_bytes>data.size())throw std::runtime_error("truncated GBP5 descriptors");p+=c.desc_bytes;c.payload_pos=p;if(p+c.payload_bytes>data.size())throw std::runtime_error("truncated GBP5 payload");p+=c.payload_bytes;chunks.push_back(c);first+=c.n;if(first>blocks)throw std::runtime_error("too many GBP5 blocks");}if(first!=blocks||p!=data.size())throw std::runtime_error("GBP5 length mismatch");
    out.assign(size_t(blocks)*64,0);
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic,1) if(chunks.size()>4)
#endif
    for(long long ci=0;ci<(long long)chunks.size();ci++){
        const DecChunk& c=chunks[size_t(ci)];std::vector<uint8_t> modes(c.n,0);size_t dp=c.desc_pos;
        if(c.flags&1){size_t end=dp+c.desc_bytes,pos=0;while(dp<end){if(dp+2>end)continue;uint8_t m=data[dp++];size_t run=size_t(data[dp++])+1;if(m>(rice6?15:9)||pos+run>c.n){pos=c.n+1;break;}std::fill(modes.begin()+pos,modes.begin()+pos+run,m);pos+=run;}if(pos!=c.n){/* error caught below via mode/payload consistency */}}
        else{if(c.desc_bytes!=(c.n+1)/2)continue;for(uint16_t i=0;i<c.n;i++){uint8_t b=data[dp+i/2];modes[i]=(i&1)?(b>>4):(b&15);}}
        size_t pp=c.payload_pos,endp=pp+c.payload_bytes;
        for(uint16_t li=0;li<c.n;li++){uint8_t mode=modes[li];size_t base=size_t(c.first+li)*64;if(mode>(rice6?15:9)){pp=endp+1;break;}if(mode>=1&&mode<=8){size_t need=size_t(8*mode);if(pp+need>endp){pp=endp+1;break;}uint64_t acc=0;int bits=0;size_t q=pp;for(int j=0;j<64;j++){while(bits<mode){acc|=uint64_t(data[q++])<<bits;bits+=8;}uint8_t code=uint8_t(acc&((1u<<mode)-1u));acc>>=mode;bits-=mode;out[base+j]=uint8_t(unzig(code));}pp+=need;}else if(mode==9){if(pp+8>endp){pp=endp+1;break;}uint8_t mask[8];std::memcpy(mask,data.data()+pp,8);pp+=8;for(int j=0;j<64;j++)if(mask[j>>3]&(1u<<(j&7))){if(pp>=endp){pp=endp+1;break;}out[base+j]=uint8_t(unzig(data[pp++]));}if(pp>endp)break;}else if(rice6&&mode>=10&&mode<=15){int k=mode-10;try{BitReader br(data.data()+pp,endp-pp);for(int j=0;j<64;j++){int q=0;while(br.get1()==0){if(++q>255){pp=endp+1;break;}}if(pp>endp)break;uint32_t rem=k?br.getn(k):0;uint32_t z=(uint32_t(q)<<k)|rem;if(z>255){pp=endp+1;break;}out[base+j]=uint8_t(unzig(uint8_t(z)));}if(pp>endp)break;pp+=br.used_bytes();}catch(...){pp=endp+1;break;}}
            if(c.flags&2){size_t valid=std::min<size_t>(64,expected>base?expected-base:0);if(valid){uint8_t acc=out[base];for(size_t j=1;j<valid;j++){acc=uint8_t(int(acc)+int(out[base+j]));out[base+j]=acc;}}}
        }
        // malformed chunks are detected after the parallel section by reparsing in strict mode below if needed.
    }
    out.resize(expected);
    // Cheap strict structural validation, without repeating bit decode.
    for(const auto& c:chunks){std::vector<uint8_t> modes(c.n);size_t dp=c.desc_pos;if(c.flags&1){size_t end=dp+c.desc_bytes,pos=0;while(dp<end){if(dp+2>end)throw std::runtime_error("bad GBP5 RLE");uint8_t m=data[dp++];size_t run=size_t(data[dp++])+1;if(m>(rice6?15:9)||pos+run>c.n)throw std::runtime_error("bad GBP5 RLE mode");std::fill(modes.begin()+pos,modes.begin()+pos+run,m);pos+=run;}if(pos!=c.n)throw std::runtime_error("short GBP5 RLE");}else{if(c.desc_bytes!=(c.n+1)/2)throw std::runtime_error("bad GBP5 descriptor size");for(uint16_t i=0;i<c.n;i++){uint8_t b=data[dp+i/2];modes[i]=(i&1)?(b>>4):(b&15);if(modes[i]>(rice6?15:9))throw std::runtime_error("bad GBP5/6 mode");}}
        size_t need=0;for(uint8_t m:modes){if(m>=1&&m<=8)need+=size_t(8*m);else if(m==9){/* sparse variable: validate by walking */}}
        size_t pp=c.payload_pos,endp=pp+c.payload_bytes;for(uint8_t m:modes){if(m>=1&&m<=8){size_t n=size_t(8*m);if(pp+n>endp)throw std::runtime_error("truncated GBP5 bitstream");pp+=n;}else if(m==9){if(pp+8>endp)throw std::runtime_error("truncated GBP5 sparse bitmap");uint8_t mask[8];std::memcpy(mask,data.data()+pp,8);pp+=8;int nz=0;for(int k=0;k<8;k++)nz+=popcount8(mask[k]);if(pp+size_t(nz)>endp)throw std::runtime_error("truncated GBP5 sparse values");pp+=size_t(nz);}else if(rice6&&m>=10&&m<=15){int k=m-10;BitReader br(data.data()+pp,endp-pp);for(int j=0;j<64;j++){int q=0;while(br.get1()==0){if(++q>255)throw std::runtime_error("invalid GBP6 Rice quotient");}uint32_t rem=k?br.getn(k):0;if(((uint32_t(q)<<k)|rem)>255)throw std::runtime_error("invalid GBP6 Rice value");}pp+=br.used_bytes();}}if(pp!=endp)throw std::runtime_error("trailing GBP5/6 chunk bytes");}
}

static void intra_restore(const std::vector<uint8_t>& r,std::vector<uint8_t>& f,int w,int h){f.resize(r.size());int cw=w/2,ch=h/2;size_t pos=0;auto plane=[&](int pw,int ph){size_t base=pos;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if((long long)pw*ph>250000)
#endif
    for(int y=0;y<ph;y++){size_t row=base+size_t(y)*pw;uint8_t acc=r[row];f[row]=acc;for(int x=1;x<pw;x++){acc=uint8_t(int(acc)+int(int8_t(r[row+x])));f[row+x]=acc;}}pos+=size_t(pw)*ph;};plane(w,h);plane(cw,ch);plane(cw,ch);}
static void motion_restore(const std::vector<uint8_t>& r,const std::vector<uint8_t>& p,std::vector<uint8_t>& f,int w,int h,int dx,int dy){f.resize(r.size());size_t pos=0;int cw=w/2,ch=h/2;auto plane=[&](int pw,int ph,int mdx,int mdy){size_t base=pos;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if((long long)pw*ph>250000)
#endif
    for(int y=0;y<ph;y++){int sy=clampi(y-mdy,0,ph-1);for(int x=0;x<pw;x++){int sx=clampi(x-mdx,0,pw-1);size_t di=base+size_t(y)*pw+x,si=base+size_t(sy)*pw+sx;f[di]=uint8_t(int(p[si])+int(int8_t(r[di])));}}pos+=size_t(pw)*ph;};plane(w,h,dx,dy);plane(cw,ch,dx/2,dy/2);plane(cw,ch,dx/2,dy/2);}


static constexpr int MV_BLOCK = 32;
static void unpack_p6(const std::vector<uint8_t>& payload,int w,int h,std::vector<int8_t>& mvx,std::vector<int8_t>& mvy,std::vector<uint8_t>& residual,size_t expected){
    if(payload.size()<20||std::memcmp(payload.data(),"GPM6",4)!=0)throw std::runtime_error("bad GPM6 payload");uint16_t block=le16(payload.data()+4),flags=le16(payload.data()+6),gw=le16(payload.data()+8),gh=le16(payload.data()+10);uint32_t mv_bytes=le32(payload.data()+12),res_bytes=le32(payload.data()+16);int egw=(w+MV_BLOCK-1)/MV_BLOCK,egh=(h+MV_BLOCK-1)/MV_BLOCK;if(block!=MV_BLOCK||(flags&~1u)||gw!=egw||gh!=egh||20ull+mv_bytes+res_bytes!=payload.size())throw std::runtime_error("unsupported GPM6 header");size_t count=size_t(gw)*gh;mvx.assign(count,0);mvy.assign(count,0);size_t p=20,end=20+mv_bytes,pos=0;if(flags&1){while(p<end){if(p+3>end)throw std::runtime_error("bad GPM6 MV RLE");int8_t dx=int8_t(payload[p++]),dy=int8_t(payload[p++]);size_t run=size_t(payload[p++])+1;if(pos+run>count)throw std::runtime_error("bad GPM6 MV run");std::fill(mvx.begin()+pos,mvx.begin()+pos+run,dx);std::fill(mvy.begin()+pos,mvy.begin()+pos+run,dy);pos+=run;}if(pos!=count)throw std::runtime_error("short GPM6 MV map");}else{if(mv_bytes!=count*2)throw std::runtime_error("bad GPM6 raw MV size");for(size_t i=0;i<count;i++){mvx[i]=int8_t(payload[p++]);mvy[i]=int8_t(payload[p++]);}}
    std::vector<uint8_t> rb(payload.begin()+20+mv_bytes,payload.end());unpack_gbp56(rb,residual,expected);
}
static void block_motion_restore(const std::vector<uint8_t>& r,const std::vector<uint8_t>& p,std::vector<uint8_t>& f,int w,int h,const std::vector<int8_t>& mvx,const std::vector<int8_t>& mvy){
    f.resize(r.size());int gw=(w+MV_BLOCK-1)/MV_BLOCK;size_t pos=0;int cw=w/2,ch=h/2;auto plane=[&](int pw,int ph,bool chroma){size_t base=pos;int pblock=chroma?MV_BLOCK/2:MV_BLOCK;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if((long long)pw*ph>250000)
#endif
        for(int y=0;y<ph;y++){int by=y/pblock;for(int x=0;x<pw;x++){int bx=x/pblock,bi=by*gw+bx;int dx=int(mvx[bi]),dy=int(mvy[bi]);if(chroma){dx/=2;dy/=2;}int sy=clampi(y-dy,0,ph-1),sx=clampi(x-dx,0,pw-1);size_t di=base+size_t(y)*pw+x,si=base+size_t(sy)*pw+sx;f[di]=uint8_t(int(p[si])+int(int8_t(r[di])));}}pos+=size_t(pw)*ph;};plane(w,h,false);plane(cw,ch,true);plane(cw,ch,true);
}

struct FrameItem{std::shared_ptr<std::vector<uint8_t>> frame;uint64_t no=0;};
int main(int argc,char** argv){
    if(argc<2){std::cerr<<"usage: ghvdecode INPUT.ghv [--start-frame N] [--verify] [--buffer-frames N] [--frames N] [--no-output]\n";return 2;}
    std::string path=argv[1];uint64_t start_frame=0,limit=0;bool verify=false,no_output=false;int buffer_frames=12;
    for(int i=2;i<argc;i++){std::string a=argv[i];if(a=="--verify")verify=true;else if(a=="--no-output")no_output=true;else if(a=="--start-frame"&&i+1<argc)start_frame=std::stoull(argv[++i]);else if(a=="--buffer-frames"&&i+1<argc)buffer_frames=std::clamp(std::stoi(argv[++i]),1,64);else if(a=="--frames"&&i+1<argc)limit=std::stoull(argv[++i]);}
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    setvbuf(stdout,nullptr,_IOFBF,4*1024*1024);init_crc();
    std::ifstream f(path,std::ios::binary);if(!f){std::cerr<<"cannot open input\n";return 3;}uint8_t h[96];f.read(reinterpret_cast<char*>(h),96);if(f.gcount()!=96||std::memcmp(h,"GHV1",4)!=0){std::cerr<<"bad GHV header\n";return 4;}uint16_t header_size=le16(h+6);uint32_t w=le32(h+12),ht=le32(h+16),frame_count=le32(h+28);uint64_t frames_off=le64(h+56);if(header_size!=96||w==0||ht==0||(w&1)||(ht&1)){std::cerr<<"unsupported GHV dimensions\n";return 4;}size_t frame_size=size_t(w)*ht*3/2;f.seekg(std::streamoff(frames_off),std::ios::beg);
    auto t0=std::chrono::steady_clock::now();uint64_t decoded=0,output=0;std::shared_ptr<std::vector<uint8_t>> prev;
    auto decode_next=[&](uint64_t i)->std::shared_ptr<std::vector<uint8_t>>{
        uint8_t rh[32];f.read(reinterpret_cast<char*>(rh),32);if(f.gcount()!=32||std::memcmp(rh,"VFRM",4)!=0)throw std::runtime_error("bad frame header at "+std::to_string(i));uint32_t no=le32(rh+4);uint8_t typ=rh[16],codec=rh[17];uint16_t meta=le16(rh+18);uint32_t raw=le32(rh+20),packed=le32(rh+24),checksum=le32(rh+28);if(no!=i||(codec!=4&&codec!=5&&codec!=6&&codec!=7&&codec!=8)||raw!=frame_size)throw std::runtime_error("unsupported/corrupt frame "+std::to_string(i));std::vector<uint8_t> payload(packed);if(packed){f.read(reinterpret_cast<char*>(payload.data()),packed);if(size_t(f.gcount())!=packed)throw std::runtime_error("truncated payload");}std::vector<uint8_t> unwrapped;if(typ!=2&&codec<7)unwrap_zp06(payload,unwrapped);const std::vector<uint8_t>& work=(typ==2||codec>=7)?payload:unwrapped;int dx=int8_t(meta&255),dy=int8_t((meta>>8)&255);
        std::shared_ptr<std::vector<uint8_t>> recon;if(typ==2){if(!prev)throw std::runtime_error("repeat without prev");recon=prev;}else{std::vector<uint8_t> res;recon=std::make_shared<std::vector<uint8_t>>();if(codec==8){if(typ!=0&&typ!=1)throw std::runtime_error("bad GHVC8 frame type");ghvc8::decode(work,typ==1?prev.get():nullptr,int(w),int(ht),typ,frame_size,*recon);}else if(codec==7){if(typ!=0&&typ!=1)throw std::runtime_error("bad GHVC7 frame type");ghvc7::decode(work,typ==1?prev.get():nullptr,int(w),int(ht),typ,frame_size,*recon);}else if(codec==6&&typ==1){if(!prev)throw std::runtime_error("P without prev");if(work.size()>=4&&std::memcmp(work.data(),"GPM6",4)==0){std::vector<int8_t> mvx,mvy;unpack_p6(work,int(w),int(ht),mvx,mvy,res,frame_size);block_motion_restore(res,*prev,*recon,int(w),int(ht),mvx,mvy);}else{unpack_gbp56(work,res,frame_size);motion_restore(res,*prev,*recon,int(w),int(ht),0,0);}}else{if(codec==6||codec==5)unpack_gbp56(work,res,frame_size);else unpack_gbp4(work,res,frame_size);if(typ==0)intra_restore(res,*recon,int(w),int(ht));else if(typ==1){if(!prev)throw std::runtime_error("P without prev");motion_restore(res,*prev,*recon,int(w),int(ht),dx,dy);}else throw std::runtime_error("bad frame type");}}if(verify&&crc32(recon->data(),recon->size())!=checksum)throw std::runtime_error("CRC mismatch frame "+std::to_string(i));prev=recon;decoded++;return recon;};
    try{
        if(no_output){for(uint64_t i=0;i<frame_count;i++){auto fr=decode_next(i);if(i>=start_frame){output++;if(limit&&output>=limit)break;}}}
        else{
            std::deque<FrameItem> q;std::mutex m;std::condition_variable cv_not_empty,cv_not_full;bool done=false;std::string err;std::thread producer([&]{try{for(uint64_t i=0;i<frame_count;i++){auto fr=decode_next(i);if(i<start_frame)continue;std::unique_lock<std::mutex> lk(m);cv_not_full.wait(lk,[&]{return q.size()<size_t(buffer_frames);});q.push_back({fr,i});lk.unlock();cv_not_empty.notify_one();if(limit&&++output>=limit)break;}}catch(const std::exception& e){err=e.what();}std::lock_guard<std::mutex> lk(m);done=true;cv_not_empty.notify_all();});
            // Build a real startup cushion before the presentation pipe starts.
            // This is especially useful for 1080p where a single difficult frame
            // should not immediately starve audio/video synchronization.
            {std::unique_lock<std::mutex> lk(m);size_t prefill=size_t(std::min(buffer_frames,std::max(4,buffer_frames/2)));cv_not_empty.wait(lk,[&]{return done||q.size()>=prefill;});}
            uint64_t written=0;while(true){FrameItem it;{std::unique_lock<std::mutex> lk(m);cv_not_empty.wait(lk,[&]{return done||!q.empty();});if(q.empty()&&done)break;it=std::move(q.front());q.pop_front();}cv_not_full.notify_one();if(std::fwrite(it.frame->data(),1,it.frame->size(),stdout)!=it.frame->size()){producer.detach();return 8;}written++;}producer.join();if(!err.empty())throw std::runtime_error(err);output=written;std::fflush(stdout);
        }
    }catch(const std::exception& e){std::cerr<<"decode error: "<<e.what()<<"\n";return 6;}
    double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();std::cerr<<"GHV_DECODE_RESULT decoded="<<decoded<<" output="<<output<<" elapsed="<<sec<<" fps="<<(decoded/std::max(sec,1e-6))<<" buffer="<<buffer_frames<<"\n";return 0;
}
