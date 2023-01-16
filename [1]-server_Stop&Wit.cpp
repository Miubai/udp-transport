#include <iostream>
#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

using namespace std;
int main(){
    FILE *fp;
    fp = fopen("/tmp/clientFile/test.txt","w");
    if (!fp){
        exit(-1);
    }
    char buff[1024] = "测试样例";
    int ret = fprintf(fp,"%s",buff);
    if (ret < 0){
        return -1;
    }
    return 1;

}