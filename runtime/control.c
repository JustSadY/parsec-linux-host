#include "runtime.h"
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int secure_directory(const char *path,ph_error *e){
    if(mkdir(path,0755)&&errno!=EEXIST)return ph_fail(e,"Cannot create host configuration directory");
    struct stat st;if(lstat(path,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0022))return ph_fail(e,"Unsafe host configuration directory");return 0;
}
static int system_action(ph_context *c,const char *action,ph_error *e){
    char *args[]={(char*)c->systemctl,(char*)action,PH_UNIT,NULL};ph_process p;
    if(c->run(c,args,NULL,NULL,25,&p,e)){ph_process_free(&p);return -1;}int result=p.code;ph_process_free(&p);return result?ph_fail(e,"Service operation failed; open Host logs"):0;
}
int ph_claim_owner(ph_context *c,ph_error *e){
    if(c->euid!=0)return ph_fail(e,"Administrator authentication required");
    if(c->caller_uid<1000)return ph_fail(e,"Run from the desktop account through pkexec or sudo");
    struct passwd *user=getpwuid(c->caller_uid);if(!user)return ph_fail(e,"Unknown desktop account");
    if(secure_directory(c->etc,e))return -1;
    char path[PATH_MAX];ph_path(path,sizeof path,c->etc,"owner.json");
    json_t *current=ph_read_json(path,1024,0,e);
    if(current){json_t *uid=json_object_get(current,"uid");int same=json_is_integer(uid)&&json_integer_value(uid)==c->caller_uid;json_decref(current);if(!same)return ph_fail(e,"This host belongs to another local account");}
    else if(errno!=ENOENT)return -1;
    json_t *o=json_pack("{s:i,s:i}","uid",(int)c->caller_uid,"gid",(int)user->pw_gid);int r=ph_atomic_json(path,o,0644,e);json_decref(o);if(r)return -1;
    if(secure_directory(c->dropin,e))return -1;
    ph_path(path,sizeof path,c->dropin,"owner.conf");char wanted[100];snprintf(wanted,sizeof wanted,"[Service]\nGroup=%u\n",(unsigned)user->pw_gid);
    char actual[100]={0};int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);ssize_t n=fd<0?-1:read(fd,actual,sizeof actual-1);if(fd>=0)close(fd);
    if(n<0||strcmp(actual,wanted)){
        char tmp[PATH_MAX];snprintf(tmp,sizeof tmp,"%s.new-XXXXXX",path);fd=mkostemp(tmp,O_CLOEXEC);if(fd<0)return ph_fail(e,"Cannot create owner service configuration");
        int failed=fchmod(fd,0644)||write(fd,wanted,strlen(wanted))!=(ssize_t)strlen(wanted)||fsync(fd);if(close(fd))failed=1;
        if(!failed&&rename(tmp,path))failed=1;if(failed){unlink(tmp);return ph_fail(e,"Cannot configure host owner");}
        char *args[]={(char*)c->systemctl,"daemon-reload",NULL};ph_process p;if(c->run(c,args,NULL,NULL,25,&p,e)){ph_process_free(&p);return -1;}int code=p.code;ph_process_free(&p);if(code)return ph_fail(e,"systemd reload failed");
    }
    return 0;
}
json_t *ph_status(ph_context *c,ph_error *e){
    char *args[]={(char*)c->systemctl,"show",PH_UNIT,"--property=ActiveState,SubState,Result",NULL};ph_process p;
    if(c->run(c,args,NULL,NULL,25,&p,e)){ph_process_free(&p);return NULL;}
    json_t *service=json_object();char *save=NULL,*line=strtok_r(p.out,"\n",&save);
    while(line){char *eq=strchr(line,'=');if(eq){*eq=0;if(!strcmp(line,"ActiveState")||!strcmp(line,"SubState")||!strcmp(line,"Result"))json_object_set_new(service,line,json_string(eq+1));}line=strtok_r(NULL,"\n",&save);}ph_process_free(&p);
    json_t *settings=ph_settings_load(c,e);if(!settings){json_decref(service);return NULL;}
    char path[PATH_MAX];ph_path(path,sizeof path,c->etc,"account-info.json");ph_error ignored={0};json_t *info=ph_read_json(path,4096,0,&ignored);const char *name=info?json_string_value(json_object_get(info,"name")):NULL;
    ph_path(path,sizeof path,c->lib,"lowlatd");int installed=access(path,X_OK)==0;
    ph_path(path,sizeof path,c->etc,"account.json");struct stat st;int configured=lstat(path,&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0077);
    json_t *result=json_pack("{s:b,s:b,s:o,s:s,s:o,s:o}","installed",installed,"configured",configured,"service",service,"account",name?name:"","settings",settings,"devices",ph_inventory(c->drm,e));json_decref(info);
    json_t *extra=ph_share_status(c,&ignored);if(extra){json_object_update(result,extra);json_decref(extra);}return result;
}
struct response {char *text;size_t size;};
static size_t response_write(char *ptr,size_t size,size_t nmemb,void *userdata){struct response *r=userdata;size_t n=size*nmemb;if(n>131072-r->size)return 0;char *p=realloc(r->text,r->size+n+1);if(!p)return 0;r->text=p;memcpy(p+r->size,ptr,n);r->size+=n;p[r->size]=0;return n;}
static int request_progress(void *opaque,curl_off_t down_total,curl_off_t down_now,curl_off_t up_total,curl_off_t up_now){
    (void)down_total;(void)down_now;(void)up_total;(void)up_now;ph_context *c=opaque;return c->cancel&&*c->cancel;
}
json_t *ph_request(ph_context *c,const char *method,const char *endpoint,json_t *body,const char *token,ph_error *e){
    if(strcmp(endpoint,"/v2/auth")&&strcmp(endpoint,"/me")&&strcmp(endpoint,"/v2/hosts?mode=desktop")){ph_fail(e,"Unsupported account endpoint");return NULL;}
    CURL *curl=curl_easy_init();if(!curl){ph_fail(e,"Cannot initialize account connection");return NULL;}
    char url[256];snprintf(url,sizeof url,"https://kessel-api.parsec.app%s",endpoint);struct curl_slist *headers=NULL;headers=curl_slist_append(headers,"Content-Type: application/json");headers=curl_slist_append(headers,"Accept: application/json");
    char *auth=NULL,*payload=body?json_dumps(body,JSON_COMPACT):NULL;if(token){if(asprintf(&auth,"Authorization: Bearer %s",token)<0)auth=NULL;if(auth)headers=curl_slist_append(headers,auth);}
    struct response response={0};curl_easy_setopt(curl,CURLOPT_URL,url);curl_easy_setopt(curl,CURLOPT_CUSTOMREQUEST,method);curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,!strcmp(endpoint,"/v2/hosts?mode=desktop")?3L:10L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,!strcmp(endpoint,"/v2/hosts?mode=desktop")?6L:25L);curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,response_write);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,request_progress);curl_easy_setopt(curl,CURLOPT_XFERINFODATA,c);
    if(payload)curl_easy_setopt(curl,CURLOPT_POSTFIELDS,payload);
    CURLcode code=curl_easy_perform(curl);long http=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&http);json_t *answer=NULL;
    if(code!=CURLE_OK)ph_fail(e,"Parsec account connection failed");else if(http<200||http>=300)ph_fail(e,"Parsec login: HTTP %ld",http);else{answer=json_loads(response.text?response.text:"",JSON_REJECT_DUPLICATES,NULL);if(!json_is_object(answer)){json_decref(answer);answer=NULL;ph_fail(e,"Invalid account response");}}
    if(payload){explicit_bzero(payload,strlen(payload));free(payload);}if(auth){explicit_bzero(auth,strlen(auth));free(auth);}if(response.text){explicit_bzero(response.text,response.size);free(response.text);}curl_slist_free_all(headers);curl_easy_cleanup(curl);return answer;
}
json_t *ph_dispatch(ph_context *c,const char *action,json_t *data,ph_error *e){
    if(!strcmp(action,"status"))return ph_status(c,e);
    if(!strcmp(action,"hardware"))return json_pack("{s:o}","devices",ph_inventory(c->drm,e));
    if(c->euid!=0){ph_fail(e,"Administrator authentication required");return NULL;}
    int known=!strcmp(action,"save")||!strcmp(action,"login")||!strcmp(action,"start")||!strcmp(action,"stop")||!strcmp(action,"logs");
    if(!known)return ph_share_dispatch(c,action,data,e);
    /* A second desktop user must not stop, reconfigure or inspect this host. */
    if(!strcmp(action,"save")){
        json_t *s=ph_settings_validate(data,e);if(!s)return NULL;json_t *devices=ph_inventory(c->drm,e),*device=ph_resolve(s,devices,e);char **args=device?ph_host_argv(c,s,device,NULL,e):NULL;json_decref(device);json_decref(devices);if(!args){json_decref(s);return NULL;}ph_argv_free(args);
        if(ph_claim_owner(c,e)){json_decref(s);return NULL;}char path[PATH_MAX];ph_path(path,sizeof path,c->etc,"settings.json");if(ph_atomic_json(path,s,0644,e)||system_action(c,json_is_true(json_object_get(s,"autostart"))?"enable":"disable",e)){json_decref(s);return NULL;}json_decref(s);
        json_t *status=ph_status(c,e);if(!status)return NULL;const char *state=json_string_value(json_object_get(json_object_get(status,"service"),"ActiveState"));int active=state&&!strcmp(state,"active");json_decref(status);return json_pack("{s:b,s:b}","saved",1,"restart_required",active);
    }
    if(!strcmp(action,"login")){
        if(!json_is_object(data)){ph_fail(e,"Invalid login fields");return NULL;}const char *key;json_t *v;json_object_foreach(data,key,v){if((strcmp(key,"email")&&strcmp(key,"password")&&strcmp(key,"tfa"))||!json_is_string(v)||json_string_length(v)!=strlen(json_string_value(v))){ph_fail(e,"Invalid login fields");return NULL;}}
        const char *email=json_string_value(json_object_get(data,"email")),*password=json_string_value(json_object_get(data,"password")),*tfa=json_string_value(json_object_get(data,"tfa"));if(!email||!*email||!password||!*password){ph_fail(e,"Email and password required");return NULL;}
        json_t *status=ph_status(c,e);if(!status)return NULL;const char *state=json_string_value(json_object_get(json_object_get(status,"service"),"ActiveState"));int active=state&&!strcmp(state,"active");json_decref(status);if(active){ph_fail(e,"Stop hosting before changing account");return NULL;}if(ph_claim_owner(c,e))return NULL;
        json_t *body=json_pack("{s:s,s:s,s:[s,s,s]}","email",email,"password",password,"session_scopes","ws.host","ws.client","api.writer");if(tfa&&*tfa)json_object_set_new(body,"tfa",json_string(tfa));json_t *auth=c->request(c,"POST","/v2/auth",body,NULL,e);json_decref(body);if(!auth)return NULL;
        json_t *session=json_object_get(auth,"session_id");const char *token=json_string_value(session);if(!token||!strlen(token)||strlen(token)>4096||json_string_length(session)!=strlen(token)){json_decref(auth);ph_fail(e,"Invalid host session response");return NULL;}
        char path[PATH_MAX];ph_path(path,sizeof path,c->etc,"account.json");json_t *account=json_pack("{s:s}","session_id",token);int result=ph_atomic_json(path,account,0600,e);json_decref(account);if(result){json_decref(auth);return NULL;}
        ph_error ignored={0};json_t *me=c->request(c,"GET","/me",NULL,token,&ignored);const char *name=me?json_string_value(json_object_get(json_object_get(me,"data"),"name")):NULL;char safe[81];snprintf(safe,sizeof safe,"%.80s",name&&*name?name:"Parsec account");while(strlen(safe)&&!json_string(safe))safe[strlen(safe)-1]=0;
        ph_path(path,sizeof path,c->etc,"account-info.json");json_t *info=json_pack("{s:s}","name",safe);result=ph_atomic_json(path,info,0644,e);json_decref(info);json_decref(me);json_decref(auth);return result?NULL:json_pack("{s:b}","connected",1);
    }
    if(ph_claim_owner(c,e))return NULL;
    if(!strcmp(action,"logs")){
        char *args[]={(char*)c->journalctl,"-u",PH_UNIT,"-n","40","--no-pager","-o","cat",NULL};ph_process p;if(c->run(c,args,NULL,NULL,25,&p,e)){ph_process_free(&p);return NULL;}size_t len=strlen(p.out);json_t *result=json_pack("{s:s}","logs",p.out+(len>16000?len-16000:0));ph_process_free(&p);return result;
    }
    if(!strcmp(action,"start")){
        char path[PATH_MAX];ph_path(path,sizeof path,c->etc,"account.json");json_t *account=ph_read_json(path,8192,1,e);if(!account){ph_fail(e,"Connect your Host account first");return NULL;}json_decref(account);
        json_t *s=ph_settings_load(c,e);if(!s)return NULL;json_t *devices=ph_inventory(c->drm,e),*device=ph_resolve(s,devices,e);char **args=device?ph_host_argv(c,s,device,NULL,e):NULL;json_decref(device);json_decref(devices);json_decref(s);if(!args)return NULL;ph_argv_free(args);
    }
    if(system_action(c,action,e))return NULL;return json_pack("{s:b}","completed",1);
}
json_t *ph_helper_call(ph_context *c,const char *action,json_t *data,int privileged,ph_error *e){
    char *args[5];int i=0;if(privileged)args[i++]=(char*)c->pkexec;args[i++]=(char*)c->helper;args[i++]=(char*)action;args[i]=NULL;
    char *payload=data?json_dumps(data,JSON_COMPACT):strdup("{}");if(!payload){ph_fail(e,"Out of memory");return NULL;}ph_process p;int failed=c->run(c,args,payload,NULL,90,&p,e);explicit_bzero(payload,strlen(payload));free(payload);if(failed){ph_process_free(&p);return NULL;}
    json_t *reply=json_loads(p.out,JSON_REJECT_DUPLICATES,NULL);int code=p.code;ph_process_free(&p);if(!json_is_object(reply)){json_decref(reply);ph_fail(e,"Administrator authentication was cancelled or the controller failed");return NULL;}
    if(code||!json_is_true(json_object_get(reply,"ok"))){const char *message=json_string_value(json_object_get(reply,"error"));ph_fail(e,"%s",message?message:"Host controller failed");json_decref(reply);return NULL;}json_t *result=json_object_get(reply,"result");json_incref(result);json_decref(reply);return result;
}
__attribute__((weak)) json_t *ph_share_status(ph_context *c,ph_error *e){(void)c;(void)e;return NULL;}
__attribute__((weak)) json_t *ph_share_dispatch(ph_context *c,const char *a,json_t *d,ph_error *e){(void)c;(void)a;(void)d;ph_fail(e,"Unknown action");return NULL;}
int ph_hostctl_main(int argc,char **argv){
    ph_context c;ph_context_init(&c);ph_error e={0};json_t *data=NULL,*result=NULL;
    char raw[16385];size_t n=0;if(argc!=2)ph_fail(&e,"Exactly one action required");else{n=fread(raw,1,sizeof raw,stdin);if(n==sizeof raw)ph_fail(&e,"Request too large");else{raw[n]=0;data=n?json_loadb(raw,n,JSON_REJECT_DUPLICATES,NULL):json_object();if(!data)ph_fail(&e,"Invalid request JSON");else result=ph_dispatch(&c,argv[1],data,&e);}}
    explicit_bzero(raw,sizeof raw);json_decref(data);json_t *reply=result?json_pack("{s:b,s:o}","ok",1,"result",result):json_pack("{s:b,s:s}","ok",0,"error",*e.text?e.text:"Host controller failed");json_dumpf(reply,stdout,JSON_COMPACT);fputc('\n',stdout);json_decref(reply);return result?0:1;
}
