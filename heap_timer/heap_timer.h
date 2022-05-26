#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>
#include <queue>

#include "../log/log.h"

struct client_node
{
    sockaddr_in address;
    int sockfd;
    std::function<void(struct client_node *)> cb;
    time_t expire; // 定时器生效的绝对时间
};

class HeapTimer
{
public:
    HeapTimer()
    {
        heap.reserve(64);
    }
    ~HeapTimer()
    {
        clear();
    }
    void percolate_down(int index); // 对堆结点进行下滤
    void percolate_up(int index);   // 对堆结点进行上滤
    void add(client_node node);
    void adjust_timer(int index, time_t time);
    void del(int index);
    void pop();
    void tick();
    void clear();
    int get_index(int sockfd);
    void invoke(int index);

public:
    int m_close_log=0;

private:
    std::vector<client_node> heap;
};

class Sig
{
public:
    Sig(){};
    ~Sig(){};

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler(HeapTimer *m_timer);

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    static int u_epollfd;
    int m_TIMESLOT;
    int m_close_log=0;
};
void cb_func(client_node *Node);
#endif