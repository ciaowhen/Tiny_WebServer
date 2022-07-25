//
// Created by ciaowhen on 2022/7/15.
// Log类功能实现
//

#include <iostream>
#include <string>
#include <cstring>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <pthread.h>
#include "log.h"
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}
Log::~Log()
{
    if(m_fp != NULL)
        fclose(m_fp);
}

//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    //如果设置了max_queue_size则设置为异步写入方式
    if(max_queue_size >= 1)
    {
        m_is_async = true;      //异步标志
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数，这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;

    //输出内容的长度
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', sizeof(m_buf));
    
    m_split_lines = split_lines;    //日志最大行数

    //设置时间参数
    time_t t = time(NULL);            //返回1970-1-1, 00:00:00以来经过的秒数
    struct tm *sys_tm = localtime(&t);      //将时间数值变换成本地时间，考虑到本地时区和夏令时标志;
    struct tm my_tm = *sys_tm;

    //strrchr 函数在字符串 s 中是从后到前（或者称为从右向左）查找字符 c，找到字符 c 第一次出现的位置就返回，返回值指向这个位置。
    //从后往左找到第一个/的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256]{0};

    //相当于自定义日志名
    //若输入的文件名没有/，则直接将时间+文件名作为日志名
    if(p == NULL)
        snprintf(log_full_name,255,"%d_%02d_%02d_%s", my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,file_name);
    else
    {
        //将/的位置向后移动一个位置，然后复制到log_name中
        //因为file_name为起始地址，p - file_name + 1 是文件所在路径文件夹的长度
        //dirname相当于./
        strcpy(log_name, p+1);
        strncpy(dir_name, file_name, p - file_name+1);   //拷贝src字符串的前n个字符至dest
        //后面的参数跟format有关
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year+1900,my_tm.tm_mon+1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    
    m_fp = fopen(log_full_name, "a");       //打开日志文件并设置相应模式
    if(m_fp == NULL)
        return false;
    return true;
}
void Log::write_log(int level, const char *format, ...)
{
    struct timeval now{0,0};
    gettimeofday(&now, NULL);       //gettimeofday返回自1970-01-01 00:00:00到现在经历的秒数。
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);  //将时间数值变换成本地时间，考虑到本地时区和夏令时标志;
    struct tm my_tm = *sys_tm;

    char s[16]{0};
    switch (level)      //根据日志分级拼接字符串
    {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s,"[info]:");
            break;
    }

    //写入一个log，对m_count++， 注意m_split_lines最大行数
    m_mutex.lock();
    m_count++;

    //新的日期或者等于最大行数的倍数时需要再写入新的日志文件
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
    {
        char new_log[256]{0};
        fflush(m_fp);       //刷新文件中的缓冲区
        fclose(m_fp);       //关闭fopen打开的文件
        char tail[16]{0};

        //格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon+1, my_tm.tm_mday);if(m_today != my_tm.tm_mday)        //如果是新的日期，则创建新的日志文件，日志名需要更新新的日期
        {
            snprintf(new_log, 255, "%s%s%s", dir_name,  tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {   //如果不是新的日期，那只有等于最大行数的情况，此时需要创建新的日志文件，日志名需要更新版本号
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);    //将传入的format参数赋值给valst，便于格式化输出

    string log_str;
    m_mutex.lock();

    //写入内容格式化：时间+内容;
    //时间格式化
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year+1900, my_tm.tm_mon+1,my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    //内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数(不包含终止符)
    int m = vsnprintf(m_buf+n, m_log_buf_size - 1, format, valst);
    m_buf[n+m] = '\n';
    m_buf[n+m+1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    //若m_is_async为true表示异步
    //若异步，则将写好的日志信息加入阻塞队列，同步则加锁向文件中写
    if(m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

//刷新数据流缓冲区
void Log::flush(void )
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
