#include "runtime.h"
#include <string.h>
int main(int argc,char **argv){const char *name=strrchr(argv[0],'/');name=name?name+1:argv[0];if(!strcmp(name,"hostctl"))return ph_hostctl_main(argc,argv);if(!strcmp(name,"service-runner"))return ph_runner_main();return ph_launcher_main(argc,argv);}
