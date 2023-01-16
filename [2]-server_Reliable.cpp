/*
 * Created by miubai on 2021/12/3.
 * 改版原因：
 * 4版窗口没设计清楚，固定窗口，从buf向后走，而不是buf长度就是win的大小
 * 4版读文件存在问题，文件读取，先统一加载到buf中，然后再通过窗口发送给用户，
 * 5版修改了策略，代码更加简介
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
char filename[] = "/tmp/serverFile/test.txt";
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
//计算校验和
unsigned short makesum(int count, char* buf);

/*
 * 等待建立连接
 * */
int waitConn(int sfd,struct sockaddr_in local_addr,struct sockaddr_in *cli_addr);

/*
 * 判断帧是否有效
 * */
int Judge(int sfd, struct PACKAGE sendPackage,struct PACKAGE *recvPackage,int *length);

/*
 * 发包函数
 * 只发包，sendto再定制
 * return -1 发送失败
 * */
int sendPackage(int sfd,char buff[], struct PACKAGE recPackage, unsigned short seq, struct sockaddr_in cli_addr,struct PACKAGE *package);

int main(int argc,char *argv[]) {
    if (argc != 3) {
        perror("error args! please ip and ports!\n");
        return -1;
    }
    //才发现写错了服务器与客户端。。岔行了离谱
    //既然只能做服务器，那就先创建等待建立连接
    int sfd = socket(AF_INET,SOCK_DGRAM,0);//socket文件描述符
    struct sockaddr_in local_addr;
    memset(&local_addr,0,sizeof(local_addr));

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr(argv[1]);
    local_addr.sin_port = htons(atoi(argv[2]));
    int bid_ret = bind(sfd,(struct sockaddr*)&local_addr,sizeof(local_addr));//绑定本机地址
    if (-1 == bid_ret){
        perror("bind error");
        return -1;
    }
    //地址绑定成功，开始等待建立连接
    struct sockaddr_in cli_addr;
    memset(&cli_addr,0,sizeof(cli_addr));
    int wait_ret = waitConn(sfd,local_addr,&cli_addr);//客户机信息都在cli_addr里面
    if (-1 == wait_ret){
        perror("waitConn");
        return -1;
    }
    cout << "与客户机：" << inet_ntoa(cli_addr.sin_addr) << " port = " << cli_addr.sin_port << "成功建立连接\n";
    //连接成功建立,等待客户发包，然后收包
    FILE *fp = fopen(filename,"w");//以写的方式
    struct PACKAGE recvPackage;//接收到的包
    memset(&recvPackage,0,sizeof(recvPackage));
    struct PACKAGE senPackage;//发确认包
    memset(&senPackage,0,sizeof(senPackage));
    senPackage.tag = 0x06;//syn ack
    senPackage.seq = 1;//发送给客户端一次
    char buff[BUFSIZE];
    unsigned short seq = 1;//本地序号
    if (!fp){
        perror("fopen error");
        return -1;
    } else{
        int judge_ret = 0;
        int length = 0;//记录接收文件的长度
        //接收文件，并将数据放到文件里面，在此之前，需要判断是否有效，一直接收一直判断
        while (judge_ret = Judge(sfd,senPackage,&recvPackage,&length)){
            //如果帧不对，直接停止
            if (-1 == judge_ret){
                perror("judge error");
                return -1;
            }
            if (2 == judge_ret){
                //要求重传上一帧
                cout << "请求重传上一帧\n";
                seq++;
                sendPackage(sfd,"0",recvPackage,seq,cli_addr,&senPackage);//发送重传帧
                continue;
            }
            cout << "阻塞了？\n";
            //如果都没有问题，则直接接收，写入文件中
            memset(buff,0,sizeof(buff));
            memcpy(&buff,&recvPackage.data,sizeof(recvPackage.data));//放到缓冲区了，然后写入文件
            int writ_length = fwrite(buff, sizeof(char),length,fp);//写入文件
            if (writ_length < length){
                perror("fwrite error");
                return -1;
            }
            memset(buff,0,sizeof(buff));//清空缓冲区
            seq++;//序号加1
        }
        fclose(fp);

    }
    close(sfd);

}



/*
 * 等待建立连接
 * */
/*
int waitConn(int sfd, struct sockaddr_in *cli_addr,){
    struct PACKAGE synPackage;//连接建立请求包
    memset(&synPackage,0,sizeof(synPackage));
    struct sockaddr_in from;
    memset(&from,0,sizeof(from));
    socklen_t  len = sizeof(from);
    //请求包做好了，直接发送给客户即？？？，，，
    //阻塞接收客户机请求
    int rec_ret = recvfrom(sfd,&synPackage,sizeof(synPackage),0,(struct sockaddr*)&from,&len);//来源信息再from中
    if (-1 == rec_ret){
        perror("waitConn synConn erro");
        return -1;
    }
    //成功收到了连接建立请求，需确认
    if (0x04 == synPackage.tag){
        memcpy(cli_addr,&from,sizeof(from));
        //确实是在请求建立连接
        cout << "客户机: " << inet_ntoa(from.sin_addr) << "port = " << from.sin_port << "\n";

        //发送第二次握手帧

    }

}*/
/*
 * 懒得重写连接了，将就用算了
 * */
int waitConn(int sfd,struct sockaddr_in local_addr,struct sockaddr_in *cli_addr){

    struct sockaddr_in from_addr;//客户地址
    struct PACKAGE recConnSyn;//syn包
    memset(&recConnSyn,0,sizeof(recConnSyn));//初始化
    socklen_t len = sizeof(from_addr);
    int recv_ret = recvfrom(sfd,&recConnSyn,sizeof(recConnSyn),0,(struct sockaddr*)&from_addr,&len);
    if (-1 == recv_ret ){
        perror("initSer recvfrom error");
        return -1;
    }
    if (recv_ret > 0){
        //如果接收到了数据
        cout << "接收到了来自客户机发来的[" << recv_ret <<  "]字节的数据\n";
        cout << "客户机地址：" << inet_ntoa(from_addr.sin_addr) << "\nport = " << from_addr.sin_port << "\n";
        if (0x04 == recConnSyn.tag){
            //如果是syn请求建立连接报文
            cout << "客户机请求建立连接\n";
            struct PACKAGE recConnAck;
            memset(&recConnAck,0,sizeof(recConnAck));//初始化
            recConnAck.tag = 0x06;//syn ack fin 所以是0x06
            recConnAck.seq = recConnSyn.seq + 1;//期望接收的下一帧
            recConnAck.ack = recConnSyn.seq;//确认前一帧
            int sen_ret = sendto(sfd,&recConnAck,sizeof(recConnAck),0,(struct sockaddr*)&from_addr,sizeof(from_addr));//发送ack帧给客户机
            if (-1 == sen_ret){
                perror("initSer sendto error");
                return -1;
            }
            cout << "发送第二次握手帧成功\n" << "tag = " << recConnAck.tag << "\n";
            //发送ack帧成功，现在接收最后一次握手帧
            struct PACKAGE finConn;
            memset(&finConn,0,sizeof(finConn));
            recv_ret = recvfrom(sfd,&finConn,sizeof(finConn),0,(struct sockaddr*)&from_addr,&len);
            if (-1 == recv_ret){
                perror("initSer recvfrom finConn");
                return -1;
            }
            //最后一次握手帧成功收到
            cout << "成功收到最后一次握手帧！连接成功建立。\n";
            //cout << "addr = " << inet_ntoa(from_addr.sin_addr) << "\n";
            memcpy(cli_addr,&from_addr,sizeof(from_addr));//返回客户机地址
            return sfd;//成功则将文件描述符返回
        }
    }
}
/*
 * 判断收到的帧是否有效
 * */
int Judge(int sfd, struct PACKAGE sendPackage,struct PACKAGE *recvPackage,int *length){
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
            *length = rec_ret;//长度返回
            cout << "收到seq = " << aPackage.seq << " ack = " << aPackage.ack << "数据报\n";
            return 1;

        } else{
            //乱序，重传
            cout << "接收方出现乱序了，需要重传！\n";
            return 2;
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
    //发包成功
    return 1;
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