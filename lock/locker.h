//
// Created by ciaowhen on 2022/7/15.
//

#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

//均采用RAII机制
//创建信号量类
class sem
{
public:
    sem()
    {
        if(sem_init(&m_sem, 0, 0) != 0)     //初始化信号量，初值为0
            throw std::exception();
    }
    sem(int num)
    {
        if(sem_init(&m_sem, 0, num) != 0)        //初始化信号量，初值为num
            throw std::exception();
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

//创建互斥量类
class locker
{
public:
    locker()
    {
        if(pthread_mutex_init(&m_mutex, NULL) != 0)     //初始化互斥量
            throw std::exception();
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);            //销毁互斥量
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;   //加锁
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;     //解锁
    }
    pthread_mutex_t *get()
    {
        return &m_mutex;        //获取互斥锁
    }

private:
    pthread_mutex_t m_mutex;
};

//创建条件变量类
class cond
{
public:
    cond()
    {
        if(pthread_cond_init(&m_cond,NULL) != 0)        //初始化条件变量
            throw std::exception();
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);      //阻塞等待一个条件变量
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);     //限时等待一个条件变量
        return ret == 0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;       //唤醒至少一个阻塞在条件上变量的线程
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;    //唤醒全部阻塞在条件上变量的线程
    }

private:
    pthread_cond_t m_cond;
};


#endif //LOCKER_H
