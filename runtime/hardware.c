#include "runtime.h"
#include <dirent.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int matches(const char *s,const char *pattern){regex_t r;if(regcomp(&r,pattern,REG_EXTENDED|REG_NOSUB))return 0;int ok=!regexec(&r,s,0,NULL,0);regfree(&r);return ok;}
static int read_text(const char *path,char *out,size_t cap){FILE *f=fopen(path,"re");if(!f)return -1;if(!fgets(out,(int)cap,f)){fclose(f);return -1;}fclose(f);out[strcspn(out,"\r\n")]=0;return 0;}
static const char *basename_const(const char *p){const char *s=strrchr(p,'/');return s?s+1:p;}
json_t *ph_inventory(const char *root,ph_error *e){
    (void)e;json_t *devices=json_array();struct dirent **items=NULL;int count=scandir(root,&items,NULL,alphasort);if(count<0)return devices;
    for(int i=0;i<count;i++){
        const char *card=items[i]->d_name;if(!matches(card,"^card[0-9]+$"))continue;
        char path[PATH_MAX],dev[PATH_MAX],driverpath[PATH_MAX],driverreal[PATH_MAX],vendor[32],ident[32];
        snprintf(path,sizeof path,"%s/%s/device",root,card);if(!realpath(path,dev))continue;
        const char *pci=basename_const(dev);if(!matches(pci,"^[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-7]$"))continue;
        ph_path(driverpath,sizeof driverpath,dev,"driver");if(!realpath(driverpath,driverreal))continue;const char *driver=basename_const(driverreal);
        if(strcmp(driver,"nvidia")&&strcmp(driver,"amdgpu")&&strcmp(driver,"i915")&&strcmp(driver,"xe")&&strcmp(driver,"nouveau"))continue;
        ph_path(path,sizeof path,dev,"vendor");if(read_text(path,vendor,sizeof vendor))continue;ph_path(path,sizeof path,dev,"device");if(read_text(path,ident,sizeof ident))continue;
        const char *v=!strncmp(vendor,"0x",2)?vendor+2:vendor,*id=!strncmp(ident,"0x",2)?ident+2:ident;
        if(!matches(v,"^[0-9a-f]{4}$")||!matches(id,"^[0-9a-f]{4}$"))continue;
        json_t *outputs=json_array();size_t prefix=strlen(card);
        for(int j=0;j<count;j++){const char *connector=items[j]->d_name;if(strncmp(connector,card,prefix)||connector[prefix]!='-')continue;snprintf(path,sizeof path,"%s/%s/status",root,connector);char status[40];if(!read_text(path,status,sizeof status)&&!strcmp(status,"connected"))json_array_append_new(outputs,json_string(connector+prefix+1));}
        const char *brand=!strcmp(v,"10de")?"NVIDIA":!strcmp(v,"1002")?"AMD":!strcmp(v,"8086")?"Intel":v;char label[128];snprintf(label,sizeof label,"%.6s [%.4s] · %.12s",brand,id,pci);
        json_t *d=json_pack("{s:s,s:s,s:s,s:s,s:s,s:o}","pci",pci,"card",card,"vendor",v,"driver",driver,"label",label,"outputs",outputs);json_array_append_new(devices,d);
    }
    for(int i=0;i<count;i++)free(items[i]);
    free(items);return devices;
}
json_t *ph_resolve(json_t *s,json_t *devices,ph_error *e){
    const char *gpu=json_string_value(json_object_get(s,"gpu")),*output=json_string_value(json_object_get(s,"output"));if(!gpu||!output||!json_is_array(devices)){ph_fail(e,"Invalid GPU selection");return NULL;}
    json_t *best=NULL,*d;size_t i;
    json_array_foreach(devices,i,d){const char *pci=json_string_value(json_object_get(d,"pci"));json_t *outputs=json_object_get(d,"outputs");if(!pci||!json_is_array(outputs)||!json_array_size(outputs)|| (strcmp(gpu,"auto")&&strcmp(gpu,pci)))continue;
        if(strcmp(output,"auto")){int found=0;size_t j;json_t *o;json_array_foreach(outputs,j,o){const char *v=json_string_value(o);if(v&&!strcmp(v,output))found=1;}if(!found)continue;}
        if(!best||strcmp(pci,json_string_value(json_object_get(best,"pci")))<0)best=d;
    }
    if(!best)ph_fail(e,"No connected display on the selected GPU; VFIO devices are excluded");
    return best?json_incref(best):NULL;
}

/* A boot service starts before the display manager has necessarily brought up
 * a connector. Wait without consuming systemd's restart budget. SIGTERM can
 * cancel this just as it can stop a running stream. */
json_t *ph_wait_display(ph_context *c,json_t *settings,ph_error *e){
    int announced=0;
    while(!(c->cancel&&*c->cancel)){
        ph_error probe={0};
        json_t *devices=ph_inventory(c->drm,&probe);
        json_t *device=ph_resolve(settings,devices,&probe);
        json_decref(devices);
        if(device)return device;
        if(!announced){
            fprintf(stderr,"Waiting for the configured GPU and connected display\n");
            announced=1;
        }
        struct timespec delay={1,0};
        nanosleep(&delay,NULL);
    }
    ph_fail(e,"Display wait cancelled");
    return NULL;
}

/* A headless AMD/Intel card can encode: only capture needs a lit output. */
json_t *ph_encoder_select(json_t *s,json_t *devices,ph_error *e){
    const char *pci=json_string_value(json_object_get(s,"encoding_gpu"));
    if(!pci||!strcmp(pci,"auto")){ph_fail(e,"No separate encoding GPU selected");return NULL;}
    json_t *d;size_t i;
    json_array_foreach(devices,i,d){
        const char *id=json_string_value(json_object_get(d,"pci")),*vendor=json_string_value(json_object_get(d,"vendor"));
        if(!id||strcmp(id,pci))continue;
        if(!vendor||(strcmp(vendor,"1002")&&strcmp(vendor,"8086"))){ph_fail(e,"Separate encoding GPU must use AMD or Intel VA-API");return NULL;}
        return json_incref(d);
    }
    ph_fail(e,"Selected encoding GPU is unavailable; it may be bound to VFIO");return NULL;
}

/* The desktop user's PulseAudio/PipeWire outputs, as the monitor sources
 * lowlatd records from. Runs in the user's session, never as root. */
json_t *ph_audio_outputs(ph_context *c,ph_error *e){
    char *args[]={(char*)c->pactl,"-f","json","list","sinks",NULL};ph_process p;
    if(c->run(c,args,NULL,NULL,5,&p,e)||p.code){ph_process_free(&p);ph_fail(e,"Audio devices unavailable");return NULL;}
    json_t *sinks=json_loads(p.out,0,NULL),*outputs=json_array(),*sink;size_t i;ph_process_free(&p);
    json_array_foreach(sinks,i,sink){
        const char *id=json_string_value(json_object_get(sink,"monitor_source")),*label=json_string_value(json_object_get(sink,"description"));
        if(!id||!matches(id,"^[A-Za-z0-9_][A-Za-z0-9_.:@+-]{0,126}$"))continue;
        json_array_append_new(outputs,json_pack("{s:s,s:s}","id",id,"label",label&&*label?label:id));
    }
    json_decref(sinks);return outputs;
}
