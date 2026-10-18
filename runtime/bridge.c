#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *const codec[]={"h264","h265"},*const quality[]={"lowest-latency","balanced","highest"},*const encoder[]={"auto","nvenc","vaapi"},*const conversion[]={"auto","vulkan","opengl"};
static void utf8_copy(char *dest,size_t cap,const char *src){size_t n=strlen(src);if(n>=cap)n=cap-1;while(n&&((unsigned char)src[n]&0xc0)==0x80)n--;memcpy(dest,src,n);dest[n]=0;}
void ph_bridge_message(struct Bridge *b,const char *message){utf8_copy(b->status,sizeof b->status,message);}
static int index_of(const char *s,const char *const *values,int count){for(int i=0;i<count;i++)if(s&&!strcmp(s,values[i]))return i;return 0;}
static void load_form(struct Bridge *b,json_t *s){
    utf8_copy(b->name,sizeof b->name,json_string_value(json_object_get(s,"name")));utf8_copy(b->output,sizeof b->output,json_string_value(json_object_get(s,"output")));utf8_copy(b->gpu,sizeof b->gpu,json_string_value(json_object_get(s,"gpu")));
    b->fps=(int)json_integer_value(json_object_get(s,"fps"));b->bitrate=(int)json_integer_value(json_object_get(s,"bitrate"));
    b->codec=index_of(json_string_value(json_object_get(s,"codec")),codec,2);b->quality=index_of(json_string_value(json_object_get(s,"quality")),quality,3);b->encoder=index_of(json_string_value(json_object_get(s,"encoder")),encoder,3);b->conversion=index_of(json_string_value(json_object_get(s,"conversion")),conversion,3);
    b->autostart=json_is_true(json_object_get(s,"autostart"));b->full_fps=json_is_true(json_object_get(s,"full_fps"));b->mute_speakers=json_is_true(json_object_get(s,"mute_speakers"));b->clipboard=json_is_true(json_object_get(s,"clipboard"));
}
json_t *ph_bridge_form(ph_bridge *w,const struct Bridge *b,ph_error *e){
    if(b->codec<0||b->codec>=2||b->quality<0||b->quality>=3||b->encoder<0||b->encoder>=3||b->conversion<0||b->conversion>=3){ph_fail(e,"Invalid native form selection");return NULL;}
    if(!memchr(b->name,0,sizeof b->name)||!memchr(b->output,0,sizeof b->output)||!memchr(b->gpu,0,sizeof b->gpu)){ph_fail(e,"Invalid native form text");return NULL;}
    if((b->autostart!=0&&b->autostart!=1)||(b->full_fps!=0&&b->full_fps!=1)||(b->mute_speakers!=0&&b->mute_speakers!=1)||(b->clipboard!=0&&b->clipboard!=1)){ph_fail(e,"Invalid native boolean selection");return NULL;}
    json_t *s=w->settings?json_deep_copy(w->settings):ph_settings_default();
    json_t *name=json_string(b->name),*output=json_string(b->output),*gpu=json_string(b->gpu);if(!name||!output||!gpu){json_decref(name);json_decref(output);json_decref(gpu);json_decref(s);ph_fail(e,"Invalid UTF-8 in native form");return NULL;}
    json_object_set_new(s,"name",name);json_object_set_new(s,"output",output);json_object_set_new(s,"gpu",gpu);
    json_object_set_new(s,"fps",json_integer(b->fps));json_object_set_new(s,"bitrate",json_integer(b->bitrate));json_object_set_new(s,"codec",json_string(codec[b->codec]));json_object_set_new(s,"quality",json_string(quality[b->quality]));json_object_set_new(s,"encoder",json_string(encoder[b->encoder]));json_object_set_new(s,"conversion",json_string(conversion[b->conversion]));
    json_object_set_new(s,"autostart",json_boolean(b->autostart));json_object_set_new(s,"full_fps",json_boolean(b->full_fps));json_object_set_new(s,"mute_speakers",json_boolean(b->mute_speakers));json_object_set_new(s,"clipboard",json_boolean(b->clipboard));json_t *valid=ph_settings_validate(s,e);json_decref(s);return valid;
}
int ph_bridge_refresh(ph_bridge *w,int settings,int preserve_message,ph_error *e){
    json_t *result=ph_helper_call(w->ctx,"status",NULL,0,e);if(!result)return -1;struct Bridge *b=w->state;
    b->configured=json_is_true(json_object_get(result,"configured"));const char *service=json_string_value(json_object_get(json_object_get(result,"service"),"ActiveState"));b->running=service&&!strcmp(service,"active");
    if(!preserve_message){char text[256];snprintf(text,sizeof text,"Linux host: %s | %s",service?service:"unknown",b->configured?"Host account connected":"Connect the Host account first");ph_bridge_message(b,text);}
    if(settings){json_t *s=ph_settings_validate(json_object_get(result,"settings"),e);if(!s){json_decref(result);return -1;}json_decref(w->settings);w->settings=s;load_form(b,s);}
    json_t *devices=json_object_get(result,"devices"),*d;size_t i;unsigned count=0;
    json_array_foreach(devices,i,d){if(count>=8)break;if(!json_array_size(json_object_get(d,"outputs")))continue;const char *pci=json_string_value(json_object_get(d,"pci")),*label=json_string_value(json_object_get(d,"label"));if(!pci||!label)continue;utf8_copy(b->gpu_ids[count],sizeof b->gpu_ids[count],pci);utf8_copy(b->gpu_labels[count],sizeof b->gpu_labels[count],label);count++;}b->gpu_count=count;
    ph_bridge_extra_refresh(b,result);json_decref(result);return 0;
}
static int login_terminal(ph_error *e){
    const char *paths[]={"/usr/bin/xdg-terminal-exec","/usr/bin/konsole","/usr/bin/gnome-terminal","/usr/bin/xterm"};int selected=-1;for(int i=0;i<4;i++)if(!access(paths[i],X_OK)){selected=i;break;}if(selected<0)return ph_fail(e,"Run parsec-linux-host login in a terminal");
    pid_t pid=fork();if(pid<0)return ph_fail(e,"Cannot open login terminal");if(!pid){if(fork()!=0)_exit(0);setsid();int fd=open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);if(fd>2)close(fd);}
        if(selected==0)execl(paths[0],paths[0],"/usr/bin/parsec-linux-host","login",NULL);
        if(selected==1)execl(paths[1],paths[1],"--separate","-e","/usr/bin/parsec-linux-host","login",NULL);
        if(selected==2)execl(paths[2],paths[2],"--","/usr/bin/parsec-linux-host","login",NULL);
        if(selected==3)execl(paths[3],paths[3],"-e","/usr/bin/parsec-linux-host","login",NULL);_exit(127);
    }while(waitpid(pid,NULL,0)<0&&errno==EINTR){}return 0;
}
static int save_snapshot(ph_bridge *w,json_t *form,ph_error *e){
    if(w->read_only)return ph_fail(e,"Read-only UI test: no system settings or service state changed");
    json_t *reply=ph_helper_call(w->ctx,"save",form,1,e);if(!reply)return -1;
    json_decref(w->settings);w->settings=json_incref(form);int restart=json_is_true(json_object_get(reply,"restart_required"));json_decref(reply);
    ph_bridge_message(w->state,restart?"Settings saved. Stop and start hosting to apply them.":"Settings saved.");return 0;
}
/* The room as the page draws it: titles stay unique per guest (the page keys
 * its rows by them) and names are trimmed before formatting, so a cut never
 * splits a UTF-8 sequence. NULL clears it. */
void ph_bridge_publish_room(struct Bridge *b,const ph_room *room){
    unsigned n=room?room->count:0;if(n>BRIDGE_GUESTS_MAX)n=BRIDGE_GUESTS_MAX;
    for(unsigned i=0;i<n;i++){
        const ph_guest *g=&room->guests[i];char name[64];utf8_copy(name,sizeof name,*g->name?g->name:"Guest");
        snprintf(b->guest_titles[i],sizeof b->guest_titles[i],"%s (#%u)",name,(unsigned)g->id);
        snprintf(b->guest_details[i],sizeof b->guest_details[i],"%s · %s",g->owner?"Your account":"Invited guest",g->connected?"connected":"connecting");
        snprintf(b->guest_kick[i],sizeof b->guest_kick[i],"Kick##guest-%u",(unsigned)g->id);
        b->guest_ids[i]=g->id;
    }
    b->guest_count=n;
    if(room&&room->width&&room->height){
        const char *codec=!strcmp(room->codec,"h265")?"H.265":!strcmp(room->codec,"h264")?"H.264":room->codec;
        snprintf(b->stream,sizeof b->stream,"Streaming %ux%u · %u FPS · %u Mbps%s%s",room->width,room->height,room->fps,room->bitrate,*codec?" · ":"",codec);
    } else b->stream[0]=0;
}
static void *worker(void *arg){
    ph_bridge *w=arg;struct Bridge *b=w->state;json_t *seen=NULL,*attempted=NULL;double changed=ph_now(),next_poll=ph_now()+2;int sticky=0;
    while(!w->stopped){
        struct timespec delay={0,100000000};nanosleep(&delay,NULL);if(w->stopped)break;
        ph_error e={0};struct Bridge snapshot;memcpy(&snapshot,b,sizeof snapshot);json_t *form=ph_bridge_form(w,&snapshot,&e);
        int form_changed=form?(!seen||!json_equal(form,seen)):0;
        if(form_changed){json_decref(seen);seen=json_incref(form);changed=ph_now();sticky=0;}
        uint32_t seq=__atomic_load_n(&b->seq,__ATOMIC_ACQUIRE),ack=__atomic_load_n(&b->ack,__ATOMIC_RELAXED);unsigned action=b->action;
        if(seq!=ack){
            b->busy=1;
            if(action==4){if(ph_bridge_refresh(w,0,sticky,&e))sticky=1;}
            else if(w->read_only){ph_fail(&e,"Read-only UI test: no system settings or service state changed");sticky=1;}
            else if(action==1){json_decref(attempted);attempted=form?json_incref(form):NULL;if(!form||save_snapshot(w,form,&e))sticky=1;else sticky=0;}
            else if(action==5){if(login_terminal(&e))sticky=1;else{ph_bridge_message(b,"Complete Host login in the terminal. Status updates automatically.");sticky=0;}}
            else if(action==2||action==3){json_t *reply=ph_helper_call(w->ctx,action==2?"start":"stop",NULL,1,&e);if(!reply)sticky=1;else{json_decref(reply);if(ph_bridge_refresh(w,0,0,&e))sticky=1;else sticky=0;}}
            else if(action==10){if(ph_tray_kick(&w->tray,b->kick_guest,&e))sticky=1;else{ph_bridge_message(b,"Guest removed.");sticky=0;}}
            else if(action>=6){if(ph_bridge_extra_action(w,action,&e))sticky=1;else sticky=0;}
            else {ph_fail(&e,"Unknown native Host action");sticky=1;}
            if(*e.text)ph_bridge_message(b,e.text);__atomic_store_n(&b->ack,seq,__ATOMIC_RELEASE);b->busy=0;
        } else if(form&&!w->read_only&&!json_equal(form,w->settings)&&(!attempted||!json_equal(form,attempted))&&ph_now()-changed>=.75){
            b->busy=1;json_decref(attempted);attempted=json_incref(form);
            if(save_snapshot(w,form,&e)){ph_bridge_message(b,e.text);sticky=1;}else sticky=0;b->busy=0;
        }
        /* The live room, from lowlatd's own channel while hosting runs. */
        if(b->running&&w->tray.fd<0&&ph_now()>=w->tray_retry){ph_error ignored={0};if(ph_tray_open(&w->tray,PH_SESSION_SOCKET,&ignored))w->tray_retry=ph_now()+3;}
        if(w->tray.fd>=0){
            ph_room room;int got=b->running?ph_tray_read(&w->tray,&room):-1;
            if(got>0)ph_bridge_publish_room(b,&room);
            else if(got<0){ph_tray_close(&w->tray);ph_bridge_publish_room(b,NULL);w->tray_retry=ph_now()+3;}
        }
        if(ph_now()>=next_poll&&!w->stopped){
            /* Poll only status. Never reload editable fields while the user types. */
            int unsaved=!form||!json_equal(form,w->settings);if(ph_bridge_refresh(w,0,sticky||unsaved,&e)){if(!sticky){ph_bridge_message(b,e.text);sticky=1;}}next_poll=ph_now()+2;
        }
        json_decref(form);
    }
    json_decref(seen);json_decref(attempted);return NULL;
}
int ph_bridge_start(ph_bridge *w,ph_context *c,int read_only,ph_error *e){
    memset(w,0,sizeof *w);w->ctx=c;w->read_only=read_only;ph_tray_init(&w->tray);
    const char *runtime=getenv("XDG_RUNTIME_DIR");char fallback[64];if(!runtime){snprintf(fallback,sizeof fallback,"/run/user/%u",(unsigned)getuid());runtime=fallback;}
    struct stat st;if(lstat(runtime,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0077))return ph_fail(e,"A private desktop runtime directory is required");
    snprintf(w->directory,sizeof w->directory,"%s/parsec-native-host-XXXXXX",runtime);if(!mkdtemp(w->directory))return ph_fail(e,"Cannot create private Host bridge");
    ph_path(w->path,sizeof w->path,w->directory,"bridge");int fd=open(w->path,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);if(fd<0){rmdir(w->directory);return ph_fail(e,"Cannot create Host bridge");}
    if(ftruncate(fd,sizeof(struct Bridge))){close(fd);unlink(w->path);rmdir(w->directory);return ph_fail(e,"Cannot size Host bridge");}
    w->state=mmap(NULL,sizeof(struct Bridge),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);close(fd);if(w->state==MAP_FAILED){w->state=NULL;unlink(w->path);rmdir(w->directory);return ph_fail(e,"Cannot map Host bridge");}
    w->state->magic=0x50484f53;w->state->version=BRIDGE_VERSION;w->settings=ph_settings_default();load_form(w->state,w->settings);ph_bridge_message(w->state,"Loading Linux host status...");
    c->cancel=&w->stopped;if(ph_bridge_refresh(w,1,0,e))ph_bridge_message(w->state,e->text);
    if(pthread_create(&w->thread,NULL,worker,w)){ph_bridge_stop(w);return ph_fail(e,"Cannot start Host status worker");}w->started=1;return 0;
}
void ph_bridge_stop(ph_bridge *w){w->stopped=1;if(w->started)pthread_join(w->thread,NULL);w->started=0;ph_tray_close(&w->tray);if(w->ctx)w->ctx->cancel=NULL;if(w->state)munmap(w->state,sizeof(struct Bridge));w->state=NULL;json_decref(w->settings);w->settings=NULL;if(*w->path)unlink(w->path);if(*w->directory)rmdir(w->directory);}
__attribute__((weak)) void ph_bridge_extra_refresh(struct Bridge *b,json_t *result){(void)b;(void)result;}
__attribute__((weak)) int ph_bridge_extra_action(ph_bridge *w,unsigned action,ph_error *e){(void)w;(void)action;return ph_fail(e,"Unknown native Host action");}
