//
// Created by ciaowhen on 2022/7/21.
//

#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件路径
    char server_path[200];
    getcwd(server_path, 200);   //返回当前进程的工作目录。
    char root[6] = "/root";
    m_root = (char *) malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);       //将当前目录与root文件路径拼接

    //定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode()
{
    //LT + LT
    if(0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if(1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET+LT
    else if(2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET+ET
    else if(3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if(0 == m_close_log)
    {
        //初始化日志
        if(1 == m_log_write)    //异步写
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else    //同步写
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000,0);
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306,m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_resuit(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //关闭连接
    if(0 == m_OPT_LINGER)
    {
        struct linger tmp{0,1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if(1 == m_OPT_LINGER)
    {
        struct linger tmp{1,1};     //将连接的关闭设置一个超时。如果socket发送缓冲区中仍残留数据，进程进入睡眠，内核进入定时状态去尽量去发送这些数据。
        //在超时之前，如果所有数据都发送完且被对方确认，内核用正常的FIN|ACK|FIN|ACK四个分组来关闭该连接，close()成功返回。
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(m_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    //初始化定时器使用类
    utils.init(TIMESLOT);
    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);    //创建本地套接字（管道）
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);  //设置管道读端为非阻塞
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);      //将管道添加到监听树上，设置非阻塞

    //SIGIPE：当服务器close一个连接时，若client端接着发数据。根据TCP协议的规定，会收到一个RST响应，client再往这个服务器发送数据时，系统会发出一个SIGPIPE信号给进程
    //SIG_IGN：忽略信号
    utils.addsig(SIGPIPE, SIG_IGN);     //为了防止客户端进程终止，而导致服务器进程被SIGPIPE信号终止，因此服务器程序要处理SIGPIPE信号。
    utils.addsig(SIGALRM, utils.sig_handler, false);    //SIGALARM：定时器终止时发送给进程的信号   若接收到SIGALRM信号，从管道写端写入
    utils.addsig(SIGTERM, utils.sig_handler, false);    //SIGTERM：当前进程被kill   若接收到SIGTERM信号，从管道写端写入

    alarm(TIMESLOT);

    //工具类，信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

//设置定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    //初始化当前users的http连接
    users[connfd].init(connfd, client_address,m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);
    //初始化处client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;

    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    LOG_INFO("%s", "adjust timer once");    //写入日志
}

//删除定时器
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if(timer)
        utils.m_timer_lst.del_timer(timer);
    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

//接收客户连接并分配定时器
bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addlength = sizeof(client_address);
    if(0 == m_LISTENTrigmode)   //LT
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addlength);
        if(connfd < 0)
        {
            LOG_ERROR("%s:errno is %d", "accept error", errno);
            return false;
        }
        if(http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }

    else        //ET
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address,&client_addlength);
            if(connfd < 0)
            {
                LOG_ERROR("%s:errno is %d", "accept error", errno);
                break;
            }
            if(http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

//处理定时器信号（即管道中的信号）
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);    //接收来自管道写端的数据（信号）
    if(ret == -1)
        return false;
    else if(ret == 0)
        return false;
    else
    {
        for(int i = 0; i < ret; ++i)
        {
            switch (signals[i])     //信号本身是整型数值，管道中传递的是ASCII码表中整型数值对应的字符。
            {
                case SIGALRM:       //switch的变量一般为字符或整型，当switch的变量为字符时，case中可以是字符，也可以是字符对应的ASCII码。
                {
                    timeout = true;
                    break;
                }
                case SIGTERM:
                {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

//处理读事件，将相应请求加入到请求队列
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if(1 == m_actormodel)
    {
        if(timer)
            adjust_timer(timer);

        //若检测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);  //state为0表示读请求

        while(true)
        {
            if(1 == users[sockfd].improve)  //当请求已被接收
            {
                if(1 == users[sockfd].timer_flag)   //取消定时器
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improve = 0;
                break;
            }
        }
    }
    else        //proactor
    {
        if(users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            //若检测到读事件，将该事件放入到请求队列
            m_pool->append_p(users + sockfd);
            if(timer)
                adjust_timer(timer);
        }
        else
            deal_timer(timer, sockfd);
    }
}

//处理写事件，将相应请求加入到请求队列
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    if(1 == m_actormodel) //reactor
    {
        if(timer)
            adjust_timer(timer);
        m_pool->append(users+sockfd, 1);
        while(true)
        {
            if(1 == users[sockfd].improve)
            {
                if(1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improve = 0;
                break;
            }
        }
    }
    else    //practor
    {
        if(users[sockfd].write())       //直接进行IO写数据
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            if(timer)
                adjust_timer(timer);
        }
        else
            deal_timer(timer, sockfd);
    }
}

//循环监听事件
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while(!stop_server)
    {
        int number = epoll_wait(m_epollfd, events,MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR)        // EINTR表示在读/写的时候出现了中断错误
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for(int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if(sockfd == m_listenfd)    //监听socket，说明是客户端连接请求
            {
                bool flag = dealclinetdata();
                if(false == flag)
                    continue;
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if(false == (flag))
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if(events[i].events & EPOLLIN)
                dealwithread(sockfd);
            else if(events[i].events & EPOLLOUT)
                dealwithwrite(sockfd);
        }
        if(timeout)
        {
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}