/*
 * Created by miubai on 2021/12/3.
 * 改文件流读取方式
 **/

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

#define BUFSIZE 1024

using namespace std;
//读文件发文件
char filename[50];
//创建该文件
FILE *fp;

struct PACKAGE{
    unsigned char tag;//TCP码元字段，16进制排列：ack,syn,fin从高到低，
    unsigned int seq;//序号，参考tcp序号
    unsigned int ack;//确认序号
    unsigned short win;//窗口大小
    unsigned short bufLen;//长度，占2byte = 16位
    unsigned short checksum;//校验和
    char data[2048];//数据区
};
/*
 * 制作ack帧的 ack seq tag
 * */
int make_ack(struct PACKAGE *aPackage,unsigned short seq, unsigned short ack);
//计算校验和
unsigned short makesum(int count, char* buf);
/*
 * 请求建立连接，发送连接建立请求包
 * */
int reqConnect(int argc,char *argv[]);

/*
 * 判断发送的报文，并进行处理
 * 传入参数sfd，文件描述符
 * */
int JudgePack(int *sfd, unsigned short *seq, unsigned short *ack, unsigned short *win);
/*
 * 发送ack
 * */
int sendAck(int sfd,struct sockaddr_in from, struct PACKAGE recvPackage, unsigned short *seq,unsigned short *win);
/*
 * 将获取到的文件写入到磁盘中
 *
 * */
int writeFile(char *buf, int size, FILE *fp);
int main(int argc, char *argv[]){
    if (argc != 3){
        perror("error args! please ip and ports!\n");
        return -1;
    }
    //创建udp socket连接
    int sfd = reqConnect(argc,argv);
    if (-1 == sfd){
        perror("reqConnect");
        return -1;
    }
    //等待服务器发送第一帧文件数据到本地，并进行判断
    unsigned short seq = 3;
    unsigned short ack = 2;
    unsigned short win = BUFSIZE;
    while (true){
        int judg_ret = JudgePack(&sfd,&seq,&ack,&win);
        if (-1 == judg_ret){
            return -1;
        }

    }
}
/*
 * 请求建立连接，发送连接建立请求包
 * */
int reqConnect(int argc,char *argv[]){

    int sfd;
    sfd = socket(AF_INET, SOCK_DGRAM,0);//udp
    if (sfd < 0){
        perror("reqConnect socket error");
        return -1;
    }

    //服务器地址
    sockaddr_in ser_addr;
    ser_addr.sin_family = AF_INET;//ipv4
    ser_addr.sin_addr.s_addr = inet_addr(argv[1]);//转换为32位二进制
    ser_addr.sin_port = htons(atoi(argv[2]));//转换为网络字节序
    //cout << "addr = " << inet_ntoa(ser_addr.sin_addr);

    /*向服务器发送建立连接请求，搭建第一个syn包*/
    struct PACKAGE recConnSyn;//syn包
    memset(&recConnSyn,0,sizeof(recConnSyn));
    recConnSyn.tag = 0x04;//syn ack fin,所以是0x04

    //发送syn
    int send_ret = sendto(sfd,&recConnSyn,sizeof(recConnSyn),0,(struct sockaddr *)&ser_addr,sizeof(ser_addr));
    if (-1 == send_ret){
        perror("reqConnect sendto error");
        return -1;
    }
    cout << "连接请求发送成功\n";

    //syn请求建立连接报文发送成功，等待接收报文
    struct PACKAGE recConnAck;
    memset(&recConnAck,0,sizeof(recConnAck));
    struct sockaddr_in from_addr;
    socklen_t len = sizeof(from_addr);
    int recv_ret = recvfrom(sfd,&recConnAck,sizeof(recConnAck),0,(struct sockaddr*)&from_addr,&len);
    if (-1 == recv_ret){
        perror("reConnect recvfrom error");
        return -1;
    }
    if (recv_ret > 0){
        cout << "tag = " << recConnAck.tag << "\n";
        if (0x06 == recConnAck.tag){
            //成功收到第二次握手帧
            cout << "客户机：我已经收到第二次握手帧！现在发送最后一次握手帧\n";
            //发送第三次握手帧给服务器
            struct PACKAGE finConn;
            memset(&finConn,0,sizeof(finConn));
            make_ack(&finConn,recConnAck.seq,recConnAck.seq+1);//上一帧的seq，下一帧的ack
            int fin_ret = sendto(sfd,&finConn,sizeof(finConn),0,(struct sockaddr *)&ser_addr, sizeof(ser_addr));
            if (-1 == fin_ret){
                perror("reqConnect fin_ret sendto error");
                return -1;
            }
            return sfd;//发送成功则表示成功建立连接，并将文件描述符返回
        }
    }

}

/*
 * 判断发送的报文，并进行处理
 * */
int JudgePack(int *sfd, unsigned short *seq, unsigned short *ack, unsigned short *win){
    //先创建一个缓冲区
    char buff[BUFSIZE];
    //帧原封不动接过来
    struct PACKAGE package;
    memset(&package,0,sizeof(package));
    struct sockaddr_in cli_addr;
    memset(&cli_addr,0,sizeof(cli_addr));
    socklen_t len = sizeof(cli_addr);
    //接收来自服务器的数据
    int recv_ret = recvfrom(*sfd,&package,sizeof(package),0,(struct sockaddr*)&cli_addr,&len);
    if (-1 == recv_ret){
        perror("JudgePackage error");
        return -1;
    }
    cout << "收到一帧来自服务器的数据\n";
    switch (package.tag) {
        case 0x06:{
            //syn ack都有
            cout << "seq = " << package.seq << "\nack = " << package.ack << "\n";
            /*int seq = package.seq;
            int ack = package.ack;*/
            //判断是不是第一帧传输的文件名称，如果是，则按照这种方式创建文件
            if ((0 == package.seq) && (2 == package.ack) ){
                cout << "服务器传过来一个名为: " << package.data << "的文件\n";

                strcat(filename,"recvFile1_");
                memcpy(&filename[strlen(filename)],package.data,strlen(package.data));
                fp = fopen(filename,"a");
                if (!fp){
                    cout << filename << "文件创建成功！\n";
                }
                *seq = 3;
                *win = BUFSIZE;
                //接收成功，发送ack回去
                int send_ret =  sendAck(*sfd,cli_addr,package,seq,win);
                if (-1 == send_ret){
                    return -1;
                }
                cout << "发送收到文件名称确认帧，成功\n";
                //cout << "seq = " << *seq << "\n";
                return 1;

            } else{
                //如果是其他的帧，查看序号是否正确
                cout << "seq = " << *seq << "\n";
                if ((*seq) = package.ack){
                    //ack序号与本地seq序号一致，说明没有乱序
                    cout << "没有乱序\n";
                    if (package.checksum != makesum(package.bufLen,package.data)){
                        cout << "校验码错误，请求重发！\n";
                        //发送重发包
                        //send
                        return 1;//表明需要重发
                    }
                    cout << "校验无误\n";

                    /*
                     * 查看缓存大小，如果足够才能接收
                     * */
                    if (*win >= strlen(package.data)){
                        //窗口足够，接收
                        memset(buff,0,sizeof(buff));
                        memcpy(buff,package.data, strlen(package.data));
                        *win = sizeof(buff) - strlen(package.data);//自己的窗口变小了
                        //接收文件，写入
                        if (!fp){
                            perror("fopen error");
                            exit(-1);
                        }
                        cout << "将数据写入文件\n";
                        int w_ret = writeFile(buff, strlen(buff),fp);
                        if (-1 == w_ret){
                            fclose(fp);
                            exit(0);
                        }
                        cout << "成功写入一帧数据\n";
                        cout << "数据内容：\n" << package.data;
                        *win = *win + strlen(buff);//写完后，窗口变大了
                        memset(buff,0,sizeof(buff));
                        return 1;
                    }

                }

            }
        }

    }
}
/*
 * 将获取到的文件写入到磁盘中
 *
 * */
int writeFile(char *buf, int size, FILE *fp){
    for (int i = 0; i < size; ++i) {
        if (EOF == fputc(buf[i],fp)){
            cout << "文件写完了，或者写入失败！\n";
            return -1;
        }
    }
    return 1;
}
/*
 * 发送ack
 * */
int sendAck(int sfd,struct sockaddr_in from, struct PACKAGE recvPackage, unsigned short *seq,unsigned short *win){
    struct PACKAGE aPackage;
    memset(&aPackage,0,sizeof(aPackage));
    *win = BUFSIZE - recvPackage.bufLen;
    aPackage.win = *win;
    aPackage.ack = aPackage.seq;
    aPackage.seq = *seq++;
    aPackage.tag = 0x06;//syn ack fin 110B = 6
    socklen_t len = sizeof(from);
    int send_ret = sendto(sfd,&aPackage,sizeof(aPackage),0,(struct sockaddr *)&from,len);
    if (-1 == send_ret){
        perror("sendAck error");
        return -1;
    }
    return 1;
}
/*
 * 制作ack帧的 ack seq tag
 * */
int make_ack(struct PACKAGE *aPackage,unsigned short seq, unsigned short ack){
    /*aPackage->tag = 0x06;//syn ack fin 所以是0x06，在这里做，对方识别不出来，需要在外面做码元域*/
    aPackage->seq = seq;//期望接收的下一帧
    aPackage->ack = ack;//确认前一帧
}
//计算校验和
unsigned short makesum(int count, char* buf){
    unsigned long sum;
    for (sum = 0; count > 0; count--)
    {
        sum += *buf++;
        sum = (sum >> 16) + (sum & 0xffff);
    }
    return ~sum;
}