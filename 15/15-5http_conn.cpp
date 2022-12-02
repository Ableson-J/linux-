#include "15-4http_conn.h"

/*定义http响应的一些状态信息*/
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

/*网站根目录*/
const char* doc_root = "/home/jiang/net_program/codes/15";

int setnonblocking(int fd)
{
    int oldopt = fcntl(fd, F_GETFL);
    int newopt = oldopt | O_NONBLOCK;
    fcntl(fd, F_SETFL, newopt);
    return oldopt;
}

void addfd(int epollfd, int fd, bool one_shot = false)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;		// EPOLLRDHUP事件是TCP连接被对方关闭，或者对方关闭了写操作
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    //这里是不是应该把epollin去掉?
    event.events = ev | EPOLLIN | EPOLLET | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//类外初始化静态成员
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close)
{
    if (real_close && m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

/*public成员 接收到新连接时调用*/
void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    /*下面两行是为了避免TIME_WAIT状态，仅用于调试，实际使用时应该去掉*/
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    //添加到epoll中进行监听
    addfd(m_epollfd, m_sockfd);
    m_user_count++;

    init();
}

/*private成员 由public的init调用*/
void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/*从状态机
用于解析出一行内容 一个完整的行结尾必是一个回车符+一个换行符，即"\r\n"
解析一行就是将一行的末尾的\r\n替换为\0，方便后续处理
 */
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }

        if (temp == '\n')
        {
            /*这种情况对应第一个if中LINE_OPEN的情况*/
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    return LINE_OPEN;
}

/*循环读取数据，直到无数据可读或对方关闭连接*/
bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytes_read = 0;
    while (1)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 无数据可读了
                break;
            }
            // recv出错了
            return false;
        }
        if (bytes_read == 0)
        {
            // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

/*解析http请求行 获得请求方法、目标URL,以及http版本号
一个正常的http请求行示例："GET http://www.xxx.xx/xx HTTP/1.1"*/
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    /*c库函数 strpbrk 检索temp中第一个出现" \t"中字符的字符，
    返回指向检索到的哪个字符的指针本例中是检索temp中第一次出现空格或者'\t'的位置
    如temp为"abc def",返回的url将指向c后面的空格*/
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char* method = text;
//    printf("thread %d sleep\n", pthread_self());
//    sleep(20);
    /*strcasecmp忽略大小写比较字符串*/
    if (strcasecmp(method, "GET") != 0)
    {
        return BAD_REQUEST;
    }
    m_method = GET;
    /*C 库函数 size_t strspn(const char *str1, const char *str2) 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    这一步过滤多余的空格和'\t'，确保url指向'h'*/
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        /*C 库函数 char* strchr(const char* str, int c) 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置*/
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    /*状态转换*/
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/*解析http请求的一个头部信息*/
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    /*遇到空行，表示头部字段解析完毕*/
    if (text[0] == '\0')
    {
        /*如果请求有消息体，则还需要读取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONTENT状态*/
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        /*否则说明已经得到了一个完整的http请求*/
        return GET_REQUEST;
    }

    /*处理connection头部字段*/
    if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            /*长连接*/
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        printf("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}

/*我们没有真正解析http请求的消息体，只是判断它是否被完整读入了*/
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/*主状态机
从buffer中读出所有的完整的行*/
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)
           || (line_status = parse_line()) == LINE_OK)
    {
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line: %s\n", text);

        switch (m_check_state)
        {
            case http_conn::CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
                break;
            case http_conn::CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                if (ret == GET_REQUEST)
                {
                    return do_request();
                }
                break;
            }
                break;
            case http_conn::CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST)
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
                break;
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

/*当得到一个完整、正确的http请求时就分析目标文件的属性。如果目标文件存在、对所有用户可读，且不是目录，
则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功*/
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //文件路径加文件名
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        printf("real file:%s\n", m_real_file);
        return NO_RESOURCE;
    }
    /*S_IRUSR：用户读权限
    S_IWUSR：用户写权限
    S_IRGRP：用户组读权限
    S_IWGRP：用户组写权限
    S_IROTH：其他组读权限
    S_IWOTH：其他组写权限*/
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    if (S_ISDIR(m_file_stat.st_mode))
    {
        /*请求的是个目录*/
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    /*mmap看这里：https://blog.csdn.net/qq_20363225/article/details/121653263?spm=1001.2014.3001.5501*/
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/*对内存映射区执行munmap*/
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/*写http响应*/
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    /*需要发送的字节长度*/
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            /*如果tcp写缓冲没有空间，则等待下一轮epollout事件，虽然在此期间，
            服务器无法立即接收到同一客户的下一个请求（因为EPOLLONESHOT）。但这可以保证连接的完整性*/
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            /*出错*/
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        /*为什么这里就发完了？*/
        if (bytes_to_send <= bytes_have_send)
        {
            /*发送http响应成功，根据http请求中的connection字段决定是否立即关闭连接*/
            unmap();
            if (m_linger)
            {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else
            {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

/*往写缓冲区写入待发送的数据*/
bool http_conn::add_response(const char* format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    /*VA_LIST 是在C语言中解决变参问题的一组宏，所在头文件：#include <stdarg.h>，用于获取不确定个数的参数。*/
    va_list arg_list;
    /*VA_START宏，获取可变参数列表的第一个参数的地址（ap是类型为va_list的指针，v是可变参数最左边的参数）
    #define va_start(ap, v) (ap = (va_list)&v + _INTSIZEOF(v))*/
    va_start(arg_list, format);
    /*vsnprintf 将可变参数格式化输出到字符数组*/
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        return false;
    }
    m_write_idx += len;
    /*va_end清空va_list可变参数列表*/
    va_end(arg_list);
    return true;
}

/*请求行*/
bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/*头部信息*/
bool http_conn::add_headers(int content_length)
{
    add_content_length(content_length);
    add_linger();
    add_blank_line();
    return true;
}

/*长度*/
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

/*Connection信息 长连接或短连接*/
bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}

/*头部信息结束后的空行*/
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

/*内容*/
bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

/*根据服务器处理http请求的结果，决定返回给客户端的内容*/
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        case http_conn::BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form))
            {
                return false;
            }
        }
            break;
        case http_conn::NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
            {
                return false;
            }
        }
            break;
        case http_conn::FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
            {
                return false;
            }
        }
            break;
        case http_conn::INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
            {
                return false;
            }
        }
            break;
        case http_conn::FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else
            {
                const char* ok_string = "<html><body>Hello World!</body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                {
                    return false;
                }
            }
        }
            break;
        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

/*由线程池中的工作线程调用，这是处理http请求的入口函数*/
void http_conn::process()
{
    //后来加上的
    data_locker.lock();
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        //如果一次请求不完整，直接返回，没有给客户端发送任何信息，但是连接的socket fd还没有关闭，user数组中的http_conn类保留了上一个线程处理后的信息，
        //等客户端把剩下的数据发送过来后，也许另一个线程获得消息，接着处理就是了，这就是无状态吗？
        //有没有可能上一个线程还没处理完不完整的请求，客户端接下来的请求又到了，另一个线程开始处理，这样的话岂不是两个线程同时修改http_conn了，明天加一个sleep函数试一下。
        //
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        printf("request not complete\n");
        data_locker.unlock();
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
    data_locker.unlock();
}
