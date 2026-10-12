/* SPDX-License-Identifier: GPL-3.0-or-later
 * Manual feasibility probe, not part of the host. Uses the ordinary user-consent
 * ScreenCast portal from inside the installed Umbra Flatpak; no host escape.
 * argv: virtual|monitor seconds [defer]. SIGUSR1 (re)connects capture. Ctrl-C/TERM closes the session. */
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <stdio.h>
#include <unistd.h>

static GMainLoop *loop;
static GDBusConnection *bus;
static gchar *session;
static GVariant *response;
static guint request_number;
static gboolean stopped;
static gint frames;
static gint snapshot_requested;
static gboolean request_snapshot(gpointer unused) {
  g_atomic_int_set(&snapshot_requested,TRUE);
  return G_SOURCE_CONTINUE;
}


static gboolean stop_probe(gpointer unused) {
  g_atomic_int_set(&stopped,TRUE);
  if (loop) g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}
static void on_response(GDBusConnection *c, const gchar *s, const gchar *p,
                        const gchar *i, const gchar *n, GVariant *v, gpointer u) {
  response = g_variant_ref(v);
  g_main_loop_quit(loop);
}
static GVariant *request(const char *method, GVariantBuilder *opts, gboolean start) {
  gchar *token = g_strdup_printf("umbra_probe_%u", ++request_number);
  gchar *sender = g_strdup(g_dbus_connection_get_unique_name(bus) + 1);
  for (char *p=sender; *p; ++p) if (*p=='.') *p='_';
  gchar *path=g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s",sender,token);
  g_variant_builder_add(opts,"{sv}","handle_token",g_variant_new_string(token));
  guint sub=g_dbus_connection_signal_subscribe(bus,"org.freedesktop.portal.Desktop",
    "org.freedesktop.portal.Request","Response",path,NULL,G_DBUS_SIGNAL_FLAGS_NONE,on_response,NULL,NULL);
  GVariant *args = !session ? g_variant_new("(a{sv})",opts) :
    start ? g_variant_new("(osa{sv})",session,"",opts) : g_variant_new("(oa{sv})",session,opts);
  GError *err=NULL;
  GVariant *reply=g_dbus_connection_call_sync(bus,"org.freedesktop.portal.Desktop",
    "/org/freedesktop/portal/desktop","org.freedesktop.portal.ScreenCast",method,args,
    G_VARIANT_TYPE("(o)"),G_DBUS_CALL_FLAGS_NONE,10000,NULL,&err);
  response=NULL;
  guint timer=0;
  if (reply) {
    timer=g_timeout_add_seconds(120,stop_probe,NULL);
    if (!g_atomic_int_get(&stopped)) g_main_loop_run(loop);
    if (!g_atomic_int_get(&stopped)) g_source_remove(timer);
    g_variant_unref(reply);
  } else { fprintf(stderr,"%s: %s\n",method,err->message); g_clear_error(&err); }
  g_dbus_connection_signal_unsubscribe(bus,sub);
  g_free(token);g_free(sender);g_free(path);
  if (!response) return NULL;
  guint code;GVariant *dict;
  g_variant_get(response,"(u@a{sv})",&code,&dict);
  g_variant_unref(response); response=NULL;
  printf("%s response=%u\n",method,code);fflush(stdout);
  if (code) {g_variant_unref(dict);return NULL;}
  return dict;
}
static struct pw_thread_loop *pwloop;
static struct pw_context *pwcontext;
static struct pw_core *pwcore;
static struct pw_stream *pwstream;
static struct spa_hook pwlistener;
static struct spa_video_info_raw pwformat;
static int remote_fd=-1;
static guint remote_node;
static gboolean failed;
static gint corrupt_frames;
static void capture_cleanup(void) {
  if(pwloop)pw_thread_loop_stop(pwloop);
  if(pwstream){pw_stream_destroy(pwstream);pwstream=NULL;}
  if(pwcore){pw_core_disconnect(pwcore);pwcore=NULL;}
  if(pwcontext){pw_context_destroy(pwcontext);pwcontext=NULL;}
  if(pwloop){pw_thread_loop_destroy(pwloop);pwloop=NULL;}
}
static void on_state(void *data,enum pw_stream_state old,enum pw_stream_state state,const char *error) {
  printf("PipeWire state=%s %s\n",pw_stream_state_as_string(state),error?error:"");fflush(stdout);
  if(state==PW_STREAM_STATE_ERROR){g_atomic_int_set(&failed,TRUE);stop_probe(NULL);}
}
static void on_format(void *data,uint32_t id,const struct spa_pod *param) {
  if(id!=SPA_PARAM_Format || !param)return;
  if(spa_format_video_raw_parse(param,&pwformat)<0)return;
  const guint w=pwformat.size.width,h=pwformat.size.height;
  if(!w || !h || w>16384 || h>16384 || (uint64_t)w*h*4>256*1024*1024){g_atomic_int_set(&failed,TRUE);stop_probe(NULL);return;}
  printf("PipeWire format=%u size=%ux%u maxfps=%u/%u\n",pwformat.format,w,h,pwformat.max_framerate.num,pwformat.max_framerate.denom);fflush(stdout);
  uint8_t storage[1024];struct spa_pod_builder b=SPA_POD_BUILDER_INIT(storage,sizeof storage);
  const struct spa_pod *params[2];
  params[0]=spa_pod_builder_add_object(&b,SPA_TYPE_OBJECT_ParamBuffers,SPA_PARAM_Buffers,
    SPA_PARAM_BUFFERS_buffers,SPA_POD_CHOICE_RANGE_Int(3,2,4),
    SPA_PARAM_BUFFERS_blocks,SPA_POD_Int(1),
    SPA_PARAM_BUFFERS_size,SPA_POD_Int(w*h*4),
    SPA_PARAM_BUFFERS_stride,SPA_POD_Int(w*4),
    SPA_PARAM_BUFFERS_dataType,SPA_POD_CHOICE_FLAGS_Int(1<<SPA_DATA_MemFd));
  params[1]=spa_pod_builder_add_object(&b,SPA_TYPE_OBJECT_ParamMeta,SPA_PARAM_Meta,
    SPA_PARAM_META_type,SPA_POD_Id(SPA_META_Header),
    SPA_PARAM_META_size,SPA_POD_Int(sizeof(struct spa_meta_header)));
  if(pw_stream_update_params(pwstream,params,2)<0){g_atomic_int_set(&failed,TRUE);stop_probe(NULL);}
}
static void on_process(void *data) {
  struct pw_buffer *p=NULL,*next;
  while((next=pw_stream_dequeue_buffer(pwstream))){if(p)pw_stream_queue_buffer(pwstream,p);p=next;}
  if(!p)return;
  struct spa_buffer *b=p->buffer;
  if(!b->n_datas)goto out;
  struct spa_data *d=&b->datas[0];
  struct spa_meta_header *header=spa_buffer_find_meta_data(b,SPA_META_Header,sizeof *header);
  if(!d->data || !d->chunk || (d->chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) ||
      (header && (header->flags & SPA_META_HEADER_FLAG_CORRUPTED))){g_atomic_int_inc(&corrupt_frames);goto out;}
  const guint w=pwformat.size.width,h=pwformat.size.height;
  const int stride=d->chunk->stride;
  if(!w || !h || (pwformat.format!=SPA_VIDEO_FORMAT_BGRA && pwformat.format!=SPA_VIDEO_FORMAT_BGRx) ||
      stride<(int)w*4 || d->chunk->offset>d->maxsize ||
      (uint64_t)(h-1)*stride+w*4>d->maxsize-d->chunk->offset){g_atomic_int_set(&failed,TRUE);stop_probe(NULL);goto out;}
  gint n=g_atomic_int_add(&frames,1)+1;
  static guint last_width,last_height;
  gboolean requested=g_atomic_int_compare_and_exchange(&snapshot_requested,TRUE,FALSE);
  if(requested || n==1 || n%300==0 || w!=last_width || h!=last_height){
    last_width=w;last_height=h;
    printf("frame=%d size=%ux%u seq=%" G_GUINT64_FORMAT " pts=%" G_GINT64_FORMAT " stride=%d offset=%u\n",n,w,h,header?header->seq:0,header?header->pts:0,stride,d->chunk->offset);fflush(stdout);
    FILE *out=fopen("portal-frame.ppm","wb");
    if(out){
      fprintf(out,"P6\n%u %u\n255\n",w,h);
      guint8 *row=g_malloc(w*3);
      for(guint y=0;y<h;y++){
        const guint8 *src=(guint8*)d->data+d->chunk->offset+(size_t)y*stride;
        for(guint x=0;x<w;x++){row[x*3]=src[x*4+2];row[x*3+1]=src[x*4+1];row[x*3+2]=src[x*4];}
        fwrite(row,1,w*3,out);
      }
      g_free(row);fclose(out);
    }
  }
out:
  pw_stream_queue_buffer(pwstream,p);
}
static const struct pw_stream_events pwevents={PW_VERSION_STREAM_EVENTS,
  .state_changed=on_state,.param_changed=on_format,.process=on_process};
static gboolean reconnect(gpointer unused) {
  capture_cleanup();
  pwloop=pw_thread_loop_new("umbra-portal-probe",NULL);
  if(!pwloop)goto error;
  pwcontext=pw_context_new(pw_thread_loop_get_loop(pwloop),NULL,0);
  if(!pwcontext)goto error;
  int copy=dup(remote_fd);if(copy<0)goto error;
  pwcore=pw_context_connect_fd(pwcontext,copy,NULL,0);
  if(!pwcore)goto error;
  pwstream=pw_stream_new(pwcore,"Umbra portal capture probe",pw_properties_new(
    PW_KEY_MEDIA_TYPE,"Video",PW_KEY_MEDIA_CATEGORY,"Capture",PW_KEY_MEDIA_ROLE,"Screen",NULL));
  if(!pwstream)goto error;
  pw_stream_add_listener(pwstream,&pwlistener,&pwevents,NULL);
  uint8_t storage[1024];struct spa_pod_builder b=SPA_POD_BUILDER_INIT(storage,sizeof storage);
  const struct spa_pod *params[]={spa_pod_builder_add_object(&b,SPA_TYPE_OBJECT_Format,SPA_PARAM_EnumFormat,
    SPA_FORMAT_mediaType,SPA_POD_Id(SPA_MEDIA_TYPE_video),
    SPA_FORMAT_mediaSubtype,SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
    SPA_FORMAT_VIDEO_format,SPA_POD_CHOICE_ENUM_Id(3,SPA_VIDEO_FORMAT_BGRx,SPA_VIDEO_FORMAT_BGRx,SPA_VIDEO_FORMAT_BGRA))};
  if(pw_stream_connect(pwstream,PW_DIRECTION_INPUT,remote_node,PW_STREAM_FLAG_AUTOCONNECT|PW_STREAM_FLAG_MAP_BUFFERS,params,1)<0)goto error;
  if(pw_thread_loop_start(pwloop)<0)goto error;
  puts("capture connected (direct PipeWire, MemFd)");fflush(stdout);return G_SOURCE_CONTINUE;
error:
  g_atomic_int_set(&failed,TRUE);stop_probe(NULL);return G_SOURCE_CONTINUE;
}
static gboolean stats(gpointer unused) {
  printf("frames=%d monotonic_us=%" G_GINT64_FORMAT "\n",g_atomic_int_get(&frames),g_get_monotonic_time());fflush(stdout);
  return G_SOURCE_CONTINUE;
}
int main(int argc,char **argv) {
  int rc=1,fd=-1;
  if((argc!=3 && argc!=4) || (g_strcmp0(argv[1],"virtual") && g_strcmp0(argv[1],"monitor"))) return 2;
  int duration=atoi(argv[2]);if(duration<1 || duration>300)return 2;
  guint type=g_strcmp0(argv[1],"virtual")==0?4:1;
  pw_init(NULL,NULL);loop=g_main_loop_new(NULL,FALSE);
  g_unix_signal_add(SIGINT,stop_probe,NULL);g_unix_signal_add(SIGTERM,stop_probe,NULL);
  GError *err=NULL;bus=g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,&err);
  if(!bus){fprintf(stderr,"bus: %s\n",err->message);return 1;}
  GVariantBuilder opts;g_variant_builder_init(&opts,G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&opts,"{sv}","session_handle_token",g_variant_new_string("umbra_probe"));
  GVariant *r=request("CreateSession",&opts,FALSE);if(!r)goto done;
  g_variant_lookup(r,"session_handle","s",&session);g_variant_unref(r);if(!session)goto done;
  g_variant_builder_init(&opts,G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&opts,"{sv}","types",g_variant_new_uint32(type));
  g_variant_builder_add(&opts,"{sv}","multiple",g_variant_new_boolean(FALSE));
  g_variant_builder_add(&opts,"{sv}","cursor_mode",g_variant_new_uint32(2));
  g_variant_builder_add(&opts,"{sv}","persist_mode",g_variant_new_uint32(0));
  r=request("SelectSources",&opts,FALSE);if(!r)goto done;g_variant_unref(r);
  puts("Waiting for desktop consent (120 second timeout)");fflush(stdout);
  g_variant_builder_init(&opts,G_VARIANT_TYPE_VARDICT);
  r=request("Start",&opts,TRUE);if(!r)goto done;
  GVariant *streams=g_variant_lookup_value(r,"streams",G_VARIANT_TYPE("a(ua{sv})"));
  if(!streams || g_variant_n_children(streams)!=1){g_variant_unref(r);if(streams)g_variant_unref(streams);goto done;}
  guint node;GVariant *props;g_variant_get_child(streams,0,"(u@a{sv})",&node,&props);
  char *proptext=g_variant_print(props,TRUE);printf("node=%u %s\n",node,proptext);fflush(stdout);g_free(proptext);
  g_variant_unref(props);g_variant_unref(streams);g_variant_unref(r);
  g_variant_builder_init(&opts,G_VARIANT_TYPE_VARDICT);
  GUnixFDList *fds=NULL;
  r=g_dbus_connection_call_with_unix_fd_list_sync(bus,"org.freedesktop.portal.Desktop",
    "/org/freedesktop/portal/desktop","org.freedesktop.portal.ScreenCast","OpenPipeWireRemote",
    g_variant_new("(oa{sv})",session,&opts),G_VARIANT_TYPE("(h)"),G_DBUS_CALL_FLAGS_NONE,10000,NULL,&fds,NULL,&err);
  if(!r){fprintf(stderr,"OpenPipeWireRemote: %s\n",err->message);g_clear_error(&err);goto done;}
  gint index;g_variant_get(r,"(h)",&index);fd=g_unix_fd_list_get(fds,index,&err);g_variant_unref(r);g_object_unref(fds);
  if(fd<0)goto done;
  remote_fd=fd;remote_node=node;
  g_unix_signal_add(SIGUSR1,reconnect,NULL);
  g_unix_signal_add(SIGUSR2,request_snapshot,NULL);
  if (argc==3) reconnect(NULL);
  else { puts("Capture deferred; send SIGUSR1 after configuring the output");fflush(stdout); }
  g_timeout_add_seconds(duration,stop_probe,NULL);g_timeout_add_seconds(5,stats,NULL);
  if(!g_atomic_int_get(&stopped))g_main_loop_run(loop);
  rc=!g_atomic_int_get(&failed) && g_atomic_int_get(&frames)>0?0:1;
done:
  capture_cleanup();
  if(fd>=0)close(fd);
  if(session){GVariant *closed=g_dbus_connection_call_sync(bus,"org.freedesktop.portal.Desktop",session,"org.freedesktop.portal.Session","Close",NULL,NULL,G_DBUS_CALL_FLAGS_NONE,5000,NULL,NULL);if(closed)g_variant_unref(closed);g_free(session);}
  g_object_unref(bus);g_main_loop_unref(loop);
  printf("closed frames=%d corrupt=%d exit=%d\n",g_atomic_int_get(&frames),g_atomic_int_get(&corrupt_frames),rc);return rc;
}
