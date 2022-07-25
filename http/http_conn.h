//
// Created by ciaowhen on 2022/7/17.
//

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

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
#include <cstring>
#include <pthread.h>
#include <stdlib.h>
#include <iostream>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    //设置读写文件的名称m_read_file的大小
    static const int FILENAME_LEN = 200;
    //设置读缓冲区m_read_buf的大小
    static const int READ_BUFFER_SIZE = 2048;
    //设置写缓冲区m_write_buf的大小
    static const int WRITE_BUFFER_SIZE = 1024;
    //报文请求方法，本项目只用到GET和POST
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    //主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    //报文解析的结果
    enum HTTP_CODE
    {
        NO_REQUEST,         //请求不完整，需要继续读取请求报文数据
        GET_REQUEST,        //获得了完整的HTTP请求
        BAD_REQUEST,        //HTTP请求报文有语法错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,     //服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION
    };
    //从状态机状态
    enum LINE_STATUS
    {
        LINE_OK = 0,    //完整读取一行
        LINE_BAD,       //报文语法有误
        LINE_OPEN       //读取的行不完整
    };

public:
    http_conn(){}
    ~http_conn(){}

public:
    //初始化套接子地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    //关闭http连接
    void close_conn(bool read_close = true);
    //
    void process();
    //读取浏览器端发来的全部数据
    bool read_once();
    //响应报文写入函数
    bool write();
    //获取套接字地址结构
    sockaddr_in *get_address(){ return &m_address;}
    //同步线程初始化数据库读取表
    void initmysql_resuit(connection_pool *connPool);
    //CGI使用线程池初始化数据库表
    //void initresultFile(connection_pool *connPool);

private:
    void init();
    //从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
    //向m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);
    //主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    //主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    //主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
    //生成响应报文
    HTTP_CODE do_request();

    //m_start_line是已经解析的字符
    //get_line用于将指针向后偏移，指向未处理的字符
    char* get_line(){ return m_read_buf + m_start_line; };
    //从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();

    void unmap();   //取消文件内容映射

    //根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char* format,...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_len);
    bool add_content_type();
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;       //epoll根节点fd
    static int m_user_count;    //连接的用户数量
    MYSQL *mysql;               //数据库连接
    int m_state;                //读为0,写为1
    int timer_flag;             //定时器是否取消标志
    int improve;                 //请求是否已接收
private:
    int m_sockfd;           //对端连接的套接字
    sockaddr_in m_address;  //套接字地址结构
    char m_read_buf[READ_BUFFER_SIZE];  //存储读取的请求报文数据
    int m_read_idx;         //缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx;      //m_read_buf读取的位置m_check_idx
    int m_start_line;       //m_read_buf中已经解析的字符个数

    char m_write_buf[WRITE_BUFFER_SIZE];    //存储发出的响应报文的数据
    int m_write_idx;                        //指示buffer中的长度

    CHECK_STATE m_check_state;      //主状态机的状态
    METHOD m_method;                //请求方法

    //以下为解析请求报文中对应的6个变量
    //存储读取文件的名称
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;              //连接状态

    char *m_file_address;       //读取服务器上的文件地址
    struct stat m_file_stat;    //文件属性
    struct iovec m_iv[2];       //io向量机制iovec
    int m_iv_count;             //iovec个数
    int cgi;                    //是否启用的POST;
    char *m_string;             //存储请求头数据
    int bytes_to_send;          //剩余发送字节数
    int bytes_have_send;        //已发送字节数

    char *doc_root;                 //网站根目录，文件夹内存放请求的资源和跳转的html文件
    map<string, string> m_users;    //用户名密码数据映射表
    int m_TRIGMode;                 //触发模式
    int m_close_log;                //日志打开状态

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];

};



#endif //HTTP_CONN_H
