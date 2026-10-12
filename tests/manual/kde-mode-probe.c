/* SPDX-License-Identifier: GPL-3.0-or-later
 * Manual diagnostic. Only changes the exact Umbra portal virtual output. */
#include <wayland-client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "kde-output-device-v2.h"
#include "kde-output-management-v2.h"
struct mode {struct kde_output_device_mode_v2 *obj; int w,h,hz; unsigned flags; int removed;};
struct output {struct kde_output_device_v2 *obj; unsigned id,caps; int x,y; wl_fixed_t scale; char name[256]; struct mode modes[256]; int count; struct kde_output_device_mode_v2 *current;};
static struct output outputs[32]; static int count; static struct kde_output_management_v2 *manager; static unsigned version; static int result;
static const struct kde_output_device_mode_v2_listener mode_listener;
static void mode_size(void *data,struct kde_output_device_mode_v2 *obj,int32_t width,int32_t height) {struct mode *m=data; m->w=width; m->h=height;}
static void mode_refresh(void *data,struct kde_output_device_mode_v2 *obj,int32_t refresh) {((struct mode*)data)->hz=refresh;}
static void mode_preferred(void *data,struct kde_output_device_mode_v2 *obj) {}
static void mode_removed(void *data,struct kde_output_device_mode_v2 *obj) {((struct mode*)data)->removed=1;}
static void mode_flags(void *data,struct kde_output_device_mode_v2 *obj,uint32_t flags) {((struct mode*)data)->flags=flags;}
static const struct kde_output_device_mode_v2_listener mode_listener={.size=mode_size,.refresh=mode_refresh,.preferred=mode_preferred,.removed=mode_removed,.flags=mode_flags};
static void output_geometry(void *data,struct kde_output_device_v2 *obj,int32_t x,int32_t y,int32_t physical_width,int32_t physical_height,int32_t subpixel,const char * make,const char * model,int32_t transform) {((struct output*)data)->x=x;((struct output*)data)->y=y;}
static void output_current_mode(void *data,struct kde_output_device_v2 *obj,struct kde_output_device_mode_v2 * mode) {((struct output*)data)->current=mode;}
static void output_mode(void *data,struct kde_output_device_v2 *obj,struct kde_output_device_mode_v2 * mode) {struct output *o=data; if(o->count==256)exit(2); struct mode *m=&o->modes[o->count++]; m->obj=mode; kde_output_device_mode_v2_add_listener(mode,&mode_listener,m);}
static void output_done(void *data,struct kde_output_device_v2 *obj) {}
static void output_scale(void *data,struct kde_output_device_v2 *obj,wl_fixed_t factor) {((struct output*)data)->scale=factor;}
static void output_edid(void *data,struct kde_output_device_v2 *obj,const char * raw) {}
static void output_enabled(void *data,struct kde_output_device_v2 *obj,int32_t enabled) {}
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
static void config_applied(void *data,struct kde_output_configuration_v2 *obj) {result=1;}
static void config_failed(void *data,struct kde_output_configuration_v2 *obj) {result=-1;}
static void config_failure_reason(void *data,struct kde_output_configuration_v2 *obj,const char * reason) {}
static const struct kde_output_configuration_v2_listener config_listener={.applied=config_applied,.failed=config_failed,.failure_reason=config_failure_reason};

static void global(void *d,struct wl_registry *r,uint32_t id,const char *iface,uint32_t v) {
 if(!strcmp(iface,"kde_output_management_v2")) {version=v<19?v:19;manager=wl_registry_bind(r,id,&kde_output_management_v2_interface,version);printf("manager=%u\n",version);}
 if(!strcmp(iface,"kde_output_device_v2") && v>=2) {if(count==32)exit(2);struct output *o=&outputs[count++];o->id=id;o->obj=wl_registry_bind(r,id,&kde_output_device_v2_interface,v<20?v:20);kde_output_device_v2_add_listener(o->obj,&output_listener,o);}
}
static void removed(void *d,struct wl_registry *r,uint32_t id){for(int i=0;i<count;i++)if(outputs[i].id==id)outputs[i].obj=NULL;}
static const struct wl_registry_listener registry_listener={global,removed};
static void timeout(int sig){_exit(124);}
static int apply(struct wl_display *d,struct kde_output_configuration_v2 *c) {
 result=0;kde_output_configuration_v2_add_listener(c,&config_listener,NULL);kde_output_configuration_v2_apply(c);
 while(!result)if(wl_display_dispatch(d)<0)return 0;
 kde_output_configuration_v2_destroy(c); if(wl_display_roundtrip(d)<0)return 0;return result==1;
}
int main(int argc,char **argv) {
 setbuf(stdout,NULL);signal(SIGALRM,timeout);alarm(15);
 struct wl_display *d=wl_display_connect(NULL);if(!d)return 2;
 struct wl_registry *r=wl_display_get_registry(d);wl_registry_add_listener(r,&registry_listener,NULL);
 for(int i=0;i<3;i++)if(wl_display_roundtrip(d)<0)return 2;
 struct output *target=NULL;
 for(int i=0;i<count;i++){struct output *o=&outputs[i];printf("output=%s caps=%x\n",o->name,o->caps);for(int j=0;j<o->count;j++){struct mode *m=&o->modes[j];if(!m->removed)printf(" mode=%dx%d@%d flags=%u current=%d\n",m->w,m->h,m->hz,m->flags,o->current==m->obj);}if(!strcmp(o->name,"Virtual-virtual-xdp-kde-io.github.PreceptorOfMagic.Umbra")){if(target)return 2;target=o;}}
 if(argc==1){wl_display_disconnect(d);return manager?0:3;}
 if(argc!=4 || !target || !manager)return 3;
 int w=atoi(argv[1]),h=atoi(argv[2]),hz=atoi(argv[3]);if(w<320||w>8192||h<320||h>8192||hz<1000||hz>240000)return 2;
 struct mode *chosen=NULL;
 for(int pass=0;pass<2;pass++) {
 for(int i=0;i<target->count;i++){struct mode *m=&target->modes[i];if(!m->removed&&m->w==w&&m->h==h&&m->hz==hz)chosen=m;}
 if(chosen)break;
 if(pass || version<18 || !(target->caps&0x2000))return 3;
 /* The diagnostic owns this ephemeral output. Do not replace physical mode lists. */
 struct kde_mode_list_v2 *list=kde_output_management_v2_create_mode_list(manager);
 for(int i=0;i<target->count;i++){struct mode *m=&target->modes[i];if(m->removed||!(m->flags&1))continue;kde_mode_list_v2_set_resolution(list,m->w,m->h);kde_mode_list_v2_set_refresh_rate(list,m->hz);kde_mode_list_v2_set_reduced_blanking(list,0);kde_mode_list_v2_add_mode(list);}
 kde_mode_list_v2_set_resolution(list,w,h);kde_mode_list_v2_set_refresh_rate(list,hz);kde_mode_list_v2_set_reduced_blanking(list,0);kde_mode_list_v2_add_mode(list);
 struct kde_output_configuration_v2 *c=kde_output_management_v2_create_configuration(manager);kde_output_configuration_v2_set_custom_modes(c,target->obj,list);
 if(!apply(d,c))return 4;
 kde_mode_list_v2_destroy(list);
 if(wl_display_roundtrip(d)<0)return 2;
 }
 struct kde_output_configuration_v2 *c=kde_output_management_v2_create_configuration(manager);kde_output_configuration_v2_mode(c,target->obj,chosen->obj);
 int right=0;
 for(int i=0;i<count;i++){struct output *o=&outputs[i];if(o==target||!o->obj)continue;for(int j=0;j<o->count;j++)if(o->modes[j].obj==o->current){int edge=o->x+(int)(o->modes[j].w/(o->scale?wl_fixed_to_double(o->scale):1.0));if(edge>right)right=edge;}}
 kde_output_configuration_v2_position(c,target->obj,right,0);
 if(!apply(d,c))return 4;
 printf("position=%d,%d\n",target->x,target->y);
 printf("applied=%dx%d@%d current_matches=%d\n",w,h,hz,target->current==chosen->obj);
 int ok=target->current==chosen->obj;wl_display_disconnect(d);return ok?0:5;
}
