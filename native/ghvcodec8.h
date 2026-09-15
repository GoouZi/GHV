#pragma once

#include "ghvcodec7.h"
#include <atomic>
#include <chrono>
#include <cmath>

// GHVC8 keeps the proven GTC7 transform syntax for I frames and introduces a
// GTP8 P-frame profile: 16x16 local motion, spatial median MV prediction,
// delta-coded vectors, exact coefficient-rate/reconstruction-distortion mode
// selection, and zero-residual SKIP blocks.  Motion is always even-pixel so
// YUV420 chroma prediction remains deterministic and cheap.
namespace ghvc8 {

using ghvc7::Candidate;
static constexpr uint8_t PMAGIC[4] = {'G','T','P','8'};

struct MV { int8_t x=0,y=0; };

struct Profile {
    std::atomic<uint64_t> motion_search_ns{0};
    std::atomic<uint64_t> rd_ns{0};
    uint64_t mv_entropy_ns=0;
    uint64_t transform_quant_ns=0;
    uint64_t reconstruction_ns=0;
    uint64_t coeff_entropy_ns=0;
    uint64_t motion_decode_ns=0;
    uint64_t coeff_decode_ns=0;
    uint64_t inverse_recon_ns=0;
    uint64_t p_frames=0;
};

using ProfileClock = std::chrono::steady_clock;
inline uint64_t profile_ns(ProfileClock::time_point a,ProfileClock::time_point b){
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count());
}

inline int median3(int a,int b,int c){
    return a+b+c-std::min(a,std::min(b,c))-std::max(a,std::max(b,c));
}
inline MV predictor(const std::vector<MV>& mv,int mx,int my,int nx){
    MV z{},l=z,t=z,tr=z;
    if(mx>0)l=mv[size_t(my)*nx+mx-1];
    if(my>0)t=mv[size_t(my-1)*nx+mx];
    if(my>0&&mx+1<nx)tr=mv[size_t(my-1)*nx+mx+1]; else tr=t;
    return {int8_t(median3(l.x,t.x,tr.x)),int8_t(median3(l.y,t.y,tr.y))};
}
inline int ref_pixel(const std::vector<uint8_t>& prev,size_t base,int pw,int ph,
                     int x,int y,int dx,int dy){
    int sx=std::clamp(x+dx,0,pw-1),sy=std::clamp(y+dy,0,ph-1);
    return prev[base+size_t(sy)*pw+sx];
}
inline size_t mv_rate(MV v,MV p){
    int dx=int(v.x)-int(p.x),dy=int(v.y)-int(p.y);
    if(dx==0&&dy==0)return 1;
    return 1+ghvc7::var_size(ghvc7::zig(dx))+ghvc7::var_size(ghvc7::zig(dy));
}
inline size_t level_rate(int run,int level){
    uint32_t z=ghvc7::zig(level);
    return (run<8&&z>=1&&z<=16)?1:1+ghvc7::var_size(uint32_t(run))+ghvc7::var_size(z);
}
inline void put_level(std::vector<uint8_t>& out,int run,int level){
    uint32_t z=ghvc7::zig(level);
    if(run<8&&z>=1&&z<=16)out.push_back(uint8_t(0x80u|(uint32_t(run)<<4)|(z-1)));
    else{out.push_back(0);ghvc7::put_var(out,uint32_t(run));ghvc7::put_var(out,z);}
}
inline void get_level(const std::vector<uint8_t>& in,size_t& p,uint32_t& run,uint32_t& level){
    if(p>=in.size())throw std::runtime_error("truncated GHVC8 level");uint8_t token=in[p++];
    if(token&0x80){run=(token>>4)&7;level=(token&15)+1;}
    else if(token==0){run=ghvc7::get_var(in,p);level=ghvc7::get_var(in,p);}
    else throw std::runtime_error("bad GHVC8 level token");
}
inline int sampled_sad(const std::vector<uint8_t>& frame,const std::vector<uint8_t>& prev,
                       int w,int h,int x0,int y0,int dx,int dy){
    int sad=0;
    for(int y=0;y<16&&y0+y<h;y+=2)for(int x=0;x<16&&x0+x<w;x+=2)
        sad+=std::abs(int(frame[size_t(y0+y)*w+x0+x])-ref_pixel(prev,0,w,h,x0+x,y0+y,dx,dy));
    return sad;
}
inline Candidate make_motion_block(const std::vector<uint8_t>& frame,const std::vector<uint8_t>& prev,
                                   size_t base,int pw,int ph,int x0,int y0,int plane,int quality,MV mv){
    Candidate c;c.mode=0;int16_t residual[64];int coeff[64];
    int dx=plane?int(mv.x)/2:int(mv.x),dy=plane?int(mv.y)/2:int(mv.y);
    for(int y=0;y<8;y++)for(int x=0;x<8;x++){
        int sx=std::min(x0+x,pw-1),sy=std::min(y0+y,ph-1);
        residual[y*8+x]=int16_t(int(frame[base+size_t(sy)*pw+sx])-ref_pixel(prev,base,pw,ph,sx,sy,dx,dy));
    }
    ghvc7::transform(residual,coeff);
    for(int v=0;v<8;v++)for(int u=0;u<8;u++)
        c.q[v*8+u]=ghvc7::div_round(coeff[v*8+u],ghvc7::qstep(quality,plane,u,v));
    for(int i=63;i>=0;i--)if(c.q[ghvc7::ZIGZAG[i]]!=0){c.last=i;break;}
    if(c.last<0){c.bytes=0;return c;}
    c.bytes=1;int pos=0;
    while(pos<=c.last){int run=0;while(pos<=c.last&&c.q[ghvc7::ZIGZAG[pos]]==0){run++;pos++;}
        c.bytes+=level_rate(run,c.q[ghvc7::ZIGZAG[pos]]);pos++;}
    return c;
}
inline uint64_t block_distortion(const Candidate& c,const std::vector<uint8_t>& frame,
                                 const std::vector<uint8_t>& prev,size_t base,int pw,int ph,
                                 int x0,int y0,int plane,int quality,MV mv){
    int coeff[64];int16_t residual[64];
    for(int v=0;v<8;v++)for(int u=0;u<8;u++)coeff[v*8+u]=c.q[v*8+u]*ghvc7::qstep(quality,plane,u,v);
    ghvc7::inverse(coeff,residual);uint64_t sse=0;
    int dx=plane?int(mv.x)/2:int(mv.x),dy=plane?int(mv.y)/2:int(mv.y);
    for(int y=0;y<8&&y0+y<ph;y++)for(int x=0;x<8&&x0+x<pw;x++){
        int pred=ref_pixel(prev,base,pw,ph,x0+x,y0+y,dx,dy);
        int e=int(frame[base+size_t(y0+y)*pw+x0+x])-ghvc7::clamp8(pred+residual[y*8+x]);sse+=uint64_t(e*e);
    }
    return sse;
}
inline void reconstruct_motion_block(const Candidate& c,const std::vector<uint8_t>& prev,
                                     std::vector<uint8_t>& recon,size_t base,int pw,int ph,
                                     int x0,int y0,int plane,int quality,MV mv){
    int coeff[64];int16_t residual[64];
    for(int v=0;v<8;v++)for(int u=0;u<8;u++)coeff[v*8+u]=c.q[v*8+u]*ghvc7::qstep(quality,plane,u,v);
    ghvc7::inverse(coeff,residual);int dx=plane?int(mv.x)/2:int(mv.x),dy=plane?int(mv.y)/2:int(mv.y);
    for(int y=0;y<8&&y0+y<ph;y++)for(int x=0;x<8&&x0+x<pw;x++){
        int pred=ref_pixel(prev,base,pw,ph,x0+x,y0+y,dx,dy);
        recon[base+size_t(y0+y)*pw+x0+x]=uint8_t(ghvc7::clamp8(pred+residual[y*8+x]));
    }
}

inline std::vector<uint8_t> encode(const std::vector<uint8_t>& frame,const std::vector<uint8_t>& prev,
                                   int w,int h,int quality,int range,std::vector<uint8_t>& recon,
                                   uint64_t* zero_blocks=nullptr,uint64_t* nonzero_mv=nullptr,
                                   Profile* profile=nullptr){
    if(profile)profile->p_frames++;
    int nx=(w+15)/16,ny=(h+15)/16;std::vector<MV> mvs(size_t(nx)*ny);
    std::vector<uint8_t> mvbody;mvbody.reserve(mvs.size()*2);
    range=std::clamp(range,0,12);range-=range&1;
    // Conservative quality-first multiplier: rate is considered, but a few
    // saved bytes may not buy a visibly worse reconstructed macroblock.
    const uint64_t rate_lambda=1; // byte cost expressed in squared-error units
    uint64_t nz_count=0;int mbtotal=nx*ny;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) reduction(+:nz_count) if(mbtotal>64)
#endif
    for(int mi=0;mi<mbtotal;mi++){
        int my=mi/nx,mx=mi%nx;MV pred{},best{};int x0=mx*16,y0=my*16;
        auto motion_t0=ProfileClock::now();
        std::vector<MV> candidates; candidates.reserve(16);
        auto add=[&](int x,int y){x=std::clamp(x,-range,range);y=std::clamp(y,-range,range);x-=x&1;y-=y&1;
            MV v{int8_t(x),int8_t(y)};for(auto q:candidates)if(q.x==v.x&&q.y==v.y)return;candidates.push_back(v);};
        add(0,0);
        for(int d=2;d<=range;d+=2){add(pred.x+d,pred.y);add(pred.x-d,pred.y);add(pred.x,pred.y+d);add(pred.x,pred.y-d);}
        if(range>=2){add(2,2);add(2,-2);add(-2,2);add(-2,-2);}
        std::vector<std::pair<int,MV>> scored;scored.reserve(candidates.size());
        for(MV v:candidates)scored.push_back({sampled_sad(frame,prev,w,h,x0,y0,v.x,v.y),v});
        std::sort(scored.begin(),scored.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        candidates.clear();candidates.push_back(MV{});
        for(const auto& sv:scored){if(sv.second.x==0&&sv.second.y==0)continue;candidates.push_back(sv.second);if(candidates.size()==3)break;}
        auto motion_t1=ProfileClock::now();
        if(profile)profile->motion_search_ns.fetch_add(profile_ns(motion_t0,motion_t1),std::memory_order_relaxed);
        auto rd_t0=ProfileClock::now();
        uint64_t best_cost=~uint64_t(0);
        for(MV v:candidates){uint64_t rate=mv_rate(v,pred),dist=0;
            for(int by=0;by<2;by++)for(int bx=0;bx<2;bx++){
                int px=x0+bx*8,py=y0+by*8;if(px>=w||py>=h)continue;
                Candidate c=make_motion_block(frame,prev,0,w,h,px,py,0,quality,v);
                rate+=c.bytes;dist+=block_distortion(c,frame,prev,0,w,h,px,py,0,quality,v);
            }
            int cw=w/2,ch=h/2,cx=mx*8,cy=my*8;size_t cbase=size_t(w)*h;
            if(cx<cw&&cy<ch)for(int plane=1;plane<=2;plane++){
                size_t pb=cbase+size_t(plane-1)*cw*ch;
                Candidate c=make_motion_block(frame,prev,pb,cw,ch,cx,cy,plane,quality,v);
                rate+=c.bytes;dist+=block_distortion(c,frame,prev,pb,cw,ch,cx,cy,plane,quality,v);
            }
            uint64_t cost=dist+rate_lambda*rate;if(cost<best_cost){best_cost=cost;best=v;}
        }
        if(profile)profile->rd_ns.fetch_add(profile_ns(rd_t0,ProfileClock::now()),std::memory_order_relaxed);
        mvs[size_t(mi)]=best;if(best.x||best.y)nz_count++;
    }
    if(nonzero_mv)*nonzero_mv+=nz_count;
    auto mv_t0=ProfileClock::now();
    for(int my=0;my<ny;my++)for(int mx=0;mx<nx;mx++){
        MV pred=predictor(mvs,mx,my,nx),best=mvs[size_t(my)*nx+mx];
        int dx=int(best.x)-int(pred.x),dy=int(best.y)-int(pred.y);
        if(dx==0&&dy==0)mvbody.push_back(0);else{mvbody.push_back(1);ghvc7::put_var(mvbody,ghvc7::zig(dx));ghvc7::put_var(mvbody,ghvc7::zig(dy));}
    }
    if(profile)profile->mv_entropy_ns+=profile_ns(mv_t0,ProfileClock::now());

    uint32_t blocks=ghvc7::block_count(w,h);size_t desc_bytes=(size_t(blocks)+7)/8;
    std::vector<uint8_t> desc(desc_bytes,0),body;body.reserve(frame.size()/10);recon.assign(frame.size(),0);
    uint32_t bi=0;size_t base=0;
    auto plane_fn=[&](int pw,int ph,int plane){int bnx=(pw+7)/8,bny=(ph+7)/8,total=bnx*bny;std::vector<Candidate> cv(static_cast<size_t>(total));
        auto transform_t0=ProfileClock::now();
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(total>128)
#endif
        for(int i=0;i<total;i++){int bx=i%bnx,by=i/bnx;MV v=mvs[size_t(plane?by:by/2)*nx+(plane?bx:bx/2)];cv[size_t(i)]=make_motion_block(frame,prev,base,pw,ph,bx*8,by*8,plane,quality,v);}
        if(profile)profile->transform_quant_ns+=profile_ns(transform_t0,ProfileClock::now());
        auto recon_t0=ProfileClock::now();
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(total>128)
#endif
        for(int i=0;i<total;i++){int bx=i%bnx,by=i/bnx;MV v=mvs[size_t(plane?by:by/2)*nx+(plane?bx:bx/2)];reconstruct_motion_block(cv[size_t(i)],prev,recon,base,pw,ph,bx*8,by*8,plane,quality,v);}
        if(profile)profile->reconstruction_ns+=profile_ns(recon_t0,ProfileClock::now());
        auto entropy_t0=ProfileClock::now();
        for(auto& c:cv){if(c.last<0){desc[bi>>3]|=uint8_t(1u<<(bi&7));if(zero_blocks)(*zero_blocks)++;}
            else{body.push_back(uint8_t(c.last));int pos=0;while(pos<=c.last){int run=0;while(pos<=c.last&&c.q[ghvc7::ZIGZAG[pos]]==0){run++;pos++;}put_level(body,run,c.q[ghvc7::ZIGZAG[pos]]);pos++;}}bi++;}
        if(profile)profile->coeff_entropy_ns+=profile_ns(entropy_t0,ProfileClock::now());
        base+=size_t(pw)*ph;};
    plane_fn(w,h,0);plane_fn(w/2,h/2,1);plane_fn(w/2,h/2,2);
    std::vector<uint8_t> out;out.reserve(24+mvbody.size()+desc.size()+body.size());out.insert(out.end(),PMAGIC,PMAGIC+4);
    out.push_back(16);out.push_back(uint8_t(quality));out.push_back(uint8_t(range));out.push_back(0);
    ghvc7::put32(out,uint32_t(frame.size()));ghvc7::put32(out,blocks);ghvc7::put32(out,uint32_t(mvs.size()));ghvc7::put32(out,uint32_t(mvbody.size()));
    out.insert(out.end(),mvbody.begin(),mvbody.end());out.insert(out.end(),desc.begin(),desc.end());out.insert(out.end(),body.begin(),body.end());return out;
}

inline void decode(const std::vector<uint8_t>& in,const std::vector<uint8_t>* prev,int w,int h,
                   int frame_type,size_t expected,std::vector<uint8_t>& recon,Profile* profile=nullptr){
    if(frame_type==0){ghvc7::decode(in,nullptr,w,h,0,expected,recon);return;}
    if(!prev||in.size()<24||!std::equal(PMAGIC,PMAGIC+4,in.begin())||in[4]!=16||in[7]!=0)throw std::runtime_error("bad GHVC8 P payload");
    int quality=in[5];uint32_t raw=ghvc7::le32(in.data()+8),blocks=ghvc7::le32(in.data()+12),mvc=ghvc7::le32(in.data()+16),mvbytes=ghvc7::le32(in.data()+20);
    int nx=(w+15)/16,ny=(h+15)/16;if(raw!=expected||blocks!=ghvc7::block_count(w,h)||mvc!=uint32_t(nx*ny)||quality<1||quality>100||24+size_t(mvbytes)>in.size())throw std::runtime_error("invalid GHVC8 header");
    size_t p=24,mvend=p+mvbytes;std::vector<MV> mvs(mvc);auto motion_t0=ProfileClock::now();
    for(int my=0;my<ny;my++)for(int mx=0;mx<nx;mx++){MV pred=predictor(mvs,mx,my,nx);if(p>=mvend)throw std::runtime_error("truncated GHVC8 motion");uint8_t tok=in[p++];int dx=0,dy=0;if(tok==1){dx=ghvc7::unzig(ghvc7::get_var(in,p));dy=ghvc7::unzig(ghvc7::get_var(in,p));}else if(tok)throw std::runtime_error("bad GHVC8 motion token");if(p>mvend)throw std::runtime_error("bad GHVC8 motion size");int vx=int(pred.x)+dx,vy=int(pred.y)+dy;if(vx<-12||vx>12||vy<-12||vy>12||(vx&1)||(vy&1))throw std::runtime_error("bad GHVC8 vector");mvs[size_t(my)*nx+mx]={int8_t(vx),int8_t(vy)};}
    if(profile)profile->motion_decode_ns+=profile_ns(motion_t0,ProfileClock::now());
    if(p!=mvend)throw std::runtime_error("trailing GHVC8 motion");size_t desc_start=p,desc_bytes=(size_t(blocks)+7)/8;p+=desc_bytes;if(p>in.size())throw std::runtime_error("truncated GHVC8 descriptors");
    recon.assign(expected,0);uint32_t bi=0;size_t base=0;
    auto plane_fn=[&](int pw,int ph,int plane){int bnx=(pw+7)/8,bny=(ph+7)/8,total=bnx*bny;std::vector<Candidate> cv(static_cast<size_t>(total));
        auto entropy_t0=ProfileClock::now();
        for(int i=0;i<total;i++){Candidate c;c.mode=0;bool zero=(in[desc_start+(bi>>3)]>>(bi&7))&1;c.last=-1;if(!zero){if(p>=in.size())throw std::runtime_error("truncated GHVC8 block");c.last=in[p++];if(c.last>63)throw std::runtime_error("bad GHVC8 coefficient end");int pos=0;while(pos<=c.last){uint32_t run=0,level=0;get_level(in,p,run,level);if(!level||run>uint32_t(c.last-pos))throw std::runtime_error("bad GHVC8 run/level");pos+=int(run);c.q[ghvc7::ZIGZAG[pos]]=ghvc7::unzig(level);pos++;}}cv[size_t(i)]=c;bi++;}
        if(profile)profile->coeff_decode_ns+=profile_ns(entropy_t0,ProfileClock::now());
        auto recon_t0=ProfileClock::now();
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(total>128)
#endif
        for(int i=0;i<total;i++){int bx=i%bnx,by=i/bnx;MV v=mvs[size_t(plane?by:by/2)*nx+(plane?bx:bx/2)];reconstruct_motion_block(cv[size_t(i)],*prev,recon,base,pw,ph,bx*8,by*8,plane,quality,v);}
        if(profile)profile->inverse_recon_ns+=profile_ns(recon_t0,ProfileClock::now());base+=size_t(pw)*ph;};
    plane_fn(w,h,0);plane_fn(w/2,h/2,1);plane_fn(w/2,h/2,2);if(p!=in.size())throw std::runtime_error("trailing GHVC8 payload bytes");
}

} // namespace ghvc8
