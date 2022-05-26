#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGIMysql/sql_connection_pool.h"

template <typename T>
class ThreadPool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    ThreadPool(int model, connection_pool *pool, int thread_num = 8, int max_req = 10000);
    ~ThreadPool();

    //像请求队列中插入任务请求
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_num;            //线程池中的线程数
    int m_max_req;               //请求队列中允许的最大请求数
    pthread_t *m_threads;        //描述线程池的数组，其大小为m_thread_num
    std::list<T *> m_workqueue;  //请求队列
    locker m_queuelocker;        //保护请求队列的互斥锁
    sem m_queuestat;             //是否有任务需要处理
    connection_pool *m_connPool; //数据库
    int m_model;                 //模型切换
    int m_close_log=0;
};
template <typename T>
ThreadPool<T>::ThreadPool(int model, connection_pool *pool, int thread_num, int max_req) : m_model(model), m_thread_num(thread_num), m_max_req(max_req), m_threads(NULL), m_connPool(pool)
{
    if (thread_num <= 0 || max_req <= 0)
        throw std::exception();
    m_threads = new pthread_t[thread_num];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_num; ++i)
    {
        //循环创建线程，并将工作线程按要求进行运行
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        //将线程进行分离后，不用单独对工作线程进行回收
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
    delete[] m_threads;
}

template <typename T>
bool ThreadPool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    //根据硬件，预先设置请求队列的最大值
    if (m_workqueue.size() >= m_max_req)
    {
        m_queuelocker.unlock();
        return false;
    }
    //添加任务
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
bool ThreadPool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_req)
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
void *ThreadPool<T>::worker(void *arg)
{
    //将参数强转为线程池类，调用成员方法
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void ThreadPool<T>::run() //工作线程从请求队列中取出某个任务进行处理
{
    while (true)
    {
        //信号量等待
        m_queuestat.wait();

        //被唤醒后先加互斥锁
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        if (m_model == 1)
        {
            if (request->m_state == 0)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    //从连接池中取出一个数据库连接
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif