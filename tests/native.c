/* Execute our adapters against vendor ABI doubles. No vendor or system state. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../native/host-ui.c"
static int card_local, card_calls, clipboard_calls, click, strip_calls;
static void *expected_callbacks;
unsigned char *ui_context;
void tab(float a,const char*b,const char*c,uint64_t d){(void)a;(void)b;(void)c;(void)d;}
void header(void*a,int b,const char*c){(void)a;(void)b;(void)c;}
void text(const char*a,const char*b){(void)a;(void)b;}
void label(float a,const char*b,const char*c,int d){(void)a;(void)b;(void)c;(void)d;}
float field(float a,const char*b){(void)b;return a;}
void same_line(float a,float b){(void)a;(void)b;}
void begin_group(void){} void end_group(void){} void pop_id(void){}
void pop_vars(int a){(void)a;} void pop_colors(int a){(void)a;} void unindent(float a){(void)a;}
float cursor_x(void){return 0;} float cursor_y(void){return 0;} void cursor_set(const float*a){(void)a;}
unsigned char combo(const char*a,int*b,const char*const*c,int d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
unsigned char input(const char*a,const char*b,char*c,size_t d,int e,void*f,void*g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;return 0;}
unsigned char button(float a,const char*b,int c){(void)a;(void)b;(void)c;return 0;}
char *vendor_getenv(const char*a){(void)a;return NULL;} float vendor_roundf(float a){return a;}
void vendor_card(float a,float b,unsigned char*c,void*d,void*e,void*f,void*g,int h){(void)a;(void)b;(void)d;(void)e;(void)g;(void)h;card_calls++;card_local=((unsigned char*)f)[0x2f5]&&!*c;}
void vendor_host_list(unsigned char*a,void*b,uint32_t*c,void*d){(void)a;(void)b;(void)c;(void)d;}
unsigned char vendor_action_button(float a,const char*b,int c,unsigned char d){(void)a;(void)b;(void)c;return d?0:click;}
const char *vendor_config_string(const char*a,int b){(void)a;(void)b;return "vendor-secret";}
unsigned char vendor_icon_button(const char*a,uint32_t b,uint32_t c,int d){(void)a;(void)b;(void)c;(void)d;return click;}
void vendor_clipboard_set(const char*a){assert(!strcmp(a,"fixture-link"));clipboard_calls++;}
void vendor_guest_strip(float a,void*b,void*c,void*d){(void)a;(void)b;(void)c;strip_calls++;assert(d!=expected_callbacks);assert(((uintptr_t*)d)[1]==(uintptr_t)bridge);}
int vendor_guest_provider(void*a,int b,void**c){(void)a;(void)b;(void)c;return 99;}
void *vendor_host_command(void*a,const void*b,uint32_t c,uint32_t*d){(void)a;(void)b;(void)c;(void)d;assert(!"unexpected SDK command");return NULL;}
void *vendor_calloc(size_t a,size_t b){return calloc(a,b);}
static void idle(struct Bridge*b){b->ack=b->seq;b->busy=0;}
int main(void){
 struct Bridge b={.configured=1,.running=1};strcpy(b.encoding_gpu,"auto");bridge=&b;attempted=1;
 strcpy(b.host_peer_id,"exact-local-peer");unsigned char record[0x2f8]={0},context[1]={1};
 strcpy((char*)record+0x294,"another-peer");linux_card(1,1,context,0,0,record,0,0);assert(!card_local&&context[0]==1);
 strcpy((char*)record+0x294,"exact-local-peer");linux_card(1,1,context,0,0,record,0,0);assert(card_local&&context[0]==1&&!record[0x2f5]);
 unsigned char *list=calloc(1,0x309e0);uint32_t uid=12;
 linux_host_list(list,0,&uid,0);assert(*(uint32_t*)(list+0x2f818)==1&&!strcmp((char*)list+0x18+0x294,b.host_peer_id));
 linux_host_list(list,0,&uid,0);assert(*(uint32_t*)(list+0x2f818)==1);
 b.running=0;linux_host_list(list,0,&uid,0);assert(!*(uint32_t*)(list+0x2f818));b.running=1;free(list);
 b.running=0;linux_card(1,1,context,0,0,record,0,0);assert(!card_local);b.running=1;
 local_card=1;click=1;assert(!linux_card_action(1,"Share",1,0));assert(b.action==6&&b.seq==1);
 assert(!linux_card_action(1,"Share",1,0)&&b.seq==1);idle(&b);strcpy(b.share_link,"fixture-link");b.share_enabled=1;native_tick(&b);native_tick(&b);assert(clipboard_calls==1);
 strcpy(b.share_secret,"01234567");assert(!strcmp(linux_card_secret("ws_secret",0),"01234567"));assert(!strcmp(linux_card_secret("other",0),"vendor-secret"));
 assert(!linux_card_revoke("x",0,0,0)&&b.action==7);idle(&b);local_card=0;
 b.guest_count=1;b.guest_ids[0]=77;b.guest_user_ids[0]=12;strcpy(b.guest_names[0],"Connected");b.guest_permissions[0][1]=1;
 b.pending_count=1;b.pending_ids[0]=UINT64_MAX-7;b.pending_user_ids[0]=42;strcpy(b.pending_names[0],"Pending");
 linux_guests=1;void *records=NULL;assert(linux_guest_provider(NULL,5,&records)==2);unsigned char *r=records;assert(*(uint32_t*)(r+0x11c)==4&&r[1]==1&&*(uint32_t*)(r+0x124)==77);r+=0x1b8;assert(*(uint32_t*)(r+0x11c)==1&&!strcmp((char*)r+0x158,"18446744073709551608"));free(records);
 unsigned char command[32]={0x11,0,0,0,1};strcpy((char*)command+5,"18446744073709551608");uint32_t size=0;assert(linux_guest_command(0,command,26,&size)&&size==12&&b.action==11&&b.decision_id==UINT64_MAX-7);idle(&b);
 command[4]=0;assert(linux_guest_command(0,command,26,&size)&&b.action==12);idle(&b);
 strcpy((char*)command+5,"18446744073709551616");assert(!linux_guest_command(0,command,26,&size));
 memset(command,0,sizeof command);put32(command,0xf);put32(command+4,77);command[8]=1;command[10]=1;assert(linux_guest_command(0,command,12,&size)&&b.action==13&&b.permissions_guest==77&&b.permissions_value[0]&&!b.permissions_value[1]&&b.permissions_value[2]);idle(&b);
 put32(command,0x10);assert(linux_guest_command(0,command,12,&size)&&b.action==10&&b.kick_guest==77);idle(&b);
 linux_guests=0;uintptr_t callbacks[12]={0};unsigned char guest_context[3]={0};expected_callbacks=callbacks;linux_guest_strip(1,guest_context,0,callbacks);assert(strip_calls==1&&!callbacks[1]);
 printf("Native adapters: local identity, native actions, clipboard, guest records, full-width decisions and live permissions passed.\n");
}
