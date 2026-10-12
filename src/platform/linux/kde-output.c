/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded KDE output control. Only the caller's exact portal-created output
 * may be changed. Physical outputs are read solely to choose a free position.
 */
#include <wayland-client.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kde-output-device-v2.h"
#include "kde-output-management-v2.h"
#include "kde-output.h"
struct mode {struct kde_output_device_mode_v2 *obj; int w,h,hz; unsigned flags; int removed;};
struct output {struct kde_output_device_v2 *obj; unsigned id,caps; int x,y,enabled,transform,alive; wl_fixed_t scale; char name[256]; struct mode modes[256]; int count; struct kde_output_device_mode_v2 *current;};
struct context {
 struct output outputs[32]; int count;
 struct kde_output_management_v2 *manager; unsigned version;
 struct wl_display *display; struct wl_registry *registry;
 int64_t deadline;
};
static const struct kde_output_device_mode_v2_listener mode_listener;
static void mode_size(void *data,struct kde_output_device_mode_v2 *obj,int32_t width,int32_t height) {struct mode *m=data; m->w=width; m->h=height;}
static void mode_refresh(void *data,struct kde_output_device_mode_v2 *obj,int32_t refresh) {((struct mode*)data)->hz=refresh;}
static void mode_preferred(void *data,struct kde_output_device_mode_v2 *obj) {}
static void mode_removed(void *data,struct kde_output_device_mode_v2 *obj) {((struct mode*)data)->removed=1;}
static void mode_flags(void *data,struct kde_output_device_mode_v2 *obj,uint32_t flags) {((struct mode*)data)->flags=flags;}
static const struct kde_output_device_mode_v2_listener mode_listener={.size=mode_size,.refresh=mode_refresh,.preferred=mode_preferred,.removed=mode_removed,.flags=mode_flags};
static void output_geometry(void *data,struct kde_output_device_v2 *obj,int32_t x,int32_t y,int32_t physical_width,int32_t physical_height,int32_t subpixel,const char * make,const char * model,int32_t transform) {((struct output*)data)->x=x;((struct output*)data)->y=y;((struct output*)data)->transform=transform;}
static void output_current_mode(void *data,struct kde_output_device_v2 *obj,struct kde_output_device_mode_v2 * mode) {((struct output*)data)->current=mode;}
static void output_mode(void *data,struct kde_output_device_v2 *obj,struct kde_output_device_mode_v2 * mode) {struct output *o=data; if(o->count==256){wl_proxy_destroy((struct wl_proxy*)mode);return;} struct mode *m=&o->modes[o->count++]; m->obj=mode; kde_output_device_mode_v2_add_listener(mode,&mode_listener,m);}
static void output_done(void *data,struct kde_output_device_v2 *obj) {}
static void output_scale(void *data,struct kde_output_device_v2 *obj,wl_fixed_t factor) {((struct output*)data)->scale=factor;}
static void output_edid(void *data,struct kde_output_device_v2 *obj,const char * raw) {}
static void output_enabled(void *data,struct kde_output_device_v2 *obj,int32_t enabled) {((struct output*)data)->enabled=enabled;}
static void output_uuid(void *data,struct kde_output_device_v2 *obj,const char * uuid) {}
static void output_serial_number(void *data,struct kde_output_device_v2 *obj,const char * serialNumber) {}
static void output_eisa_id(void *data,struct kde_output_device_v2 *obj,const char * eisaId) {}
static void output_capabilities(void *data,struct kde_output_device_v2 *obj,uint32_t flags) {((struct output*)data)->caps=flags;}
static void output_overscan(void *data,struct kde_output_device_v2 *obj,uint32_t overscan) {}
static void output_vrr_policy(void *data,struct kde_output_device_v2 *obj,uint32_t vrr_policy) {}
static void output_rgb_range(void *data,struct kde_output_device_v2 *obj,uint32_t rgb_range) {}
static void output_name(void *data,struct kde_output_device_v2 *obj,const char * name) {snprintf(((struct output*)data)->name,256,"%s",name);}
static void output_high_dynamic_range(void *data,struct kde_output_device_v2 *obj,uint32_t hdr_enabled) {}
static void output_sdr_brightness(void *data,struct kde_output_device_v2 *obj,uint32_t sdr_brightness) {}
static void output_wide_color_gamut(void *data,struct kde_output_device_v2 *obj,uint32_t wcg_enabled) {}
static void output_auto_rotate_policy(void *data,struct kde_output_device_v2 *obj,uint32_t policy) {}
static void output_icc_profile_path(void *data,struct kde_output_device_v2 *obj,const char * profile_path) {}
static void output_brightness_metadata(void *data,struct kde_output_device_v2 *obj,uint32_t max_peak_brightness,uint32_t max_frame_average_brightness,uint32_t min_brightness) {}
static void output_brightness_overrides(void *data,struct kde_output_device_v2 *obj,int32_t max_peak_brightness,int32_t max_average_brightness,int32_t min_brightness) {}
static void output_sdr_gamut_wideness(void *data,struct kde_output_device_v2 *obj,uint32_t gamut_wideness) {}
static void output_color_profile_source(void *data,struct kde_output_device_v2 *obj,uint32_t source) {}
static void output_brightness(void *data,struct kde_output_device_v2 *obj,uint32_t brightness) {}
static void output_color_power_tradeoff(void *data,struct kde_output_device_v2 *obj,uint32_t preference) {}
static void output_dimming(void *data,struct kde_output_device_v2 *obj,uint32_t multiplier) {}
static void output_replication_source(void *data,struct kde_output_device_v2 *obj,const char * source) {}
static void output_ddc_ci_allowed(void *data,struct kde_output_device_v2 *obj,uint32_t allowed) {}
static void output_max_bits_per_color(void *data,struct kde_output_device_v2 *obj,uint32_t max_bpc) {}
static void output_max_bits_per_color_range(void *data,struct kde_output_device_v2 *obj,uint32_t min_value,uint32_t max_value) {}
static void output_automatic_max_bits_per_color_limit(void *data,struct kde_output_device_v2 *obj,uint32_t max_bpc_limit) {}
static void output_edr_policy(void *data,struct kde_output_device_v2 *obj,uint32_t policy) {}
static void output_sharpness(void *data,struct kde_output_device_v2 *obj,uint32_t sharpness) {}
static void output_priority(void *data,struct kde_output_device_v2 *obj,uint32_t priority) {}
static void output_auto_brightness(void *data,struct kde_output_device_v2 *obj,uint32_t enabled) {}
static const struct kde_output_device_v2_listener output_listener={.geometry=output_geometry,.current_mode=output_current_mode,.mode=output_mode,.done=output_done,.scale=output_scale,.edid=output_edid,.enabled=output_enabled,.uuid=output_uuid,.serial_number=output_serial_number,.eisa_id=output_eisa_id,.capabilities=output_capabilities,.overscan=output_overscan,.vrr_policy=output_vrr_policy,.rgb_range=output_rgb_range,.name=output_name,.high_dynamic_range=output_high_dynamic_range,.sdr_brightness=output_sdr_brightness,.wide_color_gamut=output_wide_color_gamut,.auto_rotate_policy=output_auto_rotate_policy,.icc_profile_path=output_icc_profile_path,.brightness_metadata=output_brightness_metadata,.brightness_overrides=output_brightness_overrides,.sdr_gamut_wideness=output_sdr_gamut_wideness,.color_profile_source=output_color_profile_source,.brightness=output_brightness,.color_power_tradeoff=output_color_power_tradeoff,.dimming=output_dimming,.replication_source=output_replication_source,.ddc_ci_allowed=output_ddc_ci_allowed,.max_bits_per_color=output_max_bits_per_color,.max_bits_per_color_range=output_max_bits_per_color_range,.automatic_max_bits_per_color_limit=output_automatic_max_bits_per_color_limit,.edr_policy=output_edr_policy,.sharpness=output_sharpness,.priority=output_priority,.auto_brightness=output_auto_brightness};
static void config_applied(void *data,struct kde_output_configuration_v2 *obj) {*(int*)data=1;}
static void config_failed(void *data,struct kde_output_configuration_v2 *obj) {*(int*)data=-1;}
static void config_failure_reason(void *data,struct kde_output_configuration_v2 *obj,const char * reason) {}
static const struct kde_output_configuration_v2_listener config_listener={.applied=config_applied,.failed=config_failed,.failure_reason=config_failure_reason};


static int64_t now_ms(void) {
 struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
 return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void global(void *data,struct wl_registry *r,uint32_t id,const char *iface,uint32_t v) {
 struct context *c=data;
 if(!strcmp(iface,"kde_output_management_v2") && v>=18) {
  c->version=v<19?v:19; c->manager=wl_registry_bind(r,id,&kde_output_management_v2_interface,c->version);
 }
 if(!strcmp(iface,"kde_output_device_v2") && v>=19 && c->count<32) {
  struct output *o=&c->outputs[c->count++];o->id=id;o->alive=1;
  o->obj=wl_registry_bind(r,id,&kde_output_device_v2_interface,v<20?v:20);
  kde_output_device_v2_add_listener(o->obj,&output_listener,o);
 }
}
static void removed(void *data,struct wl_registry *r,uint32_t id) {
 struct context *c=data;
 for(int i=0;i<c->count;i++)if(c->outputs[i].id==id)c->outputs[i].alive=0;
}
static const struct wl_registry_listener registry_listener={global,removed};
static int dispatch_until(struct context *c,int *done) {
 while(!*done) {
  if(wl_display_dispatch_pending(c->display)<0)return 0;
  if(*done)break;
  if(wl_display_prepare_read(c->display)!=0)continue;
  int flushing=wl_display_flush(c->display);
  if(flushing<0 && errno!=EAGAIN){wl_display_cancel_read(c->display);return 0;}
  int64_t left=c->deadline-now_ms();
  if(left<=0){wl_display_cancel_read(c->display);return 0;}
  struct pollfd p={wl_display_get_fd(c->display),POLLIN | (flushing<0?POLLOUT:0),0};
  int result=poll(&p,1,(int)left);
  if(result>0 && (p.revents&POLLIN)) {
   if(wl_display_read_events(c->display)<0)return 0;
  } else wl_display_cancel_read(c->display);
  if(result<0 && errno==EINTR)continue;
  if(result<=0 || (p.revents&(POLLERR|POLLHUP|POLLNVAL)))return 0;
 }
 return 1;
}
static void sync_done(void *data,struct wl_callback *callback,uint32_t serial) {*(int*)data=1;}
static const struct wl_callback_listener sync_listener={sync_done};
static int roundtrip(struct context *c) {
 int done=0;struct wl_callback *callback=wl_display_sync(c->display);
 if(!callback)return 0;
 wl_callback_add_listener(callback,&sync_listener,&done);
 int ok=dispatch_until(c,&done);wl_callback_destroy(callback);return ok;
}
static int apply(struct context *c,struct kde_output_configuration_v2 *config) {
 int result=0;kde_output_configuration_v2_add_listener(config,&config_listener,&result);
 kde_output_configuration_v2_apply(config);
 int ok=dispatch_until(c,&result) && result==1;
 kde_output_configuration_v2_destroy(config);
 return ok && roundtrip(c);
}
static void cleanup(struct context *c) {
 for(int i=0;i<c->count;i++) {
  for(int j=0;j<c->outputs[i].count;j++)kde_output_device_mode_v2_destroy(c->outputs[i].modes[j].obj);
  kde_output_device_v2_destroy(c->outputs[i].obj);
 }
 if(c->manager)kde_output_management_v2_destroy(c->manager);
 if(c->registry)wl_registry_destroy(c->registry);
 if(c->display)wl_display_disconnect(c->display);
 free(c);
}
static struct context *connect_outputs(void) {
 struct context *c=calloc(1,sizeof *c);if(!c)return NULL;
 c->deadline=now_ms()+5000;c->display=wl_display_connect(NULL);
 if(!c->display){cleanup(c);return NULL;}
 c->registry=wl_display_get_registry(c->display);if(!c->registry){cleanup(c);return NULL;}
 wl_registry_add_listener(c->registry,&registry_listener,c);
 for(int i=0;i<3;i++)if(!roundtrip(c)){cleanup(c);return NULL;}
 return c;
}
int umbra_kde_output_supported(void) {
 struct context *c=connect_outputs();if(!c)return 0;
 int ok=c->manager && c->count;cleanup(c);return ok;
}
int umbra_kde_output_resize(const char *name,int w,int h,int hz,int *x,int *y) {
 /* Name comes from the fixed Umbra app identity, never an arbitrary display preference. */
 if(!name || strncmp(name,"Virtual-virtual-xdp-kde-",24) || w<64 || w>8192 || h<64 || h>8192 || hz<1000 || hz>360000)return 0;
 struct context *c=connect_outputs();if(!c)return 0;
 int ok=0;struct output *target=NULL;
 if(!c->manager)goto end;
 for(int i=0;i<c->count;i++)if(c->outputs[i].alive && !strcmp(c->outputs[i].name,name)) {
  if(target)goto end;target=&c->outputs[i];
 }
 if(!target || !(target->caps&0x2000))goto end;
 struct mode *chosen=NULL;
 for(int pass=0;pass<2;pass++) {
  for(int i=0;i<target->count;i++) {
   struct mode *m=&target->modes[i];
   if(!m->removed&&m->w==w&&m->h==h&&m->hz==hz)chosen=m;
  }
  if(chosen)break;
  if(pass)goto end;
  struct kde_mode_list_v2 *list=kde_output_management_v2_create_mode_list(c->manager);
  if(!list)goto end;
  for(int i=0;i<target->count;i++) {
   struct mode *m=&target->modes[i];if(m->removed||!(m->flags&1))continue;
   kde_mode_list_v2_set_resolution(list,m->w,m->h);
   kde_mode_list_v2_set_refresh_rate(list,m->hz);
   kde_mode_list_v2_set_reduced_blanking(list,0);kde_mode_list_v2_add_mode(list);
  }
  kde_mode_list_v2_set_resolution(list,w,h);kde_mode_list_v2_set_refresh_rate(list,hz);
  kde_mode_list_v2_set_reduced_blanking(list,0);kde_mode_list_v2_add_mode(list);
  struct kde_output_configuration_v2 *config=kde_output_management_v2_create_configuration(c->manager);
  if(!config){kde_mode_list_v2_destroy(list);goto end;}
  kde_output_configuration_v2_set_custom_modes(config,target->obj,list);
  int applied=apply(c,config);kde_mode_list_v2_destroy(list);
  if(!applied || !target->alive || !roundtrip(c))goto end;
 }
 if(!target->alive || !chosen || chosen->removed)goto end;
 int64_t right=0;
 for(int i=0;i<c->count;i++) {
  struct output *o=&c->outputs[i];if(o==target||!o->alive||!o->enabled)continue;
  for(int j=0;j<o->count;j++)if(o->modes[j].obj==o->current) {
   int span=(o->transform&1)?o->modes[j].h:o->modes[j].w;
   double scale=o->scale>0?wl_fixed_to_double(o->scale):1.0;
   int64_t edge=o->x+(int64_t)(span/scale+0.5);if(edge>right)right=edge;
  }
 }
 if(right>INT_MAX-w)goto end;
 struct kde_output_configuration_v2 *config=kde_output_management_v2_create_configuration(c->manager);
 if(!config)goto end;
 kde_output_configuration_v2_mode(config,target->obj,chosen->obj);
 kde_output_configuration_v2_position(config,target->obj,(int)right,0);
 if(!apply(c,config))goto end;
 ok=target->alive && !chosen->removed && target->current==chosen->obj && target->x==right && target->y==0;
 if(ok){*x=target->x;*y=target->y;}
end:
 cleanup(c);return ok;
}
