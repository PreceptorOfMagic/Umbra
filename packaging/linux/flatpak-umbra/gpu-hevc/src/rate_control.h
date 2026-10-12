// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
// Small experimental feedback controller, not x265's full ABR/VBV algorithm.
// Budgets use this host's elapsed submission time. It never schedules, sleeps,
// drops a capture or consults the other host's rate.
class PictureRateControl {
    double bytesPerSecond=0,nominalSeconds=1.0/60,frameSeconds=1.0/60,debt=0;
    static constexpr unsigned units=4;
    unsigned nextQp[2]={28*units,28*units};
    double bitSlope[2]={1.0/6,1.0/6};
    // Allow spare bandwidth to refine text/gradients and old quantization error.
    // Keeping QP >= 28 left a static pane permanently coarse despite tiny AUs.
    // v12 (2026-09-28): 18 was still a visible floor. MEASURED on the Windows client, same
    // still test pattern, 79 Mbit/s: NVENC panes 43.1-45.6 dB luma PSNR with ZERO spread on
    // flat areas; these v10 panes 40.8-42.1 dB with a fine grain on flat areas (mean
    // deviation 0.18-0.31, p99 1.9-2.5) -- the user's "light static" in PC1+VEGA co-op, the
    // pair where both hosts use this encoder. A still picture stops refining at the floor,
    // so the floor is the ceiling on static quality. Busy content never reaches it.
    static constexpr unsigned minimumQp=8*units;
    // Quality units beyond QP51 cannot change the coded coefficients.
    static constexpr unsigned maximumQuality=51*units;
public:
    PictureRateControl(uint32_t kbps=0,uint32_t fps=0) {
        if(!kbps && !fps)return; // exact fixed-QP component fixtures
        if(kbps<1000 || kbps>1000000 || fps<1 || fps>1000)
            throw std::runtime_error("Invalid GPU bitrate budget");
        bytesPerSecond=double(kbps)*1000/8;nominalSeconds=frameSeconds=1.0/fps;
    }
    bool enabled()const{return bytesPerSecond!=0;}
    double budget(double elapsed,bool /*intra*/) {
        if(!enabled())return 0;
        // Account actual time, but do not turn a slow retry/idle interval into
        // a large next-picture burst. Each host estimates its own steady cadence;
        // this only selects QP, never delays or drops a captured picture.
        const double seconds=std::clamp(elapsed,nominalSeconds/4,std::max(nominalSeconds,0.050));
        debt=std::max(-bytesPerSecond*frameSeconds,debt-bytesPerSecond*seconds);
        // Follow faster submissions immediately; grow the estimate gradually so
        // one expensive scene change cannot fund another oversized encode.
        if(seconds<frameSeconds)frameSeconds=seconds;
        else frameSeconds+=(std::min(seconds,frameSeconds*2)-frameSeconds)/16;
        const double credit=bytesPerSecond*frameSeconds;
        // Intra pictures share the same bitrate-derived target. The former 4x
        // allowance opposed low-delay VBV; no separate access-unit cap is used.
        return std::max(1024.0,credit-debt*0.25);
    }
    unsigned start(bool intra)const{return nextQp[intra?1:0];}
    void learn(bool intra,unsigned earlier,size_t earlierBytes,unsigned current,size_t currentBytes) {
        if(current>earlier && earlierBytes>currentBytes && currentBytes) {
            const double sample=std::log2(double(earlierBytes)/double(currentBytes))*units/(current-earlier);
            bitSlope[intra?1:0]=std::clamp(sample,1.0/12,4.0);
        }
    }
    unsigned raise(unsigned qp,double actual,double wanted,bool intra) const {
        const unsigned step=unsigned(std::max(1.0,std::ceil(units*std::log2(std::max(1.0,actual/wanted))/bitSlope[intra?1:0])));
        return std::min(maximumQuality,qp+step);
    }
    void observe(bool intra,unsigned qp,size_t bytes,double wanted) {
        if(!enabled())return;
        // Bounded rate history, measured in time at the negotiated bitrate.
        // This is feedback state, not a queue or a maximum access-unit size.
        debt=std::min(bytesPerSecond*std::max(nominalSeconds,0.050),debt+double(bytes));
        unsigned next=qp;
        if(double(bytes)>wanted*1.10)next=raise(qp,double(bytes),wanted,intra);
        else if(double(bytes)<wanted*1.0) {
            const unsigned step=unsigned(std::clamp(std::floor(units*std::log2(wanted/std::max(1.0,double(bytes)))/bitSlope[intra?1:0]),1.0,double(3*units)));
            next=qp>minimumQp+step?qp-step:minimumQp;
        }
        nextQp[intra?1:0]=std::clamp(next,minimumQp,maximumQuality);
    }
};
