//
// Created by ciaowhen on 2022/7/21.
//

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;       //最大文件描述符
const int MAX_EVENT_NUMBER = 10000;     //最大事件数
const int TIMESLOT = 5;          //最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();
    void init(int port, string user, string passWord, string databaseName, int log_write, int opt_linger, int trimode, int sql_num, int thread_num, int close_log, int actor_model);    //初始化

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool &timeout, bool &stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

private:
    //基础
    int m_port;         //端口号
    char *m_root;       //当前工作目录
    int m_log_write;    //写日志为异步或同步，异步为1
    int m_close_log;    //日志是否关闭,关闭为1
    int m_actormodel;   //模式(1为reactor 0为proactor

    int m_pipefd[2];    //读取定时器状态的管道
    int m_epollfd;      //epoll树根节点fd
    http_conn *users;   //http_conn类对象

    //数据库相关
    connection_pool *m_connPool;    //连接池
    string m_user;          //登陆数据库用户名
    string m_passWord;      //登陆数据库密码
    string m_databaseName;  //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;      //线程池
    int m_thread_num;                   //线程池中的线程数

    //epoll相关
    epoll_event events[MAX_EVENT_NUMBER];   //epoll事件

    int m_listenfd;         //监听套接字
    int m_OPT_LINGER;       //连接状态
    int m_TRIGMode;         //触发模式
    int m_LISTENTrigmode;   //ET
    int m_CONNTrigmode;     //LT

    //定时器相关
    client_data *users_timer;   //定时器中客户端socket的资源（包括它的定时器）
    Utils utils;                //定时器处理
};

#endif //WEBSERVER_H
