//
// Created by ciaowhen on 2022/7/16.
//

#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../log/log.h"
#include "../lock/locker.h"
using namespace std;

class connection_pool
{
public:
    MYSQL *GetConnection();                 //获取数据库连接
    bool ReleaseConnection(MYSQL *conn);    //释放连接
    int GetFreeConn();                      //获取空闲连接数
    void DestroyPool();                     //销毁所有连接

    static connection_pool *GetInstance();  //单例模式
    void init(string url, string User, string PassWord, string DatabaseName, int Port, int MaxConn, int close_log);

private:
    connection_pool();
    ~connection_pool();

    int m_MaxcConn;       //最大连接数
    int m_CurConn;        //当前已使用的连接数
    int m_FreeCon;        //当前空闲的连接数
    locker lock;          //互斥锁
    list<MYSQL *> connList;     //连接池
    sem reserve;          //信号量

public:
    string m_url;         //主机地址
    string m_Port;        //数据库端口号
    string m_User;        //登录数据库用户名
    string m_PassWord;    //登录数据库用户密码
    string m_DatabaseName;//使用数据库名
    int m_close_log;      //日志开关
};

//RAII机制释放数据库连接
class connectionRAII
{
public:
    //数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改。
    connectionRAII(MYSQL **SQL, connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *connRAII;
    connection_pool *poolRAII;
};

#endif //SQL_CONNECTION_POOL_H
