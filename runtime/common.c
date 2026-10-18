#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int ph_fail(ph_error *e, const char *fmt, ...) {
    if(e) { va_list ap; va_start(ap,fmt); vsnprintf(e->text,sizeof e->text,fmt,ap); va_end(ap); }
    return -1;
}
void ph_path(char *out,size_t size,const char *dir,const char *file) { snprintf(out,size,"%s/%s",dir,file); }
double ph_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static uid_t caller(void) {
    const char *s=getenv("PKEXEC_UID"); if(!s||!*s)s=getenv("SUDO_UID");
    if(!s||!*s)return 0;
    char *end; errno=0; unsigned long n=strtoul(s,&end,10);
    return errno||*end||n>(unsigned)(uid_t)-1?0:(uid_t)n;
}
void ph_context_init(ph_context *c) {
    memset(c,0,sizeof *c); c->etc=PH_ETC;c->lib=PH_LIB;c->drm="/sys/class/drm";
    c->systemctl="/usr/bin/systemctl";c->journalctl="/usr/bin/journalctl";
    c->helper=PH_LIB "/hostctl";c->pkexec="/usr/bin/pkexec";
    c->dropin="/etc/systemd/system/parsec-host.service.d";
    c->peer_cache="/run/parsec-host/host-peer.json";
    c->rules="/etc/polkit-1/rules.d";
    c->euid=geteuid();c->caller_uid=c->euid?c->euid:caller();c->run=ph_run;c->request=ph_request;
}
json_t *ph_read_json(const char *path,size_t limit,int private_file,ph_error *e) {
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0){ph_fail(e,errno==ENOENT?"File not found":"Cannot read configuration");return NULL;}
    struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(size_t)st.st_size>limit||
       (private_file&&(st.st_uid!=0||(st.st_mode&0077)))) {
        close(fd);ph_fail(e,"Unsafe configuration file");return NULL;
    }
    json_error_t je;json_t *v=json_loadfd(fd,JSON_REJECT_DUPLICATES,&je);close(fd);
    if(!v)ph_fail(e,"Invalid configuration JSON");
    return v;
}
int ph_atomic_json(const char *path,json_t *v,mode_t mode,ph_error *e) {
    char tmp[PATH_MAX];
    if(snprintf(tmp,sizeof tmp,"%s.new-XXXXXX",path)>=(int)sizeof tmp)
        return ph_fail(e,"Host configuration path is too long");
    int fd=mkostemp(tmp,O_CLOEXEC);if(fd<0)return ph_fail(e,"Cannot create configuration");
    int r=fchmod(fd,mode)||json_dumpfd(v,fd,JSON_ENSURE_ASCII|JSON_COMPACT)||write(fd,"\n",1)!=1||fsync(fd);
    if(close(fd))r=1;
    if(!r&&rename(tmp,path))r=1;
    if(r){unlink(tmp);return ph_fail(e,"Cannot save configuration");}return 0;
}
void ph_process_free(ph_process *p){free(p->out);p->out=NULL;}
int ph_run(ph_context *c,char *const argv[],const char *input,char *const env[],int timeout,ph_process *res,ph_error *e) {
    int in[2],out[2];memset(res,0,sizeof *res);
    if(pipe2(in,O_CLOEXEC))return ph_fail(e,"Cannot open process input");
    if(pipe2(out,O_CLOEXEC)){close(in[0]);close(in[1]);return ph_fail(e,"Cannot open process output");}
    pid_t pid=fork();
    if(pid<0){close(in[0]);close(in[1]);close(out[0]);close(out[1]);return ph_fail(e,"Cannot start process");}
    if(!pid){
        setpgid(0,0);dup2(in[0],0);dup2(out[1],1);int null=open("/dev/null",O_WRONLY);if(null>=0)dup2(null,2);
        close(in[0]);close(in[1]);close(out[0]);close(out[1]);if(null>2)close(null);
        if(env)execve(argv[0],argv,env);else execv(argv[0],argv);_exit(127);
    }
    setpgid(pid,pid);close(in[0]);close(out[1]);
    fcntl(in[1],F_SETFL,O_NONBLOCK);fcntl(out[0],F_SETFL,O_NONBLOCK);
    size_t sent=0,need=input?strlen(input):0,used=0,cap=4096;char *buffer=malloc(cap);
    if(!buffer){kill(-pid,SIGKILL);close(in[1]);close(out[0]);waitpid(pid,NULL,0);return ph_fail(e,"Out of memory");}
    int input_open=1,output_open=1,status=0,exited=0,failed=0;double deadline=ph_now()+timeout;
    while(!exited||output_open){
        if(ph_now()>=deadline||(c->cancel&&*c->cancel)){
            kill(-pid,SIGTERM);struct timespec short_wait={0,100000000};nanosleep(&short_wait,NULL);kill(-pid,SIGKILL);
            ph_fail(e,c->cancel&&*c->cancel?"Host request cancelled":"Host controller timed out");failed=1;break;
        }
        if(input_open&&sent==need){close(in[1]);input_open=0;}
        struct pollfd fds[2]={{out[0],output_open?POLLIN:0,0},{in[1],input_open?POLLOUT:0,0}};
        poll(fds,2,50);
        if(input_open&&(fds[1].revents&(POLLOUT|POLLHUP|POLLERR))){
            ssize_t n=write(in[1],input+sent,need-sent);if(n>0)sent+=(size_t)n;
            else if(n<0&&errno!=EAGAIN&&errno!=EINTR){close(in[1]);input_open=0;}
        }
        if(output_open&&(fds[0].revents&(POLLIN|POLLHUP|POLLERR))){
            char block[4096];ssize_t n;
            while((n=read(out[0],block,sizeof block))>0){
                if(used+(size_t)n>1024*1024){ph_fail(e,"Host response too large");failed=1;break;}
                if(used+(size_t)n+1>cap){cap=(used+(size_t)n+1)*2;char *p=realloc(buffer,cap);if(!p){ph_fail(e,"Out of memory");failed=1;break;}buffer=p;}
                memcpy(buffer+used,block,(size_t)n);used+=(size_t)n;
            }
            if(!n||(n<0&&errno!=EAGAIN&&errno!=EINTR)){close(out[0]);output_open=0;}
        }
        if(failed){kill(-pid,SIGKILL);break;}
        if(!exited&&waitpid(pid,&status,WNOHANG)==pid)exited=1;
    }
    if(input_open)close(in[1]);
    if(output_open)close(out[0]);
    if(!exited)while(waitpid(pid,&status,0)<0&&errno==EINTR){}
    buffer[used]=0;res->out=buffer;res->code=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
    return failed?-1:0;
}
void ph_argv_free(char **a){if(a){for(size_t i=0;a[i];i++)free(a[i]);free(a);}}
