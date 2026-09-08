#include "/Users/martymclean/Developer/MediaMuster/src/third_party/xxhash.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
int main() {
    const std::vector<std::size_t> lengths={0,1,2,3,4,7,8,9,15,16,17,31,32,63,64,65,127,128,129,239,240,241,255,256,257,511,512,1023,1024,1025,4194303,4194304,4194305,8388609};
    const std::vector<std::size_t> chunks={1,3,16,63,64,255,256,257,4194304};
    unsigned cases=0;
    for(const auto length:lengths) {
        // Offset input by one byte while keeping the end at the ASan allocation boundary.
        std::unique_ptr<unsigned char[]> allocation(new unsigned char[length+1]);
        unsigned char *const input=allocation.get()+1;
        for(std::size_t i=0;i<length;++i) input[i]=static_cast<unsigned char>((i*131U)^(i>>7U));
        const auto expected=XXH3_64bits(input,length);
        for(const auto chunk:chunks) {
            if(length>1025 && chunk<63) continue;
            std::unique_ptr<XXH3_state_t,decltype(&XXH3_freeState)> state(XXH3_createState(),XXH3_freeState);
            if(!state || XXH3_64bits_reset(state.get())!=XXH_OK) return 2;
            if(XXH3_64bits_update(state.get(),nullptr,0)!=XXH_OK) return 3;
            for(std::size_t offset=0;offset<length;) {
                const auto size=std::min(chunk,length-offset);
                if(XXH3_64bits_update(state.get(),input+offset,size)!=XXH_OK) return 4;
                offset+=size;
            }
            if(XXH3_64bits_digest(state.get())!=expected) { std::cerr<<"mismatch length="<<length<<" chunk="<<chunk<<'\n';return 5; }
            ++cases;
        }
    }
    std::cout<<"version="<<XXH_versionNumber()<<" cases="<<cases<<" streaming_equals_one_shot=true sanitizer_failures=0\n";
}
