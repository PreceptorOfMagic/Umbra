// SPDX-License-Identifier: GPL-3.0-only
// Offline contract test of Aurora's actual HEVC helpers, never a TV replay.
extern "C" {
#include "coop_splice.h"
#include "coop_poc.h"
#include "coop_conceal.h"
}
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <array>
#include <string>
using Bytes=std::vector<unsigned char>;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Stream {
    Bytes sets;std::vector<Bytes> slices;coop_poc_params_t params{};
    explicit Stream(const char* path) {
        std::ifstream f(path,std::ios::binary);require(bool(f),"input open");Bytes b((std::istreambuf_iterator<char>(f)),{});
        std::vector<std::pair<size_t,size_t>> starts;
        for(size_t i=0;i+3<b.size();i++) {
            size_t n=(!b[i]&&!b[i+1]&&!b[i+2]&&b[i+3]==1)?4:(!b[i]&&!b[i+1]&&b[i+2]==1)?3:0;
            if(n){starts.push_back({i,n});i+=n-1;}
        }
        std::array<Bytes,3> ps;
        for(size_t i=0;i<starts.size();i++) {
            size_t at=starts[i].first+starts[i].second,end=i+1<starts.size()?starts[i+1].first:b.size();
            require(end>at+2,"short NAL");Bytes nal(b.begin()+at,b.begin()+end);unsigned type=(nal[0]>>1)&63;
            if(type>=32&&type<=34){auto& p=ps[type-32];require(p.empty()||p==nal,"parameter sets changed");p=nal;}
            else if(type<32){require(nal[2]&128,"multiple slices unsupported by test");slices.push_back(nal);}
        }
        for(const auto& p:ps){require(!p.empty(),"missing sets");sets.insert(sets.end(),{0,0,0,1});sets.insert(sets.end(),p.begin(),p.end());}
        require(slices.size()>2,"short sequence");require(coop_poc_params_parse(sets.data(),sets.size(),&params),"parse source sets");
    }
};
int main(int argc,char** argv) {
    try {
        require(argc==6,"Usage: core_cadence_smoke left.hevc right.hevc rows|columns schedule.txt output.hevc");
        Stream a(argv[1]),b(argv[2]);require(a.sets==b.sets,"hosts are not parameter-set identical");
        bool columns=std::string(argv[3])=="columns";require(columns||std::string(argv[3])=="rows","layout");
        Bytes sets(65536),scratch(8<<20),work(8<<20),timed(8<<20),tiled(8<<20);coop_poc_params_t composite{};
        size_t n=(columns?coop_nonvcl_tile_columns:coop_nonvcl_tile_rows)(a.sets.data(),a.sets.size(),sets.data(),sets.size());
        require(n&&coop_poc_params_parse(sets.data(),n,&composite),"parse composite sets");sets.resize(n);
        unsigned address=columns?composite.w_ctbs/2:coop_bottom_half_addr(&composite);
        std::ifstream schedule(argv[4]);std::ofstream out(argv[5],std::ios::binary);require(bool(schedule)&&bool(out),"schedule/output open");
        std::array<int,2> previous={-1,-1};unsigned poc=0,holds=0,converted=0;int ai,bi;
        while(schedule>>ai>>bi) {
            require(ai>=0&&bi>=0&&size_t(ai)<a.slices.size()&&size_t(bi)<b.slices.size(),"bad scheduled identity");
            if(!poc)require(ai==0&&bi==0,"bootstrap must use both IDRs");
            out.write(reinterpret_cast<const char*>(sets.data()),std::streamsize(sets.size()));
            for(unsigned side=0;side<2;side++) {
                auto& source=side?b:a;int index=side?bi:ai;
                require(index==previous[side]||index==previous[side]+1,"test cannot skip encoder references");
                const Bytes* selected=&source.slices[size_t(index)];const unsigned char* raw=selected->data();size_t len=selected->size();
                if(index==previous[side]) {
                    // Header donor only: the matching source's first P frame. Its
                    // reference/temporal flags are checked before any concealment.
                    const auto& donor=source.slices[1];coop_slice_info_t info{};
                    require(coop_slice_info(&source.params,donor.data(),donor.size(),&info)&&info.slice_type==1&&info.ref_l0_active==1&&info.first_neg_delta==1&&info.first_neg_used&&!info.rps_unresolved&&!source.params.sps_temporal_mvp_enabled,"unsupported held-pane contract");
                    len=coop_build_skip_slice(&source.params,donor.data(),donor.size(),0,scratch.data(),scratch.size(),work.data(),work.size());
                    require(len!=0,"conceal rejected");raw=work.data();holds++;
                }
                if(poc) {
                    unsigned type=(raw[0]>>1)&63;
                    n=(type==19||type==20)?coop_slice_idr_to_trail_keepref(&source.params,raw,len,poc,timed.data(),timed.size()):coop_poc_rewrite(&source.params,raw,len,poc,timed.data(),timed.size());
                    require(n!=0,"retime/IDR conversion rejected");if(type==19||type==20)converted++;raw=timed.data();len=n;
                }
                n=coop_slice_to_tile(&source.params,&composite,raw,len,side?address:0,tiled.data(),tiled.size());require(n!=0,"tile placement rejected");
                const char prefix[]={0,0,0,1};out.write(prefix,4);out.write(reinterpret_cast<const char*>(tiled.data()),std::streamsize(n));require(bool(out),"output write");
                previous[side]=index;
            }
            poc++;
        }
        require(schedule.eof()&&poc>1,"invalid schedule");
        std::cout<<"CORE_CONTRACT frames="<<poc<<" held_panes="<<holds<<" isolated_idrs="<<converted<<" POC_bits="<<a.params.log2_max_poc_lsb<<" independent_pixel_check_required=1\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
