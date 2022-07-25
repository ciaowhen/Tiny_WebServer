//
// Created by ciaowhen on 2022/7/15.
// 循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;
// 线程安全，每个操作前都要先加互斥锁，操作完成后，再解锁
//

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template<class T>
class block_queue
{
public:
    block_queue(int max_size = 1000)        //初始化阻塞队列
    {
        if(max_size <= 0)
            exit(-1);

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        //因为取队头队尾元素时我们将进行后移再取，故这里初值设为-1
        m_front = -1;
        m_back = -1;
    }

    void clear()       //清空阻塞队列
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue()      //销毁阻塞队列
    {
        m_mutex.lock();
        if(m_array != NULL)
            delete [] m_array;
        m_mutex.unlock();
    }

    bool full()     //判断队列是否已满
    {
        m_mutex.lock();
        if(m_size >= m_max_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool empty()    //判断队列是否为空
    {
        m_mutex.lock();
        if(0 == m_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool front(T &value)    //返回队首元素
    {
        m_mutex.lock();
        if(0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }

    bool back(T &value)     //返回队尾元素
    {
        m_mutex.lock();
        if(0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int size()      //返回队列长度
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }

    int max_size()  //返回队列最大长度
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

    //往队列添加元素时，需要将所有使用队列的线程先唤醒
    //当有元素push进队列时，相当于生产者生产了一个元素
    //若当前没有线程等待条件变量时，则唤醒没有意义
    bool push(const T &item)
    {
        m_mutex.lock();
        if(m_size >= m_max_size)
        {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back+1) % m_max_size;
        m_array[m_back] = item;
        m_size++;

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }
    //出队时，如果当前队列没有元素，将会等待条件变量
    bool pop(T &item)
    {
        m_mutex.lock();
        while(m_size <= 0)
        {
            //假如已经等到一个条件变量（即队列元素），但是阻塞队列中B线程比本线程更快拿走，那么实际上这里的队列元素是没有增加的
            //所以线程唤醒后还要判断是否真的拿到了该条件变量，如果不是的话就继续等待另一个条件变量
            //所以上面这里循环必须用while而不能用if
            if(!m_cond.wait(m_mutex.get())) //阻塞等待一个条件变量，发生错误则返回
            {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //增加超时处理
    //在pthread_cond_wait基础上增加了等待的时间，只指定时间内能抢到互斥锁即可
    bool pop(T &item,int ms_timeout)
    {
        timespec t = {0,0};
        timeval now = {0,0};
        gettimeofday(&now, NULL);       //gettimeofday返回自1970-01-01 00:00:00到现在经历的秒数。
        m_mutex.lock();
        if(m_size <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec =  (ms_timeout % 1000) * 1000;
            if(!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
            }
        }

        //道理同上,因为这里是限时等待，如果等待时间结束后，依旧没有获得条件变量，则返回
        if(m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front+1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;

    T *m_array;     //队列数组
    int m_size;     //队列长度
    int m_max_size; //队列最大长度
    int m_front;    //队头下标
    int m_back;     //队尾下标

};
#endif //BLOCK_QUEUE_H
