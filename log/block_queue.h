#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <deque>
#include <sys/time.h>
#include "../lock/locker.h"

using namespace std;

template <class T>
class block_queue
{
public:
    
    block_queue(int max_size = 1000)
    {
        if(max_size<=0)
        exit(-1);
    }

    ~block_queue()
    {
    }

    void clear()
    {
        m_mutex.lock();
        m_deque.clear();
        m_mutex.unlock();
    }

    //判断队列是否满了
    bool full() 
    {
        m_mutex.lock();
        if (m_deque.size() >= m_max_size)
        {

            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    //判断队列是否为空
    bool empty() 
    {
        m_mutex.lock();
        if (m_deque.empty())
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

       //返回队首元素
    bool front(T &value) 
    {
        m_mutex.lock();
        if (this->empty())
        {
            m_mutex.unlock();
            return false;
        }
        value = m_deque.front();
        m_mutex.unlock();
        return true;
    }
    //返回队尾元素
    bool back(T &value) 
    {
        m_mutex.lock();
        if (this->empty())
        {
            m_mutex.unlock();
            return false;
        }
        value = m_deque.back();
        m_mutex.unlock();
        return true;
    }

    int size() 
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_deque.size();

        m_mutex.unlock();
        return tmp;
    }

    int max_size()
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;
    }

    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item)
    {

        m_mutex.lock();
        if (m_deque.size() >= m_max_size)
        {

            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_deque.push_back(item);

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item)
    {

        m_mutex.lock();
        while (m_deque.size() <= 0)
        {
            
            if (!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }

        item=m_deque.front();
        m_deque.pop_front();
        m_mutex.unlock();
        return true;
    }

        //增加了超时处理
    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_deque.size() <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_deque.size() <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        item=m_deque.front();
        m_deque.pop();
        m_mutex.unlock();
        return true;
    }
    
private:
    locker m_mutex;
    cond m_cond;
    int m_max_size;
    deque<T> m_deque;
};

#endif