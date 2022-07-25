//
// Created by ciaowhen on 2022/7/16.
//

#include <iostream>
#include <mysql/mysql.h>
#include <string>
#include <cstring>
#include <stdlib.h>
#include <list>
#include "sql_connection_pool.h"
using namespace std;

connection_pool::connection_pool()
{
    m_CurConn = 0;
    m_FreeCon = 0;
}
//RAII机制销毁初始化;
connection_pool::~connection_pool()
{
    DestroyPool();
}
connection_pool *connection_pool::GetInstance()
{
    static connection_pool connPool;
    return &connPool;
}

//初始化连接池，即创建一堆数据库连接
void connection_pool::init(string url, string User, string PassWord, string DatabaseName, int Port, int MaxConn, int close_log)
{
    //初始化数据库信息
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DatabaseName;
    m_close_log = close_log;

    //创建MaxConn条数据库连接
    for(int i = 0; i < MaxConn; i++)
    {
        MYSQL *conn = NULL;
        conn = mysql_init(conn);      //初始化MYSQL句柄

        if(conn == NULL)
        {
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        //连接数据库
        conn = mysql_real_connect(conn, url.c_str(), User.c_str(), PassWord.c_str(), DatabaseName.c_str(), Port, NULL,0);
        if(conn == NULL)
        {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        //更新连接池和空闲连接数量
        connList.push_back(conn);
        ++m_FreeCon;
    }
    //将信号量初始化为最大连接次数
    reserve = sem(m_FreeCon);
    m_MaxcConn = m_FreeCon;
}

//当有连接请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
    MYSQL *conn = NULL;

    if(0 == connList.size())
        return NULL;

    //取出连接，连接信号量-1,0则等待
    reserve.wait();
    lock.lock();        //说明取到连接，加锁操作

    conn = connList.front();
    connList.pop_front();
    --m_FreeCon;
    ++m_CurConn;

    lock.unlock();
    return conn;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *conn)
{
    if(NULL == conn)
        return false;

    lock.lock();

    connList.push_back(conn);
    ++m_FreeCon;
    --m_CurConn;

    lock.unlock();
    reserve.post();     //信号量+1
    return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{
    lock.lock();
    if(connList.size() > 0)
    {
        list<MYSQL *>::iterator it;     //通过迭代器迭代，遍历数据库连接池
        for(it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL *conn = *it;
            mysql_close(conn);          //关闭当前数据库连接
        }
        m_CurConn = 0;
        m_FreeCon = 0;
        connList.clear();   //清空队列
    }
    lock.unlock();
}

//返回当前空闲的连接数
int connection_pool::GetFreeConn()
{
    return this->m_FreeCon;
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
    *SQL = connPool->GetConnection();       //获取一个空闲连接
    connRAII = *SQL;                        //connRAII指针指向当前得到的MYSQL连接指针
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(connRAII);      //释放当前连接
}
