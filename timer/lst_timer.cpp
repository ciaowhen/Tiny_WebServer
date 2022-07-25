//
// Created by ciaowhen on 2022/7/17.
//

#include <cstring>
#include "lst_timer.h"
#include "../http/http_conn.h"

//销毁定时器容器
sor_timer_lst::~sor_timer_lst()
{
    util_timer *tmp = head;
    while(tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

//添加定时器，内部调用私有成员add_timer
void sor_timer_lst::add_timer(util_timer *timer)
{
    if(!timer)
        return;
    //若定时器容器为空，直接加入
    if(!head)
    {
        head = tail = timer;
        return;
    }
    //如果新的定时器小于定时器头节点，则需要将当前定时器节点作为头部节点
    if(timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    //否则调用私有成员，调整内部节点
    add_timer(timer,head);
}

//调整定时器，任务发生变化时，调整定时器在链表的位置
void sor_timer_lst::adjust_timer(util_timer *timer)
{
    if(!timer)
        return;

    util_timer *tmp = timer->next;
    //被调整的定时器在链表尾部
    //定时器超时值仍然小于下一个定时器超时值，不调整
    if(!tmp || (timer->expire < tmp->expire))
        return;

    //被调整定时器是链表头结点，将定时器取出，重新插入
    if(timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    //被调整定时器在内部，将定时器取出，重新插入
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer,timer->next);
    }
}
//删除定时器
void sor_timer_lst::del_timer(util_timer *timer)
{
    if(!timer)
        return;
    if((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if(timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }

    if(timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}
//定时任务处理函数 (SIGALRM信号每次被触发，主循环中调用一次定时任务处理函数，处理链表容器中到期的定时器。)
void sor_timer_lst::tick()
{
    if(!head)
        return;
    //获取当前时间
    time_t cur = time(NULL);
    util_timer *tmp = head;
    while(tmp)
    {
        if(cur < tmp->expire)
            break;
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        if(head)
            head->prev = NULL;
        delete tmp;
        tmp = head;
    }
}

//私有函数添加定时器，供add_timer和adjust_timer调用
void sor_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    //遍历当前结点之后的链表，按照超时时间找到目标定时器对应的位置，常规双向链表插入操作
    while(tmp)
    {
      // 因为后一个定时器是前一个定时器原来的下一个定时器，调整的时候，它必定比前一个定时器要大，所以不需要对它进行比较
      if(timer->expire < tmp->expire)
      {
          prev->next = timer;
          timer->next = tmp;
          tmp->prev = timer;
          timer->prev = prev;
          break;
      }
      prev = tmp;
      tmp = tmp->next;
    }
    //说明到了链表结尾
    if(!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

//定时器使用类
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;      //设置超时时间
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    //获取文件的flags，即open函数的第二个参数:
    int old_option = fcntl(fd, F_GETFL);
    //增加文件的某个flags,这里设置为非阻塞
    int new_option = old_option | O_NONBLOCK;
    //设置文件的flag
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPLLONESHOT
//EPOLLSHOT相当于说，某次循环中epoll_wait唤醒该事件fd后，就会从注册中删除该fd,也就是说以后不会epollfd的表格中将不会再有这个fd,也就不会出现多个线程同时处理一个fd的情况。
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if(1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  //EPOLLRDHUP对端断开连接的异常就可以在底层进行处理了，不用再移交到上层。
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if(one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重用性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;

    //将信号值从管道写端写入，传输字符类型，而非整型
    send(u_pipefd[1], (char *)&msg, 1, 0);

    //将当前的errno恢复为原来的errno
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    //最新版Web服务器项目详解 - 07 定时器处理非活动连接（上）
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;//sa_flags赋值SA_RESTART 可以让当前进程接着执行没有执行完毕的系统调用函数不会默认返回-1 异常终止

    //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);

    //assert函数功能主要是程序诊断，它可以将程序诊断信息写入标准错误文件中
    assert(sigaction(sig, &sa,NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

//向对端发生错误信息，关闭对端连接
void Utils::show_error(int connfd, const char *info)
{
    send(connfd,info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

void cb_func(client_data *user_data)
{
    //将相应的对端socket从epoll树上移除
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);

    //检测错误（assert的作用是现计算表达式 expression ，如果其值为假（即为0），那么它先向stderr打印一条出错信息，然后通过调用 abort 来终止程序运行。）
    assert(user_data);

    //关闭对端连接（这里的连接关闭是因为连接不活跃超时）
    close(user_data->sockfd);

    //用户连接数-1;
    http_conn::m_user_count--;
}