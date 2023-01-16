/*
 * Created by miubai on 2021/12/3.
 * 改版原因：
 * 4版窗口没设计清楚，固定窗口，从buf向后走，而不是buf长度就是win的大小
 * 4版写入策略为：接收一点内容写一点内容到文件中，第5版更改文：将接收的内容先统一存放到缓冲区，然后再一次性写入文件
 * 4版只能发送txt文件，
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

//BUFFSIZE为缓冲区大小，注意区分窗口大小
#define BUFSIZE 2048

FILE *fp;//
char filename[] = "/tmp/clientFile/test.txt";
using namespace std;

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
int reqConnect(int argc,char *argv[],struct PACKAGE *recPAckage, struct sockaddr_in *cli_addr);

/*
 * 发包函数
 * 只发包，sendto再定制
 * return -1 发送失败
 * */
int sendPackage(int sfd,char buff[], struct PACKAGE recPackage, unsigned short seq, struct sockaddr_in cli_addr,struct PACKAGE *package);

/*
 * 判断是否存在乱序的问题
 * 无则返回1，否则返回-1
 * 参数：文件描述符号，上一次的包，和接收的包
 * return 2 乱序重传
 * */
int Judge(int sfd, struct PACKAGE sendPackage,struct PACKAGE *recvPackage);

int main(int argc,char *argv[]){

    if (argc != 3){
        perror("error args! please ip and ports!\n");
        return -1;
    }
    struct PACKAGE recPackage;
    struct sockaddr_in cli_addr;
    //创建udp socket连接
    int sfd = reqConnect(argc,argv,&recPackage,&cli_addr);//可以获取到sfd，返回包，服务器地址cli——addr
    if (-1 == sfd){
        perror("reqConnect");
        return -1;
    }
    //此时连接建立成功，序号进行发送
    cout << "成功与服务器建立连接！\n";
    unsigned short win = BUFSIZE;//设置固定窗口就是缓冲区大小
    unsigned short seq = 0;//序号默认从0开始
    char buff[BUFSIZE];//缓冲区大小

    FILE *fp = fopen(filename,"r");
    if (!fp){
        perror("fopen error");
        return -1;
    } else{
        //如果确实读到了文件
        memset(buff,0,sizeof(buff));
        int file_block_len = 0;
        fseek(fp,0,SEEK_SET);//找到文件头

        while ( (file_block_len = fread(buff, sizeof(char), BUFSIZE, fp)) > 0){//每次读取的长度为缓冲区的大小
            struct PACKAGE sendPackages;
            memset(&sendPackages,0,sizeof(sendPackages));
            int send_ret = sendPackage(sfd,buff,recPackage,seq,cli_addr,&sendPackages);//发送对应的包
            if (-1 == send_ret){
                perror("send error");
                return -1;
            }
            cout << "从文件中读到了数据如下：\n" << buff << "\n";

            //发送完后立即阻塞，如果有且正确才能进行下一步,等ack
            int judge_ret = Judge(sfd,sendPackages,&recPackage);//接收的包有放到了recPackage中
            if (-1 == judge_ret){
                perror("judge error");
                return -1;
            }
            //乱序重发
            if (2 == judge_ret){
                //乱序，重发
                int send_ret = sendPackage(sfd,buff,recPackage,seq,cli_addr,&sendPackages);//发送对应的包
                if (-1 == send_ret){
                    perror("send error");
                    return -1;
                }
            }
            //如果通过，即没有乱序则继续读发,注意，要情况缓冲区
            seq++;//这是自己的序号，序号加一
            memset(buff,0,sizeof(buff));//清空缓冲区，继续读
        }
        //循环结束，则文件读完了也发完了，
        cout << "文件发送成功\n";
        fclose(fp);//关闭文件流
    }
    close(sfd);//关闭socket描述符
}

/*
 * 请求建立连接，发送连接建立请求包
 * */
int reqConnect(int argc,char *argv[],struct PACKAGE *recPAckage, struct sockaddr_in *cli_addr){

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
            memcpy(recPAckage,&finConn,sizeof(finConn));//将该对象返回
            memcpy(cli_addr,&ser_addr,sizeof(ser_addr));//将服务器地址拷走
            return sfd;//发送成功则表示成功建立连接，并将文件描述符返回
        }
    }
}

/*
 * 发包函数
 * 只发包，sendto再定制
 * return -1 发送失败
 * */
int sendPackage(int sfd,char buff[], struct PACKAGE recPackage, unsigned short seq, struct sockaddr_in cli_addr,struct PACKAGE *package){
    struct PACKAGE sendPackage;
    memset(&sendPackage,0,sizeof(sendPackage));
    memcpy(&sendPackage.data,buff,sizeof(buff));//data填充
    sendPackage.tag = 0x06;//syn ack fin
    sendPackage.seq = seq;//当前包的序号
    sendPackage.ack = recPackage.seq;//确认客户机的seq
    sendPackage.bufLen = sizeof(sendPackage.data);
    sendPackage.win = BUFSIZE;//我的窗口大小,默认为缓冲区大小
    sendPackage.checksum = makesum(sendPackage.bufLen, sendPackage.data);
    //数据封装完成，准备发包

    int send_ret = sendto(sfd,&sendPackage,sizeof(sendPackage),0,(struct sockaddr*)&cli_addr,sizeof(cli_addr));
    if (-1 == send_ret){
        perror("sendPackage sendto error");
        return -1;
    }
    memcpy(package,&sendPackage,sizeof(sendPackage));
    cout << "成功发送：seq = " << sendPackage.seq << " ack = " << sendPackage.ack << "的包\n";
    //发包成功
    return 1;
}

/*
 * 判断是否存在乱序的问题
 * 无则返回1，否则返回-1
 * 参数：文件描述符号，上一次的包
 * return 2 乱序重传
 * */
int Judge(int sfd, struct PACKAGE sendPackage,struct PACKAGE *recvPackage){
    struct PACKAGE aPackage;
    memset(&aPackage,0,sizeof(aPackage));
    struct sockaddr_in cli_addr;
    socklen_t len = sizeof(cli_addr);
    int rec_ret = recvfrom(sfd,&aPackage,sizeof(aPackage),0,(struct sockaddr*)&cli_addr,&len);
    if (-1 == rec_ret){
        perror("Judge recvfrom error");
        return -1;
    }
    //如果接收成功判断tag码元
    if (0x06 == aPackage.tag){
        //如果syn ack有效，确定其确认的序号是否有误
        if (aPackage.ack == sendPackage.seq){
            //确认无误，返回结束
            memcpy(recvPackage, &aPackage,sizeof(aPackage));//返回接收帧
            return 1;

        } else{
            //乱序，重传
            cout << "接收方出现乱序了，需要重传！\n";
            return 2;
        }
    }

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