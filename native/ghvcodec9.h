#pragma once

#include "ghvcodec8.h"

// GHVC9 milestone 1 keeps GHVC8 reconstruction semantics while making encoder
// search effort explicit and adding a cheap, bounded PackBits-style entropy
// layer for P payloads. Raw payloads use GTP9 and cost exactly the same number
// of bytes as GTP8; GZP9 is selected only when it is smaller.
namespace ghvc9 {

using Profile = ghvc8::Profile;
static constexpr uint8_t PMAGIC[4] = {'G','T','P','9'};
static constexpr uint8_t ZMAGIC[4] = {'G','Z','P','9'};
static constexpr uint8_t CMAGIC[4] = {'G','C','P','9'};
static constexpr uint8_t BMAGIC[4] = {'G','B','P','9'};
static constexpr int LEVEL_RICE_K = 2;

struct BitWriter {
    std::vector<uint8_t>& out;uint64_t acc=0;int bits=0;
    explicit BitWriter(std::vector<uint8_t>& target):out(target){}
    void put(uint32_t value,int count){acc|=uint64_t(value)<<bits;bits+=count;while(bits>=8){out.push_back(uint8_t(acc));acc>>=8;bits-=8;}}
    void unary(uint32_t zeros){while(zeros>=24){put(0,24);zeros-=24;}if(zeros)put(0,int(zeros));put(1,1);}
    void finish(){if(bits)out.push_back(uint8_t(acc));acc=0;bits=0;}
};
struct BitReader {
    const std::vector<uint8_t>& in;size_t p,end;uint64_t acc=0;int bits=0;
    BitReader(const std::vector<uint8_t>& source,size_t start,size_t limit=std::numeric_limits<size_t>::max()):in(source),p(start),end(std::min(limit,source.size())){}
    uint32_t get(int count){while(bits<count){if(p>=end)throw std::runtime_error("truncated GHVC9 bits");acc|=uint64_t(in[p++])<<bits;bits+=8;}uint32_t v=uint32_t(acc&((uint64_t(1)<<count)-1));acc>>=count;bits-=count;return v;}
    bool peek16(uint16_t& value){while(bits<16&&p<end){acc|=uint64_t(in[p++])<<bits;bits+=8;}if(bits<16)return false;value=uint16_t(acc);return true;}
    void drop(int count){acc>>=count;bits-=count;}
    uint32_t unary31(){
        uint32_t total=0;
        for(;;){
            if(bits==0){if(p>=end)throw std::runtime_error("truncated GHVC9 unary");acc=in[p++];bits=8;}
            if(acc){int zeros=0;while(zeros<bits&&((acc>>zeros)&1u)==0)zeros++;
                if(total+uint32_t(zeros)>31)throw std::runtime_error("bad GHVC9 unary");
                acc>>=zeros+1;bits-=zeros+1;return total+uint32_t(zeros);}
            total+=uint32_t(bits);if(total>31)throw std::runtime_error("bad GHVC9 unary");acc=0;bits=0;
        }
    }
};
struct FastToken {uint16_t level=0;uint8_t run=0,bits=0;};
inline const std::array<FastToken,65536>& fast_tokens(){
    static const std::array<FastToken,65536> table=[](){std::array<FastToken,65536> t{};
        for(uint32_t word=0;word<65536;word++){int at=0,run=0;while(at<16&&((word>>at)&1u)==0){run++;at++;}if(at>=16||run>=7)continue;at++;int q=0;while(at+q<16&&((word>>(at+q))&1u)==0)q++;if(at+q+1+LEVEL_RICE_K>16||q>=31)continue;const uint32_t z=(uint32_t(q)<<LEVEL_RICE_K)|((word>>(at+q+1))&((1u<<LEVEL_RICE_K)-1));if(!z)continue;t[word]={uint16_t(z),uint8_t(run),uint8_t(at+q+1+LEVEL_RICE_K)};}return t;}();
    return table;
}

inline void put_run_bits(BitWriter& bw,uint32_t run){if(run<7)bw.unary(run);else{bw.unary(7);bw.put(run,6);}}
inline uint32_t get_run_bits(BitReader& br){uint32_t run=0;while(!br.get(1)){if(++run>7)throw std::runtime_error("bad GHVC9 run unary");}return run<7?run:br.get(6);}
inline void put_level_bits(BitWriter& bw,uint32_t z){uint32_t q=z>>LEVEL_RICE_K;if(q<31){bw.unary(q);bw.put(z&((1u<<LEVEL_RICE_K)-1),LEVEL_RICE_K);}else{bw.unary(31);bw.put(z,32);}}
inline uint32_t get_level_bits(BitReader& br){uint32_t q=br.unary31();uint32_t z=q<31?((q<<LEVEL_RICE_K)|br.get(LEVEL_RICE_K)):br.get(32);if(!z)throw std::runtime_error("zero GHVC9 level");return z;}

inline void put32(std::vector<uint8_t>& out,uint32_t value) {
    for(int i=0;i<4;i++) out.push_back(uint8_t(value>>(8*i)));
}

inline int dc_predict(const std::vector<int>& dc,int bx,int by,int bnx) {
    const int left=bx?dc[size_t(by)*bnx+bx-1]:0;
    const int top=by?dc[size_t(by-1)*bnx+bx]:0;
    const int tl=(bx&&by)?dc[size_t(by-1)*bnx+bx-1]:0;
    return ghvc8::median3(left,top,tl);
}

inline std::vector<uint8_t> dc_pack(const std::vector<uint8_t>& raw,int w,int h) {
    if(raw.size()<24) throw std::runtime_error("short GHVC9 source payload");
    const uint32_t blocks=ghvc7::le32(raw.data()+12),mvbytes=ghvc7::le32(raw.data()+20);
    const size_t desc_start=24+size_t(mvbytes),desc_bytes=(size_t(blocks)+7)/8;
    if(desc_start+desc_bytes>raw.size() || blocks!=ghvc7::block_count(w,h))
        throw std::runtime_error("invalid GHVC9 source payload");
    std::vector<uint8_t> out(raw.begin(),raw.begin()+desc_start+desc_bytes);
    std::copy(CMAGIC,CMAGIC+4,out.begin());
    size_t p=desc_start+desc_bytes;uint32_t bi=0;
    auto plane=[&](int pw,int ph){
        const int bnx=(pw+7)/8,bny=(ph+7)/8;std::vector<int> dc(size_t(bnx)*bny,0);
        for(int by=0;by<bny;by++)for(int bx=0;bx<bnx;bx++,bi++){
            const bool zero=(raw[desc_start+(bi>>3)]>>(bi&7))&1;
            if(zero)continue;
            if(p>=raw.size())throw std::runtime_error("truncated GHVC9 source block");
            const int last=raw[p++];if(last>63)throw std::runtime_error("bad GHVC9 source end");
            int q[64]={0};int pos=0;
            while(pos<=last){uint32_t run=0,level=0;ghvc8::get_level(raw,p,run,level);
                if(!level||run>uint32_t(last-pos))throw std::runtime_error("bad GHVC9 source level");
                pos+=int(run);q[ghvc7::ZIGZAG[pos]]=ghvc7::unzig(level);pos++;}
            const int pred=dc_predict(dc,bx,by,bnx),delta=q[0]-pred;uint8_t mode=0;
            if(q[0]==0)mode=0;else if(delta==0)mode=1;else if(delta>=-64&&delta<=63)mode=2;else mode=3;
            out.push_back(uint8_t(last|(mode<<6)));
            if(mode==2)out.push_back(uint8_t(delta+64));
            else if(mode==3)ghvc7::put_var(out,ghvc7::zig(delta));
            pos=1;
            while(pos<=last){int run=0;while(pos<=last&&q[ghvc7::ZIGZAG[pos]]==0){run++;pos++;}
                if(pos<=last){ghvc8::put_level(out,run,q[ghvc7::ZIGZAG[pos]]);pos++;}}
            dc[size_t(by)*bnx+bx]=q[0];
        }
    };
    plane(w,h);plane(w/2,h/2);plane(w/2,h/2);
    if(p!=raw.size()||bi!=blocks)throw std::runtime_error("trailing GHVC9 source bytes");
    return out;
}

inline std::vector<uint8_t> dc_unpack(const std::vector<uint8_t>& in,int w,int h,size_t expected_frame_bytes) {
    if(in.size()<24||!std::equal(CMAGIC,CMAGIC+4,in.begin()))throw std::runtime_error("bad GHVC9 DC payload");
    const uint32_t blocks=ghvc7::le32(in.data()+12),mvbytes=ghvc7::le32(in.data()+20);
    const size_t desc_start=24+size_t(mvbytes),desc_bytes=(size_t(blocks)+7)/8;
    if(desc_start+desc_bytes>in.size()||blocks!=ghvc7::block_count(w,h)||in.size()>expected_frame_bytes*4+65536)
        throw std::runtime_error("invalid GHVC9 DC header");
    std::vector<uint8_t> out(in.begin(),in.begin()+desc_start+desc_bytes);
    std::copy(ghvc8::PMAGIC,ghvc8::PMAGIC+4,out.begin());
    size_t p=desc_start+desc_bytes;uint32_t bi=0;
    auto plane=[&](int pw,int ph){
        const int bnx=(pw+7)/8,bny=(ph+7)/8;std::vector<int> dc(size_t(bnx)*bny,0);
        for(int by=0;by<bny;by++)for(int bx=0;bx<bnx;bx++,bi++){
            const bool zero=(in[desc_start+(bi>>3)]>>(bi&7))&1;
            if(zero)continue;
            if(p>=in.size())throw std::runtime_error("truncated GHVC9 DC block");
            const uint8_t head=in[p++];const int last=head&63,mode=head>>6;
            const int pred=dc_predict(dc,bx,by,bnx);int qdc=0;
            if(mode==1)qdc=pred;
            else if(mode==2){if(p>=in.size())throw std::runtime_error("truncated GHVC9 DC delta");qdc=pred+int(in[p++])-64;}
            else if(mode==3)qdc=pred+ghvc7::unzig(ghvc7::get_var(in,p));
            out.push_back(uint8_t(last));if(qdc)ghvc8::put_level(out,0,qdc);
            int acpos=1;bool first_ac=true;
            while(acpos<=last){uint32_t run=0,level=0;ghvc8::get_level(in,p,run,level);
                if(!level||run>uint32_t(last-acpos))throw std::runtime_error("bad GHVC9 AC level");
                ghvc8::put_level(out,int(run)+((!qdc&&first_ac)?1:0),ghvc7::unzig(level));
                acpos+=int(run)+1;first_ac=false;}
            dc[size_t(by)*bnx+bx]=qdc;
        }
    };
    plane(w,h);plane(w/2,h/2);plane(w/2,h/2);
    if(p!=in.size()||bi!=blocks)throw std::runtime_error("trailing GHVC9 DC bytes");
    return out;
}

inline std::vector<uint8_t> bit_pack(const std::vector<uint8_t>& raw,int w,int h) {
    if(raw.size()<24)throw std::runtime_error("short GHVC9 bit source");
    const uint32_t blocks=ghvc7::le32(raw.data()+12),mvbytes=ghvc7::le32(raw.data()+20);
    const size_t desc_start=24+size_t(mvbytes),desc_bytes=(size_t(blocks)+7)/8;
    if(desc_start+desc_bytes>raw.size()||blocks!=ghvc7::block_count(w,h))throw std::runtime_error("invalid GHVC9 bit source");
    std::vector<size_t> offsets(size_t(blocks)+1);size_t p=desc_start+desc_bytes;
    for(uint32_t bi=0;bi<blocks;bi++){
        offsets[bi]=p;if((raw[desc_start+(bi>>3)]>>(bi&7))&1)continue;
        if(p>=raw.size())throw std::runtime_error("truncated GHVC9 bit block");const int last=raw[p++];if(last>63)throw std::runtime_error("bad GHVC9 bit end");int pos=0;
        while(pos<=last){uint32_t run=0,level=0;ghvc8::get_level(raw,p,run,level);if(!level||run>uint32_t(last-pos))throw std::runtime_error("bad GHVC9 bit level");pos+=int(run)+1;}
    }
    offsets[blocks]=p;if(p!=raw.size())throw std::runtime_error("trailing GHVC9 bit source");
    constexpr uint32_t chunk_blocks=256;const uint32_t chunks=(blocks+chunk_blocks-1)/chunk_blocks;std::vector<std::vector<uint8_t>> encoded(chunks);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(chunks>4)
#endif
    for(long long ci=0;ci<(long long)chunks;ci++){
        const uint32_t first=uint32_t(ci)*chunk_blocks,end=std::min(blocks,first+chunk_blocks);auto& dst=encoded[size_t(ci)];BitWriter bw(dst);
        for(uint32_t bi=first;bi<end;bi++)if(!((raw[desc_start+(bi>>3)]>>(bi&7))&1)){
            size_t q=offsets[bi];const int last=raw[q++];bw.put(uint32_t(last),6);int pos=0;
            while(pos<=last){uint32_t run=0,level=0;ghvc8::get_level(raw,q,run,level);put_run_bits(bw,run);put_level_bits(bw,level);pos+=int(run)+1;}
            if(q!=offsets[bi+1])throw std::runtime_error("GHVC9 block offset mismatch");
        }
        bw.finish();
    }
    std::vector<uint8_t> out(raw.begin(),raw.begin()+desc_start+desc_bytes);std::copy(BMAGIC,BMAGIC+4,out.begin());put32(out,chunks);
    for(const auto& chunk:encoded)put32(out,uint32_t(chunk.size()));for(const auto& chunk:encoded)out.insert(out.end(),chunk.begin(),chunk.end());return out;
}

inline std::vector<uint8_t> bit_unpack(const std::vector<uint8_t>& in,int w,int h,size_t expected_frame_bytes) {
    if(in.size()<24||!std::equal(BMAGIC,BMAGIC+4,in.begin()))throw std::runtime_error("bad GHVC9 bit payload");
    const uint32_t blocks=ghvc7::le32(in.data()+12),mvbytes=ghvc7::le32(in.data()+20);
    const size_t desc_start=24+size_t(mvbytes),desc_bytes=(size_t(blocks)+7)/8;
    if(desc_start+desc_bytes>in.size()||blocks!=ghvc7::block_count(w,h)||in.size()>expected_frame_bytes*4+65536)throw std::runtime_error("invalid GHVC9 bit header");
    std::vector<uint8_t> out(in.begin(),in.begin()+desc_start+desc_bytes);std::copy(ghvc8::PMAGIC,ghvc8::PMAGIC+4,out.begin());BitReader br(in,desc_start+desc_bytes);
    for(uint32_t bi=0;bi<blocks;bi++)if(!((in[desc_start+(bi>>3)]>>(bi&7))&1)){
        int last=int(br.get(6));out.push_back(uint8_t(last));int pos=0;
        while(pos<=last){uint32_t run=get_run_bits(br),level=get_level_bits(br);if(run>uint32_t(last-pos))throw std::runtime_error("bad GHVC9 decoded run");ghvc8::put_level(out,int(run),ghvc7::unzig(level));pos+=int(run)+1;}
    }
    return out;
}

inline void decode_bits(const std::vector<uint8_t>& in,const std::vector<uint8_t>& prev,int w,int h,
                        size_t expected,std::vector<uint8_t>& recon,Profile* profile) {
    if(in.size()<24||!std::equal(BMAGIC,BMAGIC+4,in.begin())||in[4]!=16||in[7]!=0)
        throw std::runtime_error("bad GHVC9 bit payload");
    const int quality=in[5];const uint32_t raw=ghvc7::le32(in.data()+8),blocks=ghvc7::le32(in.data()+12),mvc=ghvc7::le32(in.data()+16),mvbytes=ghvc7::le32(in.data()+20);
    ghvc8::QuantTables qt(quality);const int nx=(w+15)/16,ny=(h+15)/16;
    if(raw!=expected||blocks!=ghvc7::block_count(w,h)||mvc!=uint32_t(nx*ny)||quality<1||quality>100||24+size_t(mvbytes)>in.size())
        throw std::runtime_error("invalid GHVC9 bit header");
    size_t p=24,mvend=p+mvbytes;static thread_local std::vector<ghvc8::MV> mv_scratch;if(mv_scratch.size()<mvc)mv_scratch.resize(mvc);auto& mvs=mv_scratch;
    auto motion_t0=ghvc8::ProfileClock::now();
    for(int my=0;my<ny;my++)for(int mx=0;mx<nx;mx++){
        const ghvc8::MV pred=ghvc8::predictor(mvs,mx,my,nx);if(p>=mvend)throw std::runtime_error("truncated GHVC9 motion");const uint8_t tok=in[p++];int dx=0,dy=0;
        if(tok==1){dx=ghvc7::unzig(ghvc7::get_var(in,p));dy=ghvc7::unzig(ghvc7::get_var(in,p));}else if(tok)throw std::runtime_error("bad GHVC9 motion token");
        if(p>mvend)throw std::runtime_error("bad GHVC9 motion size");const int vx=int(pred.x)+dx,vy=int(pred.y)+dy;
        if(vx<-12||vx>12||vy<-12||vy>12||(vx&1)||(vy&1))throw std::runtime_error("bad GHVC9 vector");mvs[size_t(my)*nx+mx]={int8_t(vx),int8_t(vy)};
    }
    if(p!=mvend)throw std::runtime_error("trailing GHVC9 motion");
    if(profile)profile->motion_decode_ns+=ghvc8::profile_ns(motion_t0,ghvc8::ProfileClock::now());
    const size_t desc_start=p,desc_bytes=(size_t(blocks)+7)/8;p+=desc_bytes;if(p+4>in.size())throw std::runtime_error("truncated GHVC9 descriptors");
    constexpr uint32_t chunk_blocks=256;const uint32_t chunks=ghvc7::le32(in.data()+p);p+=4;
    if(chunks!=(blocks+chunk_blocks-1)/chunk_blocks||p+size_t(chunks)*4>in.size())throw std::runtime_error("bad GHVC9 chunk table");
    std::vector<size_t> chunk_start(chunks),chunk_size(chunks);size_t data_start=p+size_t(chunks)*4,cursor=data_start;
    for(uint32_t ci=0;ci<chunks;ci++){chunk_start[ci]=cursor;chunk_size[ci]=ghvc7::le32(in.data()+p+size_t(ci)*4);cursor+=chunk_size[ci];if(cursor>in.size())throw std::runtime_error("bad GHVC9 chunk size");}
    if(cursor!=in.size())throw std::runtime_error("trailing GHVC9 chunk bytes");
    recon.assign(expected,0);static thread_local std::vector<ghvc7::Candidate> coeff_scratch;if(coeff_scratch.size()<blocks)coeff_scratch.resize(blocks);auto& cv=coeff_scratch;
    const auto coeff_t0=ghvc8::ProfileClock::now();std::atomic<bool> bad{false};
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(chunks>4)
#endif
    for(long long ci=0;ci<(long long)chunks;ci++){
        try{
            const uint32_t first=uint32_t(ci)*chunk_blocks,block_end=std::min(blocks,first+chunk_blocks);BitReader br(in,chunk_start[size_t(ci)],chunk_start[size_t(ci)]+chunk_size[size_t(ci)]);
            for(uint32_t bi=first;bi<block_end;bi++){
                auto& c=cv[bi];if(c.last>=0)for(int old=0;old<=c.last;old++)c.q[ghvc7::ZIGZAG[old]]=0;c.mode=0;c.last=-1;
                const bool zero=(in[desc_start+(bi>>3)]>>(bi&7))&1;
                if(!zero){c.last=int(br.get(6));int pos=0;while(pos<=c.last){uint32_t run=0,level=0;uint16_t word=0;const FastToken* ft=nullptr;if(br.peek16(word))ft=&fast_tokens()[word];
                        if(ft&&ft->bits){run=ft->run;level=ft->level;br.drop(ft->bits);}else{run=get_run_bits(br);level=get_level_bits(br);}
                        if(run>uint32_t(c.last-pos))throw std::runtime_error("bad run");pos+=int(run);c.q[ghvc7::ZIGZAG[pos]]=ghvc7::unzig(level);pos++;}}
            }
            if(br.p!=chunk_start[size_t(ci)]+chunk_size[size_t(ci)])throw std::runtime_error("trailing chunk");
        }catch(...){bad.store(true,std::memory_order_relaxed);}
    }
    if(bad.load(std::memory_order_relaxed))throw std::runtime_error("invalid GHVC9 coefficient chunk");
    if(profile){profile->coeff_decode_ns+=ghvc8::profile_ns(coeff_t0,ghvc8::ProfileClock::now());for(uint32_t bi=0;bi<blocks;bi++){if(cv[bi].last<0)profile->decode_zero_blocks++;else if(cv[bi].last==0)profile->decode_dc_blocks++;}}
    size_t base=0,block_base=0;
    auto plane=[&](int pw,int ph,int plane_index){
        const int bnx=(pw+7)/8,bny=(ph+7)/8,total=bnx*bny;const auto recon_t0=ghvc8::ProfileClock::now();
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(total>128)
#endif
        for(int i=0;i<total;i++){const int bx=i%bnx,by=i/bnx;const ghvc8::MV v=mvs[size_t(plane_index?by:by/2)*nx+(plane_index?bx:bx/2)];ghvc8::reconstruct_motion_block(cv[block_base+size_t(i)],prev,recon,base,pw,ph,bx*8,by*8,plane_index,qt,v);}
        if(profile)profile->inverse_recon_ns+=ghvc8::profile_ns(recon_t0,ghvc8::ProfileClock::now());base+=size_t(pw)*ph;block_base+=size_t(total);
    };
    plane(w,h,0);plane(w/2,h/2,1);plane(w/2,h/2,2);
    if(block_base!=blocks)throw std::runtime_error("GHVC9 block layout mismatch");
}

inline std::vector<uint8_t> packbits(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> body;
    body.reserve(raw.size());
    size_t i=4;
    while(i<raw.size()) {
        size_t run=1;
        while(i+run<raw.size() && raw[i+run]==raw[i] && run<130) run++;
        if(run>=3) {
            body.push_back(uint8_t(0x80u | uint8_t(run-3)));
            body.push_back(raw[i]);
            i+=run;
            continue;
        }
        const size_t start=i;
        i+=run;
        while(i<raw.size() && i-start<128) {
            size_t next=1;
            while(i+next<raw.size() && raw[i+next]==raw[i] && next<3) next++;
            if(next>=3) break;
            if(i-start+next>128) break;
            i+=next;
        }
        const size_t count=i-start;
        body.push_back(uint8_t(count-1));
        body.insert(body.end(),raw.begin()+start,raw.begin()+i);
    }
    if(body.size()+8>=raw.size()) {
        return {};
    }
    std::vector<uint8_t> out;
    out.reserve(body.size()+8);
    out.insert(out.end(),ZMAGIC,ZMAGIC+4);
    put32(out,uint32_t(raw.size()));
    out.insert(out.end(),body.begin(),body.end());
    return out;
}

inline std::vector<uint8_t> pack(const std::vector<uint8_t>& raw,int w,int h) {
    std::vector<uint8_t> plain=raw;std::copy(PMAGIC,PMAGIC+4,plain.begin());
    std::vector<uint8_t> bits=bit_pack(raw,w,h);
    return bits.size()<plain.size()?std::move(bits):std::move(plain);
}

inline std::vector<uint8_t> unpack(const std::vector<uint8_t>& in,size_t expected_frame_bytes) {
    if(in.size()<8 || !std::equal(ZMAGIC,ZMAGIC+4,in.begin()))
        throw std::runtime_error("bad GHVC9 packed payload");
    const uint32_t raw_size=ghvc7::le32(in.data()+4);
    const uint64_t safe_limit=uint64_t(expected_frame_bytes)*4u+65536u;
    if(raw_size<24 || uint64_t(raw_size)>safe_limit)
        throw std::runtime_error("invalid GHVC9 unpacked size");
    std::vector<uint8_t> out;
    out.reserve(raw_size);
    out.insert(out.end(),ghvc8::PMAGIC,ghvc8::PMAGIC+4);
    size_t p=8;
    while(p<in.size() && out.size()<raw_size) {
        const uint8_t token=in[p++];
        if(token&0x80) {
            if(p>=in.size()) throw std::runtime_error("truncated GHVC9 run");
            const size_t count=size_t(token&0x7f)+3;
            if(out.size()+count>raw_size) throw std::runtime_error("GHVC9 run overflow");
            out.insert(out.end(),count,in[p++]);
        } else {
            const size_t count=size_t(token)+1;
            if(p+count>in.size() || out.size()+count>raw_size)
                throw std::runtime_error("truncated GHVC9 literal");
            out.insert(out.end(),in.begin()+p,in.begin()+p+count);
            p+=count;
        }
    }
    if(p!=in.size() || out.size()!=raw_size)
        throw std::runtime_error("invalid GHVC9 packed length");
    return out;
}

inline std::vector<uint8_t> encode(const std::vector<uint8_t>& frame,const std::vector<uint8_t>& prev,
                                   int w,int h,int quality,int range,std::vector<uint8_t>& recon,
                                   uint64_t* zero_blocks=nullptr,uint64_t* nonzero_mv=nullptr,
                                   Profile* profile=nullptr,int nonzero_finalists=2) {
    auto raw=ghvc8::encode(frame,prev,w,h,quality,range,recon,zero_blocks,nonzero_mv,
                           profile,nonzero_finalists);
    return pack(raw,w,h);
}

inline void decode(const std::vector<uint8_t>& in,const std::vector<uint8_t>* prev,int w,int h,
                   int frame_type,size_t expected,std::vector<uint8_t>& recon,Profile* profile=nullptr) {
    if(frame_type==0) {
        ghvc7::decode(in,nullptr,w,h,0,expected,recon);
        return;
    }
    if(in.size()>=4 && std::equal(PMAGIC,PMAGIC+4,in.begin())) {
        ghvc8::decode(in,prev,w,h,frame_type,expected,recon,profile,PMAGIC);
        return;
    }
    if(in.size()>=4 && std::equal(CMAGIC,CMAGIC+4,in.begin())) {
        auto raw=dc_unpack(in,w,h,expected);
        ghvc8::decode(raw,prev,w,h,frame_type,expected,recon,profile);
        return;
    }
    if(in.size()>=4 && std::equal(BMAGIC,BMAGIC+4,in.begin())) {
        if(!prev)throw std::runtime_error("GHVC9 P frame has no reference");
        decode_bits(in,*prev,w,h,expected,recon,profile);
        return;
    }
    auto raw=unpack(in,expected);
    ghvc8::decode(raw,prev,w,h,frame_type,expected,recon,profile);
}

} // namespace ghvc9
