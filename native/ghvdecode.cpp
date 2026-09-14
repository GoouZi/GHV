#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static uint16_t le16(const uint8_t* p){ return uint16_t(p[0]) | (uint16_t(p[1])<<8); }
static uint32_t le32(const uint8_t* p){ return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); }
static uint64_t le64(const uint8_t* p){ uint64_t v=0; for(int i=0;i<8;i++)v|=uint64_t(p[i])<<(8*i); return v; }
static inline int clampi(int v,int lo,int hi){return std::max(lo,std::min(v,hi));}

static uint32_t crc_table[256];
static void init_crc(){ for(uint32_t i=0;i<256;i++){ uint32_t c=i; for(int j=0;j<8;j++) c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1); crc_table[i]=c; } }
static uint32_t crc32(const uint8_t* p,size_t n){ uint32_t c=0xFFFFFFFFu; for(size_t i=0;i<n;i++)c=crc_table[(c^p[i])&255]^(c>>8); return c^0xFFFFFFFFu; }

static int8_t unzig(uint8_t z){ int v=(int(z)>>1) ^ -int(z&1); return int8_t(v); }

static std::vector<uint8_t> unpack_gbp4(const std::vector<uint8_t>& data,size_t expected){
    if(data.size()<20 || std::memcmp(data.data(),"GBP4",4)!=0) throw std::runtime_error("bad GBP4 payload");
    uint16_t block=le16(data.data()+4), flags=le16(data.data()+6);
    uint32_t raw=le32(data.data()+8), blocks=le32(data.data()+12), desc_bytes=le32(data.data()+16);
    if(block!=64 || (flags&~3u) || raw!=expected) throw std::runtime_error("unsupported GBP4 header");
    size_t p=20; if(p+desc_bytes>data.size())throw std::runtime_error("truncated GBP4 descriptors");
    std::vector<uint8_t> modes(blocks,0);
    if(flags&1){
        size_t end=p+desc_bytes, pos=0;
        while(p<end){ if(p+2>end)throw std::runtime_error("bad GBP4 RLE"); uint8_t m=data[p++]; size_t run=size_t(data[p++])+1; if(m>9||pos+run>blocks)throw std::runtime_error("bad GBP4 RLE mode"); std::fill(modes.begin()+pos,modes.begin()+pos+run,m); pos+=run; }
        if(pos!=blocks)throw std::runtime_error("short GBP4 RLE");
    }else{
        if(desc_bytes!=(blocks+1)/2)throw std::runtime_error("bad GBP4 descriptor size");
        for(uint32_t i=0;i<blocks;i++){ uint8_t b=data[p+i/2]; modes[i]=(i&1)?(b>>4):(b&15); if(modes[i]>9)throw std::runtime_error("bad GBP4 mode"); }
        p+=desc_bytes;
    }
    std::vector<uint8_t> out(size_t(blocks)*64,0);
    for(int w=1;w<=8;w++){
        size_t rowbytes=size_t(8*w);
        for(uint32_t bi=0;bi<blocks;bi++) if(modes[bi]==w){
            if(p+rowbytes>data.size())throw std::runtime_error("truncated GBP4 bitstream");
            uint64_t acc=0; int bits=0; size_t q=p;
            for(int j=0;j<64;j++){
                while(bits<w){ acc|=uint64_t(data[q++])<<bits; bits+=8; }
                uint8_t code=uint8_t(acc & ((1u<<w)-1u)); acc>>=w; bits-=w;
                out[size_t(bi)*64+j]=uint8_t(unzig(code));
            }
            p+=rowbytes;
        }
    }
    for(uint32_t bi=0;bi<blocks;bi++) if(modes[bi]==9){
        if(p+8>data.size())throw std::runtime_error("truncated GBP4 sparse bitmap");
        uint8_t mask[8]; std::memcpy(mask,data.data()+p,8); p+=8;
        for(int j=0;j<64;j++) if(mask[j>>3]&(1u<<(j&7))){ if(p>=data.size())throw std::runtime_error("truncated GBP4 sparse values"); out[size_t(bi)*64+j]=uint8_t(unzig(data[p++])); }
    }
    if(p!=data.size())throw std::runtime_error("trailing GBP4 bytes");
    out.resize(expected);
    if((flags&2u) && !out.empty()){ uint8_t acc=out[0]; for(size_t i=1;i<out.size();i++){ acc=uint8_t(int(acc)+int(out[i])); out[i]=acc; } }
    return out;
}

static void intra_restore(const std::vector<uint8_t>& r,std::vector<uint8_t>& f,int w,int h){
    f.resize(r.size()); int cw=w/2,ch=h/2; size_t pos=0;
    auto plane=[&](int pw,int ph){ for(int y=0;y<ph;y++){ size_t row=pos+size_t(y)*pw; uint8_t acc=r[row]; f[row]=acc; for(int x=1;x<pw;x++){ acc=uint8_t(int(acc)+int(int8_t(r[row+x]))); f[row+x]=acc; }} pos+=size_t(pw)*ph; };
    plane(w,h);plane(cw,ch);plane(cw,ch);
}
static void motion_restore(const std::vector<uint8_t>& r,const std::vector<uint8_t>& p,std::vector<uint8_t>& f,int w,int h,int dx,int dy){
    f.resize(r.size()); size_t pos=0; int cw=w/2,ch=h/2;
    auto plane=[&](int pw,int ph,int mdx,int mdy){ for(int y=0;y<ph;y++){ int sy=clampi(y-mdy,0,ph-1); for(int x=0;x<pw;x++){ int sx=clampi(x-mdx,0,pw-1); size_t di=pos+size_t(y)*pw+x,si=pos+size_t(sy)*pw+sx; f[di]=uint8_t(int(p[si])+int(int8_t(r[di]))); }} pos+=size_t(pw)*ph; };
    plane(w,h,dx,dy);plane(cw,ch,dx/2,dy/2);plane(cw,ch,dx/2,dy/2);
}

int main(int argc,char** argv){
    if(argc<2){ std::cerr<<"usage: ghvdecode INPUT.ghv [--start-frame N] [--verify]\n"; return 2; }
    std::string path=argv[1]; uint64_t start_frame=0; bool verify=false;
    for(int i=2;i<argc;i++){ std::string a=argv[i]; if(a=="--verify")verify=true; else if(a=="--start-frame"&&i+1<argc)start_frame=std::stoull(argv[++i]); }
    std::ifstream f(path,std::ios::binary); if(!f){std::cerr<<"cannot open input\n";return 3;}
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    init_crc();
    uint8_t h[96]; f.read(reinterpret_cast<char*>(h),96); if(f.gcount()!=96||std::memcmp(h,"GHV1",4)!=0){std::cerr<<"bad GHV header\n";return 4;}
    uint8_t minor=h[5]; uint16_t header_size=le16(h+6); uint32_t w=le32(h+12), ht=le32(h+16), frame_count=le32(h+28); uint64_t frames_off=le64(h+56);
    if(header_size!=96 || minor<5 || w==0 || ht==0 || (w&1) || (ht&1)){std::cerr<<"unsupported GHV version/dimensions\n";return 4;}
    size_t frame_size=size_t(w)*ht*3/2; f.seekg(std::streamoff(frames_off),std::ios::beg);
    std::vector<uint8_t> prev,recon,res,payload; uint64_t decoded=0,output=0;
    for(uint64_t i=0;i<frame_count;i++){
        uint8_t rh[32]; f.read(reinterpret_cast<char*>(rh),32); if(f.gcount()!=32||std::memcmp(rh,"VFRM",4)!=0){std::cerr<<"bad frame header at "<<i<<"\n";return 5;}
        uint32_t no=le32(rh+4); uint8_t typ=rh[16],codec=rh[17]; uint16_t meta=le16(rh+18); uint32_t raw=le32(rh+20), packed=le32(rh+24), checksum=le32(rh+28);
        if(no!=i || codec!=4 || raw!=frame_size){std::cerr<<"unsupported/corrupt frame "<<i<<"\n";return 5;}
        payload.resize(packed); if(packed){f.read(reinterpret_cast<char*>(payload.data()),packed); if(size_t(f.gcount())!=packed){std::cerr<<"truncated payload\n";return 5;}}
        int dx=int8_t(meta&255),dy=int8_t((meta>>8)&255);
        try{
            if(typ==2){ if(prev.empty())throw std::runtime_error("repeat without prev"); recon=prev; }
            else { res=unpack_gbp4(payload,frame_size); if(typ==0)intra_restore(res,recon,int(w),int(ht)); else if(typ==1){ if(prev.empty())throw std::runtime_error("P without prev"); motion_restore(res,prev,recon,int(w),int(ht),dx,dy);} else throw std::runtime_error("bad frame type"); }
        }catch(const std::exception& e){std::cerr<<"decode error frame "<<i<<": "<<e.what()<<"\n";return 6;}
        if(verify && crc32(recon.data(),recon.size())!=checksum){std::cerr<<"CRC mismatch frame "<<i<<"\n";return 7;}
        prev=recon; decoded++;
        if(i>=start_frame){ std::cout.write(reinterpret_cast<const char*>(recon.data()),std::streamsize(recon.size())); if(!std::cout){return 8;} output++; }
    }
    std::cout.flush(); std::cerr<<"GHVC4_DECODE_RESULT decoded="<<decoded<<" output="<<output<<"\n"; return 0;
}
