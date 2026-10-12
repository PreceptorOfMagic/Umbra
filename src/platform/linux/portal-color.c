/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only, bounded output metadata. The compositor supplies the target color
 * volume; the encoding's 10000-nit PQ range is never invented as display data.
 */
#include "portal-color.h"
#include <wayland-client.h>
#include "color-management-v1.h"
#include "xdg-output-unstable-v1.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
struct color_output {
    struct umbra_portal_color *owner;
    struct wl_output *object;
    struct zxdg_output_v1 *logical;
    uint32_t id;
    int alive,positioned,x,y;
};
struct umbra_portal_color {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wp_color_manager_v1 *manager;
    struct zxdg_output_manager_v1 *output_manager;
    struct wp_color_management_output_v1 *color_output;
    struct wp_image_description_v1 *description;
    struct wp_image_description_info_v1 *info;
    struct color_output outputs[32],*selected;
    unsigned count,tf,primaries;
    int ready,done,failed,changed,have_xy,have_luminance;
    int64_t deadline;
    struct umbra_portal_hdr metadata;
};
static int64_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void position(void*d,struct zxdg_output_v1*o,int32_t x,int32_t y){
    struct color_output*out=d;if(out->owner->selected==out && (out->x!=x || out->y!=y))out->owner->changed=1;
    out->x=x;out->y=y;out->positioned=1;
}
static void size(void*d,struct zxdg_output_v1*o,int32_t w,int32_t h){struct color_output*out=d;if(out->owner->selected==out)out->owner->changed=1;}
static void output_done(void*d,struct zxdg_output_v1*o){}
static void output_name(void*d,struct zxdg_output_v1*o,const char*name){}
static const struct zxdg_output_v1_listener output_listener={position,size,output_done,output_name,output_name};
static void global(void*d,struct wl_registry*r,uint32_t id,const char*name,uint32_t version){
    struct umbra_portal_color*c=d;
    if(!strcmp(name,"wl_output") && c->count<32){struct color_output*o=&c->outputs[c->count++];o->owner=c;o->id=id;o->alive=1;o->object=wl_registry_bind(r,id,&wl_output_interface,1);}
    else if(!strcmp(name,"wp_color_manager_v1"))c->manager=wl_registry_bind(r,id,&wp_color_manager_v1_interface,1);
    else if(!strcmp(name,"zxdg_output_manager_v1") && version>=2)c->output_manager=wl_registry_bind(r,id,&zxdg_output_manager_v1_interface,2);
    if(c->selected && !strcmp(name,"wl_output"))c->changed=1; /* topology changes require fresh unambiguous selection */
}
static void removed(void*d,struct wl_registry*r,uint32_t id){struct umbra_portal_color*c=d;for(unsigned i=0;i<c->count;i++)if(c->outputs[i].id==id){c->outputs[i].alive=0;c->changed=1;}}
static const struct wl_registry_listener registry_listener={global,removed};
static void sync_done(void*d,struct wl_callback*c,uint32_t serial){*(int*)d=1;}
static const struct wl_callback_listener sync_listener={sync_done};
static int roundtrip(struct umbra_portal_color*c){
    int done=0,ok=1;struct wl_callback*callback=wl_display_sync(c->display);if(!callback)return 0;
    wl_callback_add_listener(callback,&sync_listener,&done);
    while(!done){
        if(wl_display_dispatch_pending(c->display)<0){ok=0;break;}if(done)break;
        if(wl_display_prepare_read(c->display))continue;
        int flush=wl_display_flush(c->display);int64_t left=c->deadline-now_ms();
        if(left<=0 || (flush<0 && errno!=EAGAIN)){wl_display_cancel_read(c->display);ok=0;break;}
        struct pollfd p={wl_display_get_fd(c->display),POLLIN|(flush<0?POLLOUT:0),0};
        int result=poll(&p,1,(int)left);
        if(result>0 && (p.revents&POLLIN)){if(wl_display_read_events(c->display)<0){ok=0;break;}}
        else wl_display_cancel_read(c->display);
        if(result<0 && errno==EINTR)continue;
        if(result<=0 || (p.revents&(POLLERR|POLLHUP|POLLNVAL))){ok=0;break;}
    }
    wl_callback_destroy(callback);return ok;
}
static void info_done(void*d,struct wp_image_description_info_v1*i){struct umbra_portal_color*c=d;c->done=1;wp_image_description_info_v1_destroy(i);c->info=NULL;}
static void icc(void*d,struct wp_image_description_info_v1*i,int32_t fd,uint32_t bytes){close(fd);((struct umbra_portal_color*)d)->failed=1;}
#define XY_ARGS int32_t rx,int32_t ry,int32_t gx,int32_t gy,int32_t bx,int32_t by,int32_t wx,int32_t wy
static void encoding_xy(void*d,struct wp_image_description_info_v1*i,XY_ARGS){}
static void target_xy(void*d,struct wp_image_description_info_v1*i,XY_ARGS){struct umbra_portal_color*c=d;int32_t xy[]={rx,ry,gx,gy,bx,by,wx,wy};memcpy(c->metadata.xy,xy,sizeof xy);c->have_xy=1;}
static void primaries(void*d,struct wp_image_description_info_v1*i,uint32_t v){((struct umbra_portal_color*)d)->primaries=v;}
static void tf(void*d,struct wp_image_description_info_v1*i,uint32_t v){((struct umbra_portal_color*)d)->tf=v;}
static void power(void*d,struct wp_image_description_info_v1*i,uint32_t v){}
static void encoding_luminance(void*d,struct wp_image_description_info_v1*i,uint32_t min,uint32_t max,uint32_t ref){}
static void target_luminance(void*d,struct wp_image_description_info_v1*i,uint32_t min,uint32_t max){struct umbra_portal_color*c=d;c->metadata.min_luminance=min;c->metadata.max_luminance=max;c->have_luminance=1;}
static void cll(void*d,struct wp_image_description_info_v1*i,uint32_t v){((struct umbra_portal_color*)d)->metadata.max_cll=v;}
static void fall(void*d,struct wp_image_description_info_v1*i,uint32_t v){((struct umbra_portal_color*)d)->metadata.max_fall=v;}
static const struct wp_image_description_info_v1_listener info_listener={info_done,icc,encoding_xy,primaries,power,tf,encoding_luminance,target_xy,target_luminance,cll,fall};
static void description_failed(void*d,struct wp_image_description_v1*i,uint32_t cause,const char*text){((struct umbra_portal_color*)d)->failed=1;}
static void description_ready(void*d,struct wp_image_description_v1*i,uint32_t identity){struct umbra_portal_color*c=d;c->info=wp_image_description_v1_get_information(i);wp_image_description_info_v1_add_listener(c->info,&info_listener,c);}
static const struct wp_image_description_v1_listener description_listener={.failed=description_failed,.ready=description_ready};
static void color_changed(void*d,struct wp_color_management_output_v1*o){struct umbra_portal_color*c=d;c->changed=1;}
static const struct wp_color_management_output_v1_listener color_listener={.image_description_changed=color_changed};
void umbra_portal_color_close(struct umbra_portal_color*c){
    if(!c)return;
    if(c->info)wp_image_description_info_v1_destroy(c->info);
    if(c->description)wp_image_description_v1_destroy(c->description);
    if(c->color_output)wp_color_management_output_v1_destroy(c->color_output);
    for(unsigned i=0;i<c->count;i++){if(c->outputs[i].logical)zxdg_output_v1_destroy(c->outputs[i].logical);wl_output_destroy(c->outputs[i].object);}
    if(c->output_manager)zxdg_output_manager_v1_destroy(c->output_manager);
    if(c->manager)wp_color_manager_v1_destroy(c->manager);
    if(c->registry)wl_registry_destroy(c->registry);
    if(c->display)wl_display_disconnect(c->display);
    free(c);
}
struct umbra_portal_color*umbra_portal_color_open(int x,int y,struct umbra_portal_hdr*metadata){
    struct umbra_portal_color*c=calloc(1,sizeof *c);if(!c)return NULL;
    c->deadline=now_ms()+3000;c->display=wl_display_connect(NULL);if(!c->display)goto fail;
    c->registry=wl_display_get_registry(c->display);wl_registry_add_listener(c->registry,&registry_listener,c);
    if(!roundtrip(c) || !c->manager || !c->output_manager)goto fail;
    for(unsigned i=0;i<c->count;i++){struct color_output*o=&c->outputs[i];o->logical=zxdg_output_manager_v1_get_xdg_output(c->output_manager,o->object);zxdg_output_v1_add_listener(o->logical,&output_listener,o);}
    if(!roundtrip(c))goto fail;
    for(unsigned i=0;i<c->count;i++){struct color_output*o=&c->outputs[i];if(o->alive && o->positioned && o->x==x && o->y==y){if(c->selected)goto fail;c->selected=o;}}
    if(!c->selected)goto fail;
    c->color_output=wp_color_manager_v1_get_output(c->manager,c->selected->object);wp_color_management_output_v1_add_listener(c->color_output,&color_listener,c);
    if(!roundtrip(c))goto fail;
    c->changed=0;
    c->description=wp_color_management_output_v1_get_image_description(c->color_output);wp_image_description_v1_add_listener(c->description,&description_listener,c);
    while(!c->done && !c->failed)if(!roundtrip(c))goto fail;
    if(c->failed || c->changed)goto fail;
    c->metadata.hdr=c->have_xy && c->have_luminance && c->tf==WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ && c->primaries==WP_COLOR_MANAGER_V1_PRIMARIES_BT2020 && c->metadata.max_luminance && c->metadata.max_luminance<=65535 && c->metadata.min_luminance<=65535 && c->metadata.max_cll<=65535 && c->metadata.max_fall<=65535;
    for(unsigned i=0;i<8;i++)if(c->metadata.xy[i]<0 || c->metadata.xy[i]>1000000)c->metadata.hdr=0;
    *metadata=c->metadata;c->ready=1;c->changed=0;return c;
fail:umbra_portal_color_close(c);return NULL;
}
int umbra_portal_color_current(struct umbra_portal_color*c){
    if(!c || c->changed || wl_display_get_error(c->display))return 0;
    while(wl_display_prepare_read(c->display)!=0)if(wl_display_dispatch_pending(c->display)<0)return 0;
    struct pollfd p={wl_display_get_fd(c->display),POLLIN,0};int result=poll(&p,1,0);
    if(result>0 && (p.revents&POLLIN)){if(wl_display_read_events(c->display)<0)return 0;}
    else wl_display_cancel_read(c->display);
    if(result<0 || (p.revents&(POLLERR|POLLHUP|POLLNVAL)) || wl_display_dispatch_pending(c->display)<0)return 0;
    return !c->changed;
}
