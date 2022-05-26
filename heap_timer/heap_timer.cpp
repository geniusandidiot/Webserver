#include <algorithm>
#include "heap_timer.h"
#include "../http/http_conn.h"
#include"../log/log.h"



void HeapTimer::pop()
{
    if (heap.empty())
        return;
    del(0);
}

void HeapTimer::del(int index)
{
    if (heap.empty() || index > (heap.size() - 1) || index < 0)
        return;
    std::swap(heap[index], heap[heap.size() - 1]);
    heap.pop_back();
    percolate_down(0);
}

void HeapTimer::adjust_timer(int index, time_t time_s)
{
    if (heap.empty() || index > (heap.size() - 1) || index < 0)
        return;
    heap[index].expire = time(NULL) + time_s;
    this->percolate_down(index);
}

void HeapTimer::percolate_down(int index)
{
    if (heap.empty() || index > (heap.size() - 1) || index < 0)
        return;
    int i = index;
    int j = i * 2 + 1;
    int size = heap.size() - 1;

    while (j < size)
    {
        if (((j + 1) < size) && (heap[j].expire > heap[j + 1].expire))
            j++;
        if (heap[j].expire > heap[i].expire)
            break;
        std::swap(heap[i], heap[j]);
        i = j;
        j = i * 2 + 1;
    }
}

void HeapTimer::percolate_up(int index)
{
    if (heap.empty() || index > (heap.size() - 1) || index < 0||heap.size()==1)
        return;
    int i = index;
    int j = (i - 1) / 2;
    while (i > 0)
    {
        if (heap[j].expire < heap[i].expire)
            break;
        std::swap(heap[i], heap[j]);
        i = j;
        j = (i - 1) / 2;
    }
}

void HeapTimer::add(client_node node)
{
    this->heap.push_back(node);
    this->percolate_up(heap.size() - 1);

}



// 寻找到时间的结点
void HeapTimer::tick()
{
    if (heap.empty())
        return;

    while (!heap.empty())
    {
        time_t cur = time(NULL);
        // 未到时间
        if (heap.front().expire > cur)
        {
            break;
        }

        if (heap.front().cb)
        {
            heap.front().cb(&heap.front());
        }
       this->pop();
    }
}

void HeapTimer::clear()
{
    heap.clear();
}

int HeapTimer::get_index(int sockfd)
{
   for(int i=0;i<heap.size();i++)
    {
        if(heap[i].sockfd==sockfd)
        return i;
    }
}

void HeapTimer::invoke(int index)
{
    heap[index].cb(&heap[index]);
}

void Sig::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}
//对文件描述符设置非阻塞
int Sig::setnonblocking(int fd)
{
    int o_option = fcntl(fd, F_GETFL);
    int n_option = o_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, n_option);
    return o_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Sig::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Sig::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Sig::addsig(int sig, void(handler)(int), bool restart )
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Sig::timer_handler(HeapTimer*m_timer)
{

    m_timer->tick();
    alarm(m_TIMESLOT);
}
void Sig::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}
int *Sig::u_pipefd = 0;
int Sig::u_epollfd = 0;
void cb_func(client_node *Node)
{
    epoll_ctl(Sig::u_epollfd, EPOLL_CTL_DEL, Node->sockfd, 0);
    assert(Node);
    close(Node->sockfd);
    http_conn::m_user_count--;
}