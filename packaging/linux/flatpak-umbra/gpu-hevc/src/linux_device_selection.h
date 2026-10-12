// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdexcept>
#include <string>
#include <vector>

namespace aghl {
struct DeviceChoice {
    std::string name, pci;
    bool unified;
};
// Eligible GPU devices only. Memory topology is a selection policy, not a
// performance estimate. Never silently replace an explicit selection.
inline size_t selectDevice(const std::vector<DeviceChoice>& devices,const std::string& selector) {
    std::vector<size_t> matches;
    for(size_t i=0;i<devices.size();++i) {
        const auto& d=devices[i];
        if(selector.empty() || (selector.compare(0,4,"pci:")==0 ?
            (!d.pci.empty() && selector=="pci:"+d.pci) : selector==d.name))matches.push_back(i);
    }
    if(matches.size()>1 && selector.empty()) {
        std::vector<size_t> separate;
        for(auto i:matches)if(!devices[i].unified)separate.push_back(i);
        if(separate.size()==1)return separate.front();
    }
    if(matches.size()==1)return matches.front();
    std::string detail=matches.empty() ? "No matching usable OpenCL GPU" : "Ambiguous OpenCL GPU selection";
    detail+="; set Graphics Adapter to an exact GPU name or DRM render node. Available:";
    for(const auto& d:devices)detail+=" ["+d.name+(d.pci.empty()?"":" pci:"+d.pci)+"]";
    if(devices.empty())detail+=" none (no CPU fallback)";
    throw std::runtime_error(detail);
}
}
