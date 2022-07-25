//
// Created by ciaowhen on 2022/7/16.
//


//线程池的安全销毁问题
//
//
//
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../http/http_conn.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_quest = 10000);
    ~threadpool();
    bool append(T *request, int state);     //向请求队列中添加任务
    bool append_p(T *request);
private:
    //工作线程运行的函数，它不断从工作队列中取出任务并执行
    static void *worker(void *arg);
    //主要实现，工作线程从请求队列中取出某个任务进行处理，
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //任务是否需要处理的信号量
    connection_pool *m_connPool;//数据库连接池
    bool m_stop;                //是否结束线程
    int m_actor_model;          //模型切换（reactor和proactor）1为reactor 0为proactor
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_quest):m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_quest),m_threads(NULL),m_stop(false),m_connPool(connPool)
{
    if(thread_number <= 0 || max_quest <= 0)
        throw std::exception();
    //线程id初始化
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
        throw std::exception();
    for(int i = 0; i < thread_number; ++i)
    {

        //循环创建线程，并将工作线程按要求进行运行
        if(pthread_create(m_threads+i,NULL,worker,this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        //将线程进行分离后，不用单独对工作线程进行回收
        if(pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    m_stop = 1;
    //由于我们将线程进行了线程分离，它的资源会被系统自动回收，而不再需要在其它线程中对其进行 pthread_join() 操作。
    delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    //根据条件，预先设置请求队列的最大值
    if(m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    //添加任务
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    //增加信号量
    m_queuestat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
    //将参数强转为线程池类，调用成员方法
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait();     //等待队列信号量
        m_queuelocker.lock();
        if(m_workqueue.empty())     //如果为空，说明信号被另一个线程抢走，解锁跳过重新等待信号量
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();   //取出请求队列的第一个请求
        m_workqueue.pop_front();            //出队
        m_queuelocker.unlock();             //解除请求队列的互斥锁，此时其他线程可以获取请求队列的资源
        if(!request)
            continue;
        if(1 == m_actor_model)  //reactor
        {
            if(0 == request->m_state)       //说明此时为读请求
            {
                if(request->read_once())     //循环读取客户数据，直到无数据可读或对方关闭连接
                {
                    request->improve = 1;   //已接收请求
                    connectionRAII mysqlconn(&request->mysql, m_connPool);      //取出数据库连接池里面的一个空闲连接
                    request->process();     //处理请求
                }
                else
                {
                    request->improve = 1;
                    request->timer_flag = 1;        //定时器标志位
                }
            }
            else        //说明为写请求
            {
                if(request->write())
                    request->improve = 1;
                else
                {
                    request->improve = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else        //proactor
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif //THREADPOOL_H
