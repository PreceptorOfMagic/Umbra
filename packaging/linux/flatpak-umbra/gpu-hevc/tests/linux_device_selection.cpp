// SPDX-License-Identifier: GPL-3.0-only
// Selection policy contract only; no encoding or physical multi-GPU claim.
#include "linux_device_selection.h"
#include <algorithm>
#include <iostream>
using aghl::DeviceChoice;
static void require(bool ok) { if(!ok)throw std::runtime_error("selection assertion failed"); }
static void rejects(const std::vector<DeviceChoice>& devices,const std::string& selector) {
    try { (void)aghl::selectDevice(devices,selector); }
    catch(const std::runtime_error& e) { require(std::string(e.what()).find("Available:")!=std::string::npos);return; }
    throw std::runtime_error("ambiguous/missing selector accepted");
}
int main() {
    rejects({},"");
    std::vector<DeviceChoice> hybrid{{"Integrated","0000:00:02.0",true},{"Discrete","0000:01:00.0",false}};
    for(int order=0;order<2;order++) {
        require(hybrid[aghl::selectDevice(hybrid,"")].name=="Discrete");
        require(hybrid[aghl::selectDevice(hybrid,"Integrated")].unified);
        require(hybrid[aghl::selectDevice(hybrid,"pci:0000:01:00.0")].name=="Discrete");
        rejects(hybrid,"Discr");rejects(hybrid,"pci:0000:02:00.0");
        std::reverse(hybrid.begin(),hybrid.end());
    }
    std::vector<DeviceChoice> dual{{"Same GPU","0000:01:00.0",false},{"Same GPU","0000:02:00.0",false}};
    rejects(dual,"");rejects(dual,"Same GPU");
    require(aghl::selectDevice(dual,"pci:0000:02:00.0")==1);
    dual[1].pci=dual[0].pci;rejects(dual,"pci:0000:01:00.0"); // duplicate ICD identity
    require(aghl::selectDevice({{"Only iGPU","",true}},"")==0);
    rejects({{"No PCI","",false}},"pci:");
    std::cout<<"GPU selection policy checks passed (not hardware qualification)\n";
}
