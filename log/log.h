//
// Created by ciaowhen on 2022/7/15.
//

#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <stdio.h>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"
using namespace std;

class Log
{
public:
    //C++11后，使用局部变量懒汉不用加锁,C++0X以后，要求编译器保证内部静态变量的线程安全性，故C++0x之后该实现是线程安全的
    static Log *get_instance()
    {
        //静态函数只能调用静态变量
        static Log instance;
        return &instance;
    }
    //异步写日志公有方法，调用私有方法async_write_log
    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_wirte_log();
    }
    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192,int split_lines = 5000000, int max_queue_size = 0);
    //将输出内容按照标准格式整理
    void write_log(int level, const char *format, ...);
    //强制刷新缓冲区
    void flush(void);

private:
    Log();
    virtual ~Log();
    void *async_wirte_log()
    {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while(m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            //c_str()函数返回一个指向正规C字符串的指针常量, 内容与本string串相同
            //fputs把字符串写入到指定的流 stream 中，但不包括空字符。
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
private:
    char dir_name[128];         //路径名
    char log_name[128];         //log文件名
    int m_split_lines;          //日志最大行数
    int m_log_buf_size;         //日志缓冲区大小
    long long m_count;          //日志行数记录
    int m_today;                //按天分文件，记录当前时间是哪一天
    FILE *m_fp;                 //打开log的文件指针
    char *m_buf;                 //要输出的内容
    block_queue<string> *m_log_queue;   //日志的阻塞队列
    bool m_is_async;            //是否同步标志位
    locker m_mutex;             //同步类
    int m_close_log;            //关闭日志
};

//这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
#define LOG_DEBUG(format,...) if(0 == m_clos_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format,...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif //LOG_H
