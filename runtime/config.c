#include "runtime.h"
#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char defaults[]="{\"name\":\"\",\"gpu\":\"auto\",\"encoding_gpu\":\"auto\",\"output\":\"auto\",\"fps\":60,\"bitrate\":10,\"codec\":\"h264\",\"encoder\":\"auto\",\"conversion\":\"auto\",\"quality\":\"lowest-latency\",\"full_fps\":false,\"mute_speakers\":true,\"clipboard\":true,\"autostart\":false,\"client_decoder\":\"hardware\",\"client_renderer\":\"opengl\"}";
json_t *ph_settings_default(void){return json_loads(defaults,0,NULL);}
static int match(const char *s,const char *pattern){regex_t r;if(regcomp(&r,pattern,REG_EXTENDED|REG_NOSUB))return 0;int ok=regexec(&r,s,0,NULL,0)==0;regfree(&r);return ok;}
static int choice(json_t *s,const char *key,const char *const *values){json_t *v=json_object_get(s,key);if(!json_is_string(v)||json_string_length(v)!=strlen(json_string_value(v)))return 0;for(int i=0;values[i];i++)if(!strcmp(json_string_value(v),values[i]))return 1;return 0;}
json_t *ph_settings_validate(json_t *data,ph_error *e){
    json_t *s=ph_settings_default();const char *key;json_t *value;
    if(!json_is_object(data)){ph_fail(e,"Settings must be an object");goto bad;}
    json_object_foreach(data,key,value){if(!json_object_get(s,key)){ph_fail(e,"Unknown setting");goto bad;}json_object_set(s,key,value);}
    json_t *name=json_object_get(s,"name");const char *n=json_string_value(name);size_t chars=0;
    if(!n){ph_fail(e,"Invalid host name");goto bad;}
    for(size_t i=0;i<json_string_length(name);i++){unsigned char c=(unsigned char)n[i];if(c<32){ph_fail(e,"Invalid host name");goto bad;}if((c&0xc0)!=0x80)chars++;}
    if(chars>64){ph_fail(e,"Host name is too long");goto bad;}
    const char *start=n;while(*start&&isspace((unsigned char)*start))start++;size_t len=strlen(start);while(len&&isspace((unsigned char)start[len-1]))len--;
    if(*start=='-'){ph_fail(e,"Host name cannot start with a dash");goto bad;}
    json_object_set_new(s,"name",json_stringn(start,len));
    const char *gpu=json_string_value(json_object_get(s,"gpu")),*output=json_string_value(json_object_get(s,"output"));
    if(!gpu||json_string_length(json_object_get(s,"gpu"))!=strlen(gpu)||!match(gpu,"^(auto|[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-7])$")){ph_fail(e,"Invalid GPU PCI address");goto bad;}
    const char *encoding_gpu=json_string_value(json_object_get(s,"encoding_gpu"));
    if(!encoding_gpu||json_string_length(json_object_get(s,"encoding_gpu"))!=strlen(encoding_gpu)||!match(encoding_gpu,"^(auto|[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-7])$")){ph_fail(e,"Invalid encoding GPU PCI address");goto bad;}
    if(!output||json_string_length(json_object_get(s,"output"))!=strlen(output)||!match(output,"^[A-Za-z0-9_.-]{1,40}$")){ph_fail(e,"Invalid display connector");goto bad;}
    const char *ints[]={"fps","bitrate"};int low[]={30,5},high[]={240,100};
    for(int i=0;i<2;i++){json_t *v=json_object_get(s,ints[i]);if(!json_is_integer(v)||json_integer_value(v)<low[i]||json_integer_value(v)>high[i]){ph_fail(e,"%s must be between %d and %d",ints[i],low[i],high[i]);goto bad;}}
    const char *keys[]={"codec","encoder","conversion","quality","client_decoder","client_renderer"};
    const char *const choices[][4]={{"h264","h265",NULL},{"auto","nvenc","vaapi",NULL},{"auto","vulkan","opengl",NULL},{"lowest-latency","balanced","highest",NULL},{"hardware","software",NULL},{"opengl","vulkan",NULL}};
    for(int i=0;i<6;i++)if(!choice(s,keys[i],choices[i])){ph_fail(e,"Unsupported %s",keys[i]);goto bad;}
    const char *bools[]={"full_fps","mute_speakers","clipboard","autostart"};
    for(int i=0;i<4;i++)if(!json_is_boolean(json_object_get(s,bools[i]))){ph_fail(e,"%s must be a boolean",bools[i]);goto bad;}
    if(!strcmp(json_string_value(json_object_get(s,"encoder")),"nvenc")&&!strcmp(json_string_value(json_object_get(s,"conversion")),"opengl")){ph_fail(e,"NVENC requires Automatic or Vulkan conversion");goto bad;}
    return s;
bad:json_decref(s);return NULL;
}
json_t *ph_settings_load(ph_context *c,ph_error *e){
    char path[PATH_MAX];ph_path(path,sizeof path,c->etc,"settings.json");
    json_t *data=ph_read_json(path,16384,0,e);if(!data){if(errno==ENOENT)return ph_settings_default();return NULL;}
    json_t *s=ph_settings_validate(data,e);json_decref(data);return s;
}
char **ph_host_argv(ph_context *c,json_t *settings,json_t *device,const char *secret,ph_error *e){
    json_t *s=ph_settings_validate(settings,e);if(!s)return NULL;
    if(secret&&*secret&&!match(secret,"^[0-9a-f]{8}$")){json_decref(s);ph_fail(e,"Invalid share secret");return NULL;}
    const char *card=json_string_value(json_object_get(device,"card"));const char *vendor=json_string_value(json_object_get(device,"vendor"));
    const char *encoding_gpu=json_string_value(json_object_get(s,"encoding_gpu"));
    int separate=strcmp(encoding_gpu,"auto")!=0;
    if(separate){
        json_t *devices=ph_inventory(c->drm,e),*chosen=ph_encoder_select(s,devices,e);
        if(!chosen){json_decref(devices);goto bad;}
        const char *capture_pci=json_string_value(json_object_get(device,"pci"));
        int cross=!capture_pci||strcmp(capture_pci,encoding_gpu);
        json_decref(chosen);json_decref(devices);
        if(cross&&!strcmp(json_string_value(json_object_get(s,"conversion")),"opengl")){ph_fail(e,"Cross-GPU encoding requires Automatic or Vulkan conversion");goto bad;}
        if(!strcmp(json_string_value(json_object_get(s,"encoder")),"nvenc")){ph_fail(e,"A separate AMD/Intel encoding GPU requires VA-API");goto bad;}
    }
    const char *encoder=json_string_value(json_object_get(s,"encoder")),*conversion=json_string_value(json_object_get(s,"conversion"));
    json_t *outputs=json_object_get(device,"outputs");
    if(!card||!match(card,"^card[0-9]+$")||!vendor||!json_is_array(outputs)||!json_array_size(outputs)){ph_fail(e,"Invalid DRM device");goto bad;}
    if(!separate&&((!strcmp(encoder,"nvenc")&&strcmp(vendor,"10de"))||(!strcmp(encoder,"vaapi")&&!strcmp(vendor,"10de")))){ph_fail(e,"Encoder does not match the selected GPU");goto bad;}
    char **args=calloc(40,sizeof *args);if(!args){ph_fail(e,"Out of memory");goto bad;}int i=0;
#define ADD(v) do{args[i]=strdup(v);if(!args[i++]){ph_argv_free(args);ph_fail(e,"Out of memory");goto bad;}}while(0)
    char file[PATH_MAX];ph_path(file,sizeof file,c->lib,"lowlatd");ADD(file);ADD("--name");
    const char *name=json_string_value(json_object_get(s,"name"));char hostname[256];gethostname(hostname,sizeof hostname-1);hostname[sizeof hostname-1]=0;ADD(*name?name:hostname);
    const char *output=json_string_value(json_object_get(s,"output"));if(!strcmp(output,"auto"))output=json_string_value(json_array_get(outputs,0));
    if(!output||!match(output,"^[A-Za-z0-9_.-]{1,40}$")){ph_argv_free(args);ph_fail(e,"Invalid display connector");goto bad;}
    char selected[80],number[20];snprintf(selected,sizeof selected,"%s:%s",card,output);ADD("--output");ADD(selected);
    ADD("--fps");snprintf(number,sizeof number,"%lld",(long long)json_integer_value(json_object_get(s,"fps")));ADD(number);
    ADD("--bitrate");snprintf(number,sizeof number,"%lld",(long long)json_integer_value(json_object_get(s,"bitrate")));ADD(number);
    ADD("--codec");ADD(json_string_value(json_object_get(s,"codec")));ADD("--quality");ADD(json_string_value(json_object_get(s,"quality")));
    ADD("--max-guests");ADD("1");
    /* A friend share secret relaxes owner-only admission to invite-only, which
     * the signaling service gates on the secret carried by the invite link. */
    if(secret&&*secret){ADD("--secret");ADD(secret);}else{ADD("--owner-only");}
    ADD("--clipboard");ADD(json_is_true(json_object_get(s,"clipboard"))?"both":"off");
    if(separate){ADD("--encoder");ADD("vaapi");}else if(strcmp(encoder,"auto")){ADD("--encoder");ADD(encoder);}if(strcmp(conversion,"auto")){ADD("--convert");ADD(conversion);}
    if(!json_is_true(json_object_get(s,"mute_speakers")))ADD("--no-host-mute");
    if(json_is_true(json_object_get(s,"full_fps")))ADD("--full-fps");
    json_decref(s);return args;
#undef ADD
bad:json_decref(s);return NULL;
}
