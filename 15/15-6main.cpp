#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include "14-2locker.h"
#include "15-3threadpool.h"
#include "15-4http_conn.h"
#include <assert.h>


#define  MAX_FD 65536
#define  MAX_EVENT_NUMBER	10000

//http_conn* users = new http_conn[MAX_FD]; user数组是所有线程的共享数据，但是并没有加锁，容易造成两个线程处理修改同一个数据的情况，通过sleep函数可以个模拟
//虽然消息队列加了锁，可以保证消息队列的同步访问，如果碰到客户端一次请求没发完，下一次再发，并且上一次的消息还没处理完返回的情况，则会出错
//要处理上述问题的话，只需要在process函数里加一把锁，保证同时只有一个线程处理http_conn类就可以了。
//整体过程是，
// 1、创建一个线程池，主线程负责接受连接，接受到一个连接后，初始化user数组中的http_conn类，user数组可以说是传递了主线程与子线程之间的数据 本来就是在一个进程里，数据共享比较方便
// 2、连接建立后，客户端发送请求过来，则调用read函数一次读取全部数据，然后通过append函数往请求队列中插入一个http_conn类的指针，通过信号量，线程池里的一个线程会被唤醒收到消息
// 3、当一个线程取得消息后，会调用指针的process函数，process函数会调用process_read和process_write函数，process_write函数如果成功填充了返回的信息，则会在
//    相应的socket文件描述符上注册一个写事件，然后主函数里调用http_conn::write()函数完成信息的发送，完成一次请求。
// 4、如果read函数里一次没有接收到完整的请求，process函数会在process_read函数后直接返回，连接没有关闭，下次收到数据后选择一个线程接着处理
// 5、一个连接的请求可能会被不同的线程处理，所以只能处理无状态的连接

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char* info)
{
    printf("%s\n", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char* argv[])
{
    if (argc <=2)
    {
        printf("usage: %s ip port\n", argv[0]);
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    /*忽略SIGPIPE信号 在向已经收到RST的socket执行写操作时，内核会向进程发送SIGPIPE信号，告知进程连接对端已关闭
    SIGPIPE默认处理方式是终止进程 所以需要对SIGPIPE信号进行处理*/
    addsig(SIGPIPE, SIG_IGN);

    /*创建线程池*/
    threadpool<http_conn>* pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch (...)
    {
        printf("new threadpoll failure\n");
        return 1;
    }

    /*预先为每个可能的客户连接分配一个http_conn对象*/
    http_conn* users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    /*SO_LINGER决定close行为，具体看这里：https://blog.csdn.net/qq_20363225/article/details/122352713?spm=1001.2014.3001.5501*/
    struct linger tmp = {1, 0};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);
    ret = listen(listenfd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while (1)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(sockfd, (struct sockaddr*)&client_address, &client_addrlen);
                if (connfd < 0)
                {
                    printf("accept failure errnor %d\n", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal Server busy");
                    continue;
                }
                /*初始化客户链接*/
                users[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                /*有异常，直接关闭客户端*/
                users[sockfd].close_conn();
            }
            else if (events[i].events & EPOLLIN)
            {
                /*根据读的结果决定是将任务添加到线程池还是关闭连接*/
                if (users[sockfd].read())
                {
                    /*也可以这样写：&(uses[sockfd])*/
                    //上面这样写不行，因为append里不仅往user数组里加东西了，还添加了信号量
                    pool->append(users + sockfd);
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                /*根据写的结果，决定是否关闭连接*/
                if (!users[sockfd].write())
                {
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;
    return 0;
}
