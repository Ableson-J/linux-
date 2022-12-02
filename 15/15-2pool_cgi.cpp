#include "15-1processpool.h"
/*用于处理客户CGI请求，它可以作为进程池类的模板参数*/
class cgi_conn
{
public:
    cgi_conn(){}
    ~cgi_conn(){}
    /*初始化客户端连接，情况缓冲区*/
    void init(int epollfd, int sockfd, const sockaddr_in& client_addr)
    {
        m_epolled = epollfd;
        m_sockfd = sockfd;
        m_address = client_addr;
        memset(m_buf, '\0', sizeof(m_buf));
        m_read_idx = 0;
    }

    /*处理客户请求*/
    void process()
    {
        int idx = 0;
        int ret = -1;
        while (1)
        {
            idx = m_read_idx;
            ret = recv(m_sockfd, m_buf + idx, BUFFER_SIZE - idx - 1, 0);
            /*如果是读操作发送错误，则关闭连接，如果是暂时无数据可读，则退出循环*/
            if (ret < 0)
            {
                if (errno != EAGAIN)
                {
                    /*读错误*/
                    removefd(m_epolled, m_sockfd);
                }
                break;
            }
                /*若对方关闭连接，则服务器也关闭*/
            else if (ret == 0)
            {
                removefd(m_epolled, m_sockfd);
                break;
            }
            else
            {
                m_read_idx += ret;
                printf("user content is: %s\n", m_buf);
                /*如果遇到字符"\r\n"则开始处理请求*/
                for (; idx < m_read_idx; ++idx)
                {
                    if (idx >= 1 && (m_buf[idx - 1] == '\r') && (m_buf[idx] == '\n'))
                    {
                        printf("start\n");
                        break;
                    }
                }
                /*如果没有遇到“\r\n”则需要读取更多客户数据*/
                if (idx == m_read_idx)
                {
                    printf("more data\n");
                    continue;
                }
                m_buf[idx - 1] = '\0';
                char* file_name = m_buf;
                /*判断客户要执行的CGI程序是否存在*/
                if (access(file_name, F_OK) == -1)
                {
                    removefd(m_epolled, m_sockfd);
                    break;
                }
                /*创建子进程来执行CGI程序*/
                ret = fork();
                if (ret == -1)
                {
                    removefd(m_epolled, m_sockfd);
                    break;
                }
                else if (ret > 0)
                {
                    /*父进程中关闭连接*/
                    removefd(m_epolled, m_sockfd);
                    break;
                }
                else
                {
                    /*子进程将标准输出定向到m_sockfd,并执行CGI程序*/
                    close(STDOUT_FILENO);
                    dup(m_sockfd);
                    printf("start1\n");
                    execl(m_buf, m_buf, 0);
                    exit(0);
                }
            }
        }
    }

private:
    /*读缓冲区大小*/
    static const int BUFFER_SIZE = 1024;
    static int m_epolled;
    int m_sockfd;
    sockaddr_in m_address;
    char m_buf[BUFFER_SIZE];
    /*标记读缓冲区中已经读入的客户数据的最后一个字节的下一个位置*/
    int m_read_idx;
};

int cgi_conn::m_epolled = -1;

int main(int argc, char* argv[])
{
    if (argc <= 2)
    {
        printf("usage: %s ip port\n", argv[0]);
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);
    ret = listen(listenfd, 5);
    assert(ret != -1);

    processpool<cgi_conn>* pool = processpool<cgi_conn>::create(listenfd, 1);
    if (pool)
    {
        pool->run();
        delete pool;
    }
    close(listenfd);
    return 0;
}
