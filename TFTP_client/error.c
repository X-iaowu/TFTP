#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include <string.h>
#include <tchar.h>
#include<winsock2.h>
#include <Windows.h>
#include<ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

#define SERVER_PORT 69
#define BUFFER_SIZE 512
#define FILE_NAME_MAX_SIZE 512
;
char* new_char(int len)
{
    char* new;
    new = (char*)malloc(sizeof(char) * len);
    return new;
}

//生成并初始化一个UDPsocket
int getUdpSocket()
{
    WORD ver = MAKEWORD(2, 2);
    //MAKEWORD(1,1)和MAKEWORD(2,2)的区别在于，前者只能一次接收一次，不能马上发送，而后者能
    WSADATA IpData;
    int err = WSAStartup(ver, &IpData);
    if (err != 0) return -1;
    int udpsocket = socket(AF_INET, SOCK_DGRAM, 0);   //AF_INET指代TCP/IP-IPV4协议族
    if (udpsocket == INVALID_SOCKET) return -2;
    return udpsocket;
}

//根据输入的IP地址和端口号构造出一个存储着地址的sockaddr_in类型
sockaddr_in getAddr(const char* ip, int port)
{
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.S_un.S_addr = inet_addr(ip);
    return addr;
}

//构造RRQ包
char* RequestDownloadPack(char* content, int* datalen, int type)
{
    int len = strlen(content);
    char* buf = new_char(len + 2 + 2 + type);
    buf[0] = 0x00;
    buf[1] = 0x01;      //RRQ在TFTP中用0x01表示
    memcpy(buf + 2, content, len);      //将被请求的文件名放入RRQ数据包
    memcpy(buf + 2 + len, "\0", 1);
    if (type == 5)      //根据用户规定的传输格式来构造数据包
    {
        memcpy(buf + 2 + len + 1, "octet", 5);
    }
    else
        memcpy(buf + 2 + 1 + len, "netascii", 8);
    memcpy(buf + 2 + len + 1 + type, "\0", 1);
    *datalen = len + 2 + 1 + type + 1;           //datalen是一个引用变量，通过它将数据长度传递出去
    return buf;
}

char* RequestUpdownloadPack(char* content, int* datalen, int type)
{
    int len = strlen(content);
    char* buf = new_char(len + 2 + 2 + type);
    buf[0] = 0x00;
    buf[1] = 0x02;
    memcpy(buf + 2, content, len);
    memcpy(buf + 2 + len, "\0", 1);
    if (type == 5)
        memcpy(buf + 2 + len + 1, "octet", 5);
    else
        memcpy(buf + 2 + len + 1, "netascii", 8);
    memcpy(buf + 2 + len + 1 + type, "\0", 1);
    *datalen = len + 2 + 1 + type + 1;
    return buf;

}

//制作ack数据包
char* AckPack(short* no)
{
    char* ack = new_char(4);
    ack[0] = 0x00;
    ack[1] = 0x04;              //ACK数据包的编号是0x04
    no = htons(no);
    /*将主机的字节序转换为网络字节序，由于不同操作系统的存储方式不同为了方便交流，
    规定了统一的网络传输字节序，由对应的主机根据自身系统的存储规则转换为主机字节序，
    ntohs函数与hton函数相反，将网络字节序转换为主机字节序，
    这里是因为no是引用类型的变量，所以必须要在传输完成后恢复到主机上存储的格式
    */

    memcpy(ack + 2, &no, 2);
    no = ntohs(no);
    return ack;
}

//制作data数据包
char* MakeData(short* no, FILE* f, int* datalen)
{
    char temp[512];
    int sum = fread(temp, 1, 512, f);           //输入512个字节的文件内容
    if (!ferror(f))
    {
        char* buf = new_char(4 + sum);
        buf[0] = 0x00;
        buf[1] = 0x03;
        
        no = htons(no);
        memcpy(buf + 2, &no, 2);
        no = ntohs(no);
        memcpy(buf + 4, temp, sum);
        *datalen = sum + 4;          //通过引用变量datalen来传递数据包长度
        return buf;
    }
    else
        return NULL;
}

void print_time(FILE* fp)
{
    time_t t;
    time(&t);
    char stime[100];
    strcpy(stime, ctime(&t));
    *(strchr(stime, '\n')) = '\0';
    fprintf(fp, "[ %s ]", stime);
    return;
}

int main()
{
    FILE* fp = fopen("TFTP_client.log", "a+");
    char commonbuf[2048];//缓冲
    int buflen;
    int NumbertoKill;
    int Killtime;
    clock_t start, end;
    double runtime;
    SOCKET socket = getUdpSocket();
    SOCKADDR_IN addr;

    int revtimeout = 1000;
    int sendtimeout = 1000;

    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&revtimeout, sizeof(int));
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&sendtimeout, sizeof(int));

    while (1)
    {
        printf("+---------------------------------------+\n");
        printf("| 1.上传文件           2.下载文件       |\n");
        printf("| 0.关闭TFTP客户端                      |\n");
        printf("+---------------------------------------+\n");

        int choice;
        scanf("%d", &choice);
        if (choice == 1)
        {
            addr = getAddr("10.12.180.34", 69);
            printf("请输入要上传的文件的全名：\n");
            char name[1000];
            int type;
            scanf("%s", name);


            printf("请选择上传文件的方式(选择序号):1.netascii     2.octet\n");
            scanf("%d", &type);

            if (type == 1)
                type = 8;   //8表示"netascii"字符串的长度
            else
                type = 5;   //5表示"octet"字符串的长度
            int datalen;
            char* sendData = RequestUpdownloadPack(name, &datalen, type);
            buflen = datalen;
            NumbertoKill = 1;       //numbertokill变量表示一个数据包recv_from超时的次数
            memcpy(commonbuf, sendData, datalen);
            //commonbuf是一个专门用于数据重传的缓冲区，有可能被重传的数据都会统一的放进commonbuf中
            //重传机制会直接从commonbuf中获得数据

            //第一次发送WRQ包
            int res = sendto(socket, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));


            start = clock();//开始计时
            print_time(fp);
            fprintf(fp, "send WRQ for file:%s\n", name);

            Killtime = 1;   //表示sendto的超时的次数，与recv_from超时分开来计算
            while (res != datalen)  //如果sendto函数失败了，则立即重新sendto，在成功或者到达上限次数之前不会进行其他操作
            {
                printf("send WRQ failed %d times\n",Killtime);
                if (Killtime <= 10) //10次为上限
                {
                    res = sendto(socket, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));
                    Killtime++;
                }
                else
                    break;
            }

            if (Killtime > 10)
                continue;
            free(sendData);
            FILE* f = fopen(name, "rb");    //f指向目标文件
            if (f == NULL)
            {
                printf("File %s open failed!\n", name);
                continue;
            }

            //开始传输文件
            short block = 0;//文件编号
            datalen = 0;
            int RST = 0; //记录重传次数
            int Fullsize = 0;   //记录文件的总大小
            while (1)   //开始传输
            {
                char buf[1024];
                sockaddr_in server;     //从server反馈的数据包中活得分配的端口号
                int len = sizeof(server);
                res = recvfrom(socket, buf, 1024, 0, (sockaddr*)&server, &len);     //监听服务器的数据包
                if (res == -1)      //如果没有收到数据
                {
                    printf("%d ", NumbertoKill);
                    if (NumbertoKill > 10)      //如果连续10次没有收到回应
                    {
                        printf("No acks get. trainsmission failed\n");
                        print_time(fp);
                        fprintf(fp, "Upload file: %s failed.\n", name);
                        break;
                    }
                    int res = sendto(socket, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
                    //重传上一个数据包
                    RST++;

                    printf("resend last blk\n");

                    Killtime = 1;       //同上处理sendto超时的情况
                    while (res != buflen)
                    {
                        printf("Resend last blk failed %d times\n",Killtime);
                        if (Killtime <= 10)
                        {
                            res = sendto(socket, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
                            Killtime++;
                        }
                        else
                            break;
                    }
                    if (Killtime > 10)
                        break;
                    NumbertoKill++;
                }

                if (res > 0)     //收到服务器的回应数据包
                {
                    short flag;
                    memcpy(&flag, buf, 2);//获取数据包的类型编号
                    flag = ntohs(flag);
                    if (flag == 4)//收到的是ACK包
                    {
                        short no;
                        memcpy(&no, buf + 2, 2);
                        no = ntohs(no);
                        if (no == block)
                        {
                            addr = server;
                            if (feof(f) && datalen != 516)//如果上传文件已经全部上传完毕
                            {
                                printf("upload finished!");
                                end = clock();
                                runtime = (double)(end - start) / CLOCKS_PER_SEC;
                                //计算传输时间
                                print_time(fp);
                                printf("Average trainsmission rate: %.2lf kb/s\n", Fullsize / runtime / 1000);
                                fprintf(fp, "Upload file: %s finished.resent times:%d.Fullsize:%d\n", name, RST, Fullsize);
                                break;
                            }
                            block++;    //否则，制作下一个DATA包
                            sendData = MakeData(&block, f, &datalen);
                            buflen = datalen;
                            Fullsize += datalen - 4;
                            //fullsize要去除数据包中头部的长度
                            NumbertoKill = 1;//重置当前数据包的重发次数
                            memcpy(commonbuf, sendData, datalen);//更新commonbuf中的内容，准备下一次可能的重传
                            if (sendData == NULL)
                            {
                                printf("File reading mistakes!\n");
                                break;
                            }
                            int res = sendto(socket, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));
                            Killtime = 1;
                            while (res != datalen)
                            {
                                printf("send block %d failed\n", block);
                                if (Killtime <= 10)
                                {
                                    res = sendto(socket, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
                                    Killtime++;
                                }
                                else
                                    break;
                            }
                            if (Killtime > 10)
                                continue;
                            printf("Pack No = %d\n", block);
                        }
                    }
                    if (flag == 5)//处理错误包
                    {
                        short errorcode;
                        memcpy(&errorcode, buf + 2, 2);
                        errorcode = ntohs(errorcode);
                        char strError[1024];//继续拆解并获得错误详细信息
                        int iter = 0;
                        while (*(buf + iter + 4) != 0)
                        {
                            memcpy(strError + iter, buf + iter + 4, 1);
                            ++iter;
                        }
                        *(strError + iter + 1) = '\0';
                        printf("Error %d %s\n", errorcode, strError);
                        print_time(fp);
                        fprintf(fp, "Error %d %s", errorcode, strError);
                        break;
                    }
                }
            }
            fclose(f);
        }
        if (choice == 2)
        {
            addr = getAddr("10.12.180.34", 69);
            printf("请输入要下载的文件全名:\n");
            char name[1000];
            int type;
            scanf("%s", name);


            printf("请输入下载文件的方式:1.netascii  2.octet\n");
            scanf("%d", &type);

            if (type == 1)
                type = 8;
            else
                type = 5;
            int datalen;
            char* sendData = RequestDownloadPack(name, &datalen, type);
            buflen = datalen;
            NumbertoKill = 1;
            memcpy(commonbuf, sendData, datalen);
            int res = sendto(socket, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));
            start = clock();
            print_time(fp);
            fprintf(fp, "send RRQ for file:%s\n", name);

            Killtime = 1;
            while (res != datalen)
            {
                printf("send RRQ failed %d times\n", Killtime);
                if (Killtime <= 10) //10次为上限
                {
                    res = sendto(socket, commonbuf, datalen, 0, (sockaddr*)&addr, sizeof(addr));
                    Killtime++;
                }
                else
                    break;
            }
            if (Killtime > 10)
                continue;
            free(sendData);
            FILE* f = fopen(name, "wb");    //f指向目标文件
            if (f == NULL)
            {
                printf("File %s open failed!\n", name);
                continue;
            }

            //开始传输文件
            short block = 0;
            datalen = 0;

            int want_recv = 1;
            int RST = 0; //记录重传次数
            int Fullsize;   //记录文件的总大小
            while (1)   //开始传输
            {
                char buf[1024];
                sockaddr_in server;     //从server反馈的数据包中活得分配的端口号
                int len = sizeof(server);
                res = recvfrom(socket, buf, 1024, 0, (sockaddr*)&server, &len);     //监听服务器的数据包
                if (res == -1)      //如果没有收到数据
                {
                    if (NumbertoKill > 10)      //如果连续10次没有收到回应
                    {
                        printf("No block get. trainsmission failed\n");
                        print_time(fp);
                        fprintf(fp, "Download file: %s failed.\n", name);
                        break;
                    }
                    int res = sendto(socket, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
                    //重传上一个数据包
                    RST++;

                    printf("resend last blk\n");

                    Killtime = 1;       //同上处理sendto超时的情况
                    while (res != buflen)
                    {
                        printf("Resend last blk failed %d times\n", Killtime);
                        if (Killtime <= 10)
                        {
                            res = sendto(socket, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
                            Killtime++;
                        }
                        else
                            break;
                    }
                    if (Killtime > 10)
                        break;
                    NumbertoKill++;
                }

                if (res > 0)
                {
                    short flag;
                    memcpy(&flag, buf, 2);//获取数据包的类型编号
                    flag = ntohs(flag);
                    if (flag == 3)//如果收到了服务器发来的DATA数据包
                    {
                        addr = server;
                        short no;
                        memcpy(&no, buf + 2, 2);
                        no = ntohs(no);
                        printf("Pack No = %d\n", no);//告知用户当前收到的数据包的编号
                        char* ack = AckPack(no);//对该数据包制作ACK
                        int sendlen = sendto(socket, ack, 4, 0, (sockaddr*)&addr, sizeof(addr));
                        Killtime = 1;
                        while (sendlen != 4)//发送长度不为4说明ACK包sendto出错
                        {
                            printf("resend last ack failed %d times", Killtime);
                            if (Killtime <= 10)
                            {
                                sendlen = sendto(socket, ack, 4, 0, (sockaddr*)&addr, sizeof(addr));
                                Killtime++;
                            }
                            else
                                break;
                        }
                        if (Killtime > 10)
                            break;
                        if (no == want_recv)//want_recv变量为用户当前期望收到的下一个数据包的编号
                        {
                            buflen = 4;
                            NumbertoKill = 1;
                            memcpy(commonbuf, ack, 4);//更新commonbuf的内容，当超时发生时，客户端将会不断重传

                            fwrite(buf + 4, res - 4, 1, f);
                            Fullsize += res - 4;
                            if (res - 4 >= 0 && res - 4 < 512)//如果当前数据包的大小小于512，说明传输完毕
                            {
                                printf("Download finished!\n");//告知用户传输结束
                                end = clock();
                                runtime = (double)(end - start) / CLOCKS_PER_SEC;//计算时间
                                print_time(fp);
                                printf("Average trainsmission rate:%.2lf kb/s\n", Fullsize / runtime / 1000);
                                fprintf(fp,"Download file %s finished. resend times :%d. Fullsize:%d\n",name,RST,Fullsize);
                                break;
                            }
                            want_recv++;
                        }
                    }
                    if (flag == 5)
                    {
                        short errorcode;
                        memcpy(&errorcode, buf + 2, 2);
                        errorcode = ntohs(errorcode);
                        char strError[1024];//继续拆解并获得错误详细信息
                        int iter = 0;
                        while (*(buf + iter + 4) != 0)
                        {
                            memcpy(strError + iter, buf + iter + 4, 1);
                            ++iter;
                        }
                        *(strError + iter + 1) = '\0';
                        printf("Error %d %s\n", errorcode, strError);
                        print_time(fp);
                        fprintf(fp, "Error %d %s", errorcode, strError);
                        break;
                    }
                }
            }
            fclose(f);
        }
        if (choice == 0)
            break;
    }
    fclose(fp);
    return 0;
}