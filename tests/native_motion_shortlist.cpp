#include "ghvcodec9.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        // Ten macroblocks exercise a partial final mask. All vectors and
        // residuals are zero, so both groups require only one mask byte.
        std::vector<uint8_t> flat(80*32*3/2,128), expected, decoded_flat;
        auto raw=ghvc8::encode(flat,flat,80,32,78,0,expected);
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
