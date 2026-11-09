#include "ghvcodec9.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        for(int k : {1,2,3,4}) {
            std::vector<uint8_t> bytes;
            ghvc9::BitWriter writer(bytes);
            const uint32_t levels[]={1,2,3u<<k,4u<<k,(4u<<k)+1,(5u<<k)+1,
                                     (7u<<k)+1,127,1024,1u<<20};
            for(uint32_t level:levels) ghvc9::write_compact_level(writer,level,k);
            writer.finish();
            ghvc9::BitReader reader(bytes,0);
            for(uint32_t level:levels)
                if(ghvc9::read_compact_level(reader,k)!=level)
                    throw std::runtime_error("compact level boundary mismatch");
        }
        // Ten macroblocks exercise a partial final mask. All vectors and
        // residuals are zero, so both groups require only one mask byte.
        std::vector<uint8_t> flat(80*32*3/2,128), expected, decoded_flat;
        {
            auto image=flat;
            for(size_t i=0;i<image.size();++i) image[i]=uint8_t((i/8+i%19)%256);
            const auto reference=ghvc7::encode(image,nullptr,80,32,78,true,expected);
            auto compact=reference;
            ghvc9::compact_intra(compact);
            if(compact.size()>=reference.size() || ghvc9::expand_intra(compact,80,32,image.size())!=reference)
                throw std::runtime_error("intra coefficient round trip failed");
            ghvc9::decode(compact,nullptr,80,32,0,image.size(),decoded_flat);
            if(decoded_flat!=expected) throw std::runtime_error("compact intra pixels differ");
            compact.push_back(0);
            bool rejected=false;
            try { ghvc9::expand_intra(compact,80,32,image.size()); }
            catch(const std::runtime_error&) { rejected=true; }
            if(!rejected) throw std::runtime_error("trailing intra byte accepted");
        }
        auto raw=ghvc8::encode(flat,flat,80,32,78,0,expected);
        // Build a block sequence whose first chunk contains both large DC
        // values and small AC values, including an AC with no preceding DC.
        {
            auto coeff_source=raw;
            const size_t desc=24+ghvc7::le32(raw.data()+20);
            const uint32_t blocks=ghvc7::le32(raw.data()+12);
            coeff_source.resize(desc);
            coeff_source.resize(desc+(blocks+7)/8,0);
            for(uint32_t i=0;i<blocks;++i) {
                const bool ac=(i%4)==0;
                coeff_source.push_back(ac?8:0);
                if(i%8!=0) ghvc8::put_level(coeff_source,0,60);
                if(ac) ghvc8::put_level(coeff_source,i%8==0?8:7,-1);
            }
            const auto original=ghvc9::bit_pack(coeff_source,80,32);
            auto encoded=ghvc9::bit_pack(coeff_source,80,32,true);
            if((encoded[7]&10)!=10 || encoded.size()>=original.size())
                throw std::runtime_error("expected compact coefficients and a smaller packet");
            std::vector<uint8_t> reference, result;
            ghvc9::decode(original,&flat,80,32,1,flat.size(),reference);
            for(int motion=0;motion<2;++motion) {
                if(motion) ghvc9::compact_motion(encoded);
                ghvc9::decode(encoded,&flat,80,32,1,flat.size(),result);
                if(result!=reference) throw std::runtime_error("coefficient reconstruction mismatch");
            }
        }
        auto packet=ghvc9::bit_pack(raw,80,32);
        ghvc9::compact_motion(packet);
        if (packet[7]!=1 || ghvc7::le32(packet.data()+20)!=2)
            throw std::runtime_error("expected two compact motion masks");
        ghvc9::decode(packet,&flat,80,32,1,flat.size(),decoded_flat);
        if (decoded_flat!=flat) throw std::runtime_error("compact motion mismatch");
        for (uint8_t invalid_mask : {4,1}) {
            auto invalid=packet;
            invalid[25]=invalid_mask; // padding bit or literal beyond the body
            bool rejected=false;
            try { ghvc9::decode(invalid,&flat,80,32,1,flat.size(),decoded_flat); }
            catch (const std::runtime_error&) { rejected=true; }
            if (!rejected) throw std::runtime_error("invalid compact motion accepted");
        }
        // More than eight distinct motion candidates exposed the old zero-limit
        // overflow. Odd macroblock edges also exercise partial luma/chroma blocks.
        for (int width : {32, 34}) {
            const int height = 34;
            std::vector<uint8_t> prev(size_t(width)*height*3/2), frame(prev.size());
            for (size_t i=0;i<prev.size();i++) {
                prev[i]=uint8_t((i*37+i/width*13)%256);
                frame[i]=uint8_t((i*19+i/width*7)%256);
            }
            for (int limit : {0, 1, 7}) {
                std::vector<uint8_t> recon, decoded;
                ghvc9::Profile profile;
                auto payload=ghvc9::encode(frame,prev,width,height,78,12,recon,
                                           nullptr,nullptr,&profile,limit);
                const uint64_t macroblocks=uint64_t((width+15)/16)*((height+15)/16);
                if (profile.rd_candidates.load()!=macroblocks*(limit+1))
                    throw std::runtime_error("motion shortlist limit violated");
                ghvc9::decode(payload,&prev,width,height,1,frame.size(),decoded);
                if (decoded!=recon) throw std::runtime_error("reconstruction mismatch");
                if (limit==0) {
                    std::vector<uint8_t> zero_recon;
                    auto zero_payload=ghvc9::encode(frame,prev,width,height,78,0,zero_recon,
                                                    nullptr,nullptr,nullptr,0);
                    // Search range is metadata; only reconstruction must match.
                    if (zero_recon!=recon) throw std::runtime_error("zero limit searched motion");
                }
            }
        }
        std::cout << "[PASS] motion shortlist bounds and GHVC9 reconstruction\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
