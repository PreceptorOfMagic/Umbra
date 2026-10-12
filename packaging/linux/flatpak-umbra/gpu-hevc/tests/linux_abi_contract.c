/* SPDX-License-Identifier: GPL-3.0-only
 * Manual negative ABI/colour checks. No synthetic frame is ever encoded.
 * Run against the source-built library inside the actual GPU sandbox.
 */
#include "aurora_gpu_hevc_linux.h"
#include <dlfcn.h>
#include <stdio.h>
int main(int argc,char**argv) {
    if(argc!=3)return 2;
    void*lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"%s\n",dlerror());return 1;}
    aghl_session*(*open_session)(const aghl_config*,char*,size_t)=(void*)dlsym(lib,"aghl_open");
    int(*encode)(aghl_session*,const aghl_frame*,uint64_t,uint32_t,agh_packet*,char*,size_t)=(void*)dlsym(lib,"aghl_encode");
    void(*close_session)(aghl_session*)=(void*)dlsym(lib,"aghl_close");
    if(!open_session||!encode||!close_session)return 1;
    char error[1024];
    aghl_config config={sizeof config,1,64,64,10,1,argv[2],1000,60};
    aghl_session*s=open_session(&config,error,sizeof error);
    if(s){close_session(s);return 1;}
    printf("ABI1 rejected: %s\n",error);
    config.abi_version=AGHL_ABI_VERSION;
    unsigned char data[64*64*4]={0};
    for(unsigned test=0;test<3;test++) {
        config.hdr=test==0;
        s=open_session(&config,error,sizeof error);if(!s){fprintf(stderr,"valid open: %s\n",error);return 1;}
        aghl_frame frame={sizeof frame,64,64,test==1?AGHL_XBGR2101010_PQ:AGHL_BGRA8,64*4,sizeof data,data};
        if(test==2)frame.bytes=4;
        agh_packet packet={.size=sizeof packet};
        int rc=encode(s,&frame,0,0,&packet,error,sizeof error);close_session(s);
        if(rc==0 || packet.data || packet.bytes){fprintf(stderr,"rejection failed test%u\n",test);return 1;}
        printf("test%u rejected: %s\n",test,error);
    }
    dlclose(lib);puts("ABI/colour/bounds rejection checks passed");return 0;
}
