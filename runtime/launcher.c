#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static volatile sig_atomic_t client_pid;
static void stop_client(int signal){if(client_pid>0)kill(client_pid,signal);}
static int verify_module(const char *module,const char *expected){
    if(!expected||strlen(expected)!=64)return 0;
    int fd=open(module,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return 0;
    EVP_MD_CTX *ctx=EVP_MD_CTX_new();
    if(!ctx){close(fd);return 0;}
    int ok=EVP_DigestInit_ex(ctx,EVP_sha256(),NULL);
    char block[16384];ssize_t n=0;
    while(ok&&(n=read(fd,block,sizeof block))>0)
        ok=EVP_DigestUpdate(ctx,block,(size_t)n);
    if(n<0)ok=0;
    unsigned char digest[32]={0};unsigned length=0;
    if(ok)ok=EVP_DigestFinal_ex(ctx,digest,&length);
    EVP_MD_CTX_free(ctx);close(fd);
    char hex[65];
    for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
    return ok&&length==32&&!strcmp(hex,expected);
}
static int launch(ph_context *c,int count,char **extra,int temporary,int read_only,ph_error *e){
    if(geteuid()==0)return ph_fail(e,"Open Parsec as your desktop user, without sudo");
    const char *display=getenv("DISPLAY");if(!display||!*display)return ph_fail(e,"An X11 or XWayland desktop session is required");
    struct passwd *user=getpwuid(getuid());if(!user)return ph_fail(e,"Unknown desktop account");const char *home=user->pw_dir;char vendor[PATH_MAX],module_dir[PATH_MAX],module[PATH_MAX],metadata_path[PATH_MAX],profile[PATH_MAX],profile_module[PATH_MAX],profile_meta[PATH_MAX];
    /* A development override, honoured only for this unprivileged user's own
     * run (root is refused above); the module is still consistency-checked. */
    const char *dev_vendor=getenv("PARSEC_HOST_VENDOR_DIR");
    if(dev_vendor&&*dev_vendor)snprintf(module_dir,sizeof module_dir,"%s",dev_vendor);
    else ph_path(module_dir,sizeof module_dir,c->lib,"vendor");
    ph_path(vendor,sizeof vendor,module_dir,"parsecd");ph_path(module,sizeof module,module_dir,"parsecd-150-105c.so");ph_path(metadata_path,sizeof metadata_path,module_dir,"appdata.json");
    json_t *metadata=ph_read_json(metadata_path,4096,0,e);if(!metadata)return -1;const char *so=json_string_value(json_object_get(metadata,"so_name")),*entry=json_string_value(json_object_get(metadata,"entry_symbol"));int valid=so&&!strcmp(so,"parsecd-150-105c.so")&&entry&&!strcmp(entry,"wx_main")&&verify_module(module,json_string_value(json_object_get(metadata,"hash")));json_decref(metadata);if(!valid)return ph_fail(e,"Installed native module is inconsistent; reinstall the matching package");
    ph_path(profile,sizeof profile,home,".parsec");if(mkdir(profile,0700)&&errno!=EEXIST)return ph_fail(e,"Cannot create Parsec profile");struct stat st;if(lstat(profile,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0022))return ph_fail(e,"Unsafe Parsec profile directory");
    const char *runtime=getenv("XDG_RUNTIME_DIR");char fallback[64];if(!runtime){snprintf(fallback,sizeof fallback,"/run/user/%u",(unsigned)getuid());runtime=fallback;}if(lstat(runtime,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0077))return ph_fail(e,"A private desktop runtime directory is required");
    char lock_path[PATH_MAX];ph_path(lock_path,sizeof lock_path,runtime,"parsec-linux-host.lock");int lock=open(lock_path,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0)return ph_fail(e,"Cannot lock Parsec client");if(flock(lock,LOCK_EX|LOCK_NB)){close(lock);return ph_fail(e,"Parsec Linux Host is already open");}
    ph_bridge bridge;if(ph_bridge_start(&bridge,c,read_only,e)){close(lock);return -1;}
    json_t *settings=ph_settings_load(c,e);if(!settings){ph_bridge_stop(&bridge);close(lock);return -1;}
    char config[128];snprintf(config,sizeof config,"app_host=0:decoder_software=%d:client_renderer=%d",!strcmp(json_string_value(json_object_get(settings,"client_decoder")),"software"),!strcmp(json_string_value(json_object_get(settings,"client_renderer")),"vulkan")?5:0);json_decref(settings);
    ph_path(profile_module,sizeof profile_module,profile,"parsecd-150-105c.so");ph_path(profile_meta,sizeof profile_meta,profile,"appdata.json");
    size_t cap=100+(size_t)count;char **args=calloc(cap,sizeof *args);if(!args){ph_bridge_stop(&bridge);close(lock);return ph_fail(e,"Out of memory");}int i=0;
#define A(v) args[i++]=(char*)(v)
    A("/usr/bin/bwrap");A("--ro-bind");A("/");A("/");A("--unshare-pid");A("--unshare-ipc");A("--proc");A("/proc");A("--dev-bind");A("/dev");A("/dev");A("--tmpfs");A("/tmp");A("--ro-bind");A("/tmp/.X11-unix");A("/tmp/.X11-unix");A("--dir");A("/tmp/runtime");
    if(temporary){A("--overlay-src");A(profile);A("--tmp-overlay");A(profile);}else{A("--bind");A(profile);A(profile);}
    A("--ro-bind");A(module);A(profile_module);A("--ro-bind");A(metadata_path);A(profile_meta);A("--bind");A(bridge.path);A(bridge.path);A("--clearenv");
    A("--setenv");A("HOME");A(home);A("--setenv");A("PATH");A("/usr/bin:/bin");A("--setenv");A("LANG");A(getenv("LANG")?getenv("LANG"):"C.UTF-8");A("--setenv");A("DISPLAY");A(display);A("--setenv");A("XDG_SESSION_TYPE");A("x11");A("--setenv");A("XDG_RUNTIME_DIR");A("/tmp/runtime");A("--setenv");A("PARSEC_NATIVE_HOST_BRIDGE");A(bridge.path);
    const char *keys[]={"XAUTHORITY","__EGL_VENDOR_LIBRARY_FILENAMES","__GLX_VENDOR_LIBRARY_NAME","__NV_PRIME_RENDER_OFFLOAD","LIBGL_ALWAYS_SOFTWARE"};for(int k=0;k<5;k++)if(getenv(keys[k])){A("--setenv");A(keys[k]);A(getenv(keys[k]));}
    A("--die-with-parent");A("--");A(vendor);A(config);for(int k=0;k<count;k++)A(extra[k]);args[i]=NULL;
#undef A
    pid_t pid=fork();if(!pid){execv(args[0],args);_exit(127);}free(args);int result=-1;if(pid<0)ph_fail(e,"Cannot launch Parsec");else{client_pid=pid;struct sigaction sa={0},oldint,oldterm;sa.sa_handler=stop_client;sigemptyset(&sa.sa_mask);sigaction(SIGINT,&sa,&oldint);sigaction(SIGTERM,&sa,&oldterm);int status=0;while(waitpid(pid,&status,0)<0&&errno==EINTR){}client_pid=0;sigaction(SIGINT,&oldint,NULL);sigaction(SIGTERM,&oldterm,NULL);result=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);}
    ph_bridge_stop(&bridge);close(lock);return result;
}
static char *prompt(const char *label,int secret){
    fputs(label,stdout);fflush(stdout);struct termios old,changed;int changed_term=secret&&!tcgetattr(0,&old);if(changed_term){changed=old;changed.c_lflag&=~ECHO;tcsetattr(0,TCSAFLUSH,&changed);}char *line=NULL;size_t cap=0;ssize_t n=getline(&line,&cap,stdin);if(changed_term){tcsetattr(0,TCSAFLUSH,&old);fputc('\n',stdout);}if(n<0){free(line);return NULL;}line[strcspn(line,"\r\n")]=0;return line;
}
int ph_launcher_main(int argc,char **argv){
    ph_context c;ph_context_init(&c);ph_error e={0};const char *action="open";int temporary=0,read_only=0,index=1;
    if(index<argc&&argv[index][0]!='-')action=argv[index++];
    while(index<argc){if(!strcmp(argv[index],"--temporary-profile")){temporary=1;index++;}else if(!strcmp(argv[index],"--read-only")){read_only=1;index++;}else break;}
    if((index<argc&&!strcmp(argv[index],"--help"))||!strcmp(action,"help")){puts("Usage: parsec-linux-host [open|login|status|start|stop] [--temporary-profile] [--read-only]");return 0;}
    if(!strcmp(action,"open")){int result=launch(&c,argc-index,argv+index,temporary,read_only,&e);if(result<0)fprintf(stderr,"%s\n",e.text);return result<0?1:result;}
    if(index<argc|| (strcmp(action,"login")&&strcmp(action,"status")&&strcmp(action,"start")&&strcmp(action,"stop"))){fputs("Invalid command; run parsec-linux-host --help\n",stderr);return 1;}
    json_t *data=json_object();char *email=NULL,*password=NULL,*tfa=NULL;if(!strcmp(action,"login")){
        if(!isatty(0)){ph_fail(&e,"Run login in an interactive terminal");goto fail;}email=prompt("Parsec email: ",0);password=prompt("Parsec password: ",1);tfa=prompt("Two-factor code (Enter if unused): ",0);if(!email||!password||!tfa){ph_fail(&e,"Login cancelled");goto fail;}
        json_object_set_new(data,"email",json_string(email));json_object_set_new(data,"password",json_string(password));if(*tfa)json_object_set_new(data,"tfa",json_string(tfa));
    }
    json_t *reply=ph_helper_call(&c,action,data,strcmp(action,"status")!=0,&e);if(password)explicit_bzero(password,strlen(password));free(email);free(password);free(tfa);json_decref(data);if(!reply){fprintf(stderr,"%s\n",e.text);return 1;}if(!strcmp(action,"status")){json_dumpf(reply,stdout,JSON_INDENT(2));fputc('\n',stdout);}else puts("Done.");json_decref(reply);return 0;
fail:if(password)explicit_bzero(password,strlen(password));free(email);free(password);free(tfa);json_decref(data);fprintf(stderr,"%s\n",e.text);return 1;
}
