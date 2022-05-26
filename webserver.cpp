#include "webserver.h"

using namespace std;

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);
}
WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write)
        {
            if (!Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800))
                throw std::exception(); 
        }

        else
        {

            if (!Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0))
                throw std::exception();
        }
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new ThreadPool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //优雅关闭连接
    if (m_OPT_LINGER == 0)
    {
        struct linger tmp = {0, 0};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (m_OPT_LINGER == 1)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    // 监听所有地址
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    m_sig.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    m_sig.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    m_sig.setnonblocking(m_pipefd[1]);
    m_sig.addfd(m_epollfd, m_pipefd[0], false, 0);

    //工具类,信号和描述符基础操作
    Sig::u_pipefd = m_pipefd;
    Sig::u_epollfd = m_epollfd;

    m_sig.addsig(SIGPIPE, SIG_IGN);
    m_sig.addsig(SIGALRM, m_sig.sig_handler, false);
    m_sig.addsig(SIGTERM, m_sig.sig_handler, false);

    alarm(TIMESLOT);
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    client_node node;
    time_t cur = time(NULL);
    node.expire = cur + 3 * TIMESLOT;
    node.address = client_address;
    node.sockfd = connfd;
    node.cb = cb_func;
    m_timer.add(node);

}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(int sockfd)
{

    time_t new_time = time(NULL) + 3 * TIMESLOT;
    m_timer.adjust_timer(m_timer.get_index(sockfd), new_time);
    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(int sockfd)
{
    int i = m_timer.get_index(sockfd);
    m_timer.invoke(i);
    m_timer.del(i);
    LOG_INFO("close fd %d", sockfd);
}

bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlen = sizeof(client_address);
    if (m_LISTENTrigmode == 0)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlen);
        if (connfd < 0)
        {
            LOG_ERROR("%s:%d", "Accept Error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            m_sig.show_error(connfd, "Internal Server Busy!");
            LOG_ERROR("%s", "Internal Server Busy");
            return false;
        }
        timer(connfd, client_address);

    }
    else
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlen);
            if (connfd < 0)
            {
                LOG_ERROR("%s:%d", "Accept Error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                m_sig.show_error(connfd, "Internal Server Busy!");
                LOG_ERROR("%s", "Internal Server Busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
        
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    //reactor
    if (m_actormodel == 1)
    {
        adjust_timer(sockfd);

        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (1)
        {
            if (users[sockfd].improv == 1)
            {
                if (users[sockfd].timer_flag == 1)
                {
                    deal_timer(sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].read_once())
        {
            LOG_INFO("Deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);
            adjust_timer(sockfd);

        }
        else
        {
            deal_timer(sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    //reactor
    if (m_actormodel == 1)
    {
        adjust_timer(sockfd);

        m_pool->append(users + sockfd, 1);

        while (1)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("Send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            adjust_timer(sockfd);
        }
        else
        {
            deal_timer(sockfd);
        }
    }
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)    
    {
        LOG_INFO("%s","NOtice");
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        LOG_INFO("%s---%d","NOtice!!!!",number);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "Epoll Failure!");
            break;
        }
        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
                     LOG_INFO("%d-%d",sockfd,m_listenfd);
            // 处理新来到的连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclientdata();
                if (!flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应定时器
                deal_timer(sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (flag == false)
                    LOG_ERROR("%s", "Dealclientdata Failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                LOG_INFO("%s","thread");
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                LOG_INFO("%s","write");
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            m_sig.timer_handler(&m_timer);

            LOG_INFO("%s", "Timer Tick!");

            timeout = false;
        }
    }
}