//
// Created by ciaowhen on 2022/7/17.
//

#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include "../log/log.h"

class util_timer;

//定时器中客户端socket的资源（包括它的定时器）
struct client_data
{
    sockaddr_in address;    //客户端socket地址
    int sockfd;             //socket文件描述符
    util_timer *timer;      //定时器
};

//定时器类（为链表节点的结构）
class util_timer
{
public:
    util_timer(): prev(NULL), next(NULL){}
public:
    time_t expire;                  //超时时间
    void(* cb_func)(client_data *); //回调函数，定时事件，具体的，从内核事件表删除事件，关闭文件描述符，释放连接资源。
    client_data *user_data;         //连接资源
    util_timer *prev;               //前一个定时器地址
    util_timer *next;               //后一个定时器地址
};

//定时器容器类（为升序双向链表结构）   添加定时器的事件复杂度是O(n),删除定时器的事件复杂度是O(1)。
class sor_timer_lst
{
public:
    sor_timer_lst():head(NULL), tail(NULL){}
    ~sor_timer_lst();

    void add_timer(util_timer *timer);          //添加定时器
    void adjust_timer(util_timer *timer);       //调整定时器
    void del_timer(util_timer *timer);          //删除定时器
    void tick();                                //定时任务处理函数

private:
    void add_timer(util_timer *timer, util_timer *lst_head);    //供add_timer和adjust_timer调用
    util_timer *head;            //定时器容器头节点（超时时间最短）
    util_timer *tail;            //定时器容器尾节点（超时时间最长）
};

//定时器处理
class Utils
{
public:
    Utils(){};
    ~Utils(){};

    void init(int timeslot);
    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    //发送错误信息，关闭对端连接
    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;           //管道地址
    sor_timer_lst m_timer_lst;      //定时器容器
    static int u_epollfd;           //epoll事件监听管道读写事件（这里管道的读写事件即信号处理函数往管道的写端写入信号值）
    int m_TIMESLOT;                 //超时时间
};
void cb_func(client_data *user_data);

#endif //LST_TIMER_H
