#include"http_connection.h"

//静态变量一定要初始化
int http_connection::m_epoll_fd = -1;
int http_connection::m_user_count =0;

//网站的根目录(项目的根路径)
const char* doc_root = "/home/hua/Myproject/5_project/resources";

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//设置文件描述符非阻塞
void setnonblocking(int fd){
    int old_flag = fcntl(fd , F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL , new_flag);
}


// 向epoll中添加需要监听的文件描述符
void addfd(int epoll_fd, int fd , bool one_shot){
    struct epoll_event event;
    event.data.fd = fd ;
    event.events = EPOLLIN |EPOLLET |  EPOLLRDHUP ;        //水平模式或边缘模式

    if(one_shot)
    {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD , fd , &event);       //注册事件 注册你想要检测有无变化的文件描述符
    
    //设置文件描述符非阻塞
    setnonblocking(fd);
}   

// 从epoll中移除需要监听的文件描述符
void removefd (int epoll_fd , int fd){
    epoll_ctl(epoll_fd , EPOLL_CTL_DEL , fd , 0);
    close(fd);
} 

// 修改epoll文件描述符
void modifyfd(int epoll_fd , int fd , int ev){
    struct epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;      //修改事件的值ev | EPOLLONESHOT | EPOLLRDHUP
    epoll_ctl(epoll_fd , EPOLL_CTL_MOD , fd , &event);
}

// 初始化新接收的连接 ,在客户端数组users中初始化每一个新连接的客户端信息
void http_connection::init(int sockfd , const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;

    //设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd , SOL_SOCKET , SO_REUSEADDR, &reuse , sizeof(reuse));
    //添加到epoll对象中
    addfd(m_epoll_fd , sockfd , true);
    m_user_count++;     //总用户数+1

    init();
}

//异常事件 关闭连接
void http_connection::close_conection(){
    if(m_sockfd != -1)
    {
        removefd(m_epoll_fd, m_sockfd);
        m_sockfd = -1;      //置为-1就没有用了
        m_user_count--;     //关闭一个连接，客户总数量-1
    }
}
 
// 初始化相关连接
void http_connection::init(){
    
    int bytes_to_send = 0;    //将要发送的字节数
    int bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;            //初始状态为 解析请求首行
    m_checked_index = 0;
    m_start_line = 0 ;
    m_read_index = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;

    m_linger = false;

    bzero(m_read_buf , READ_BUFFER_SIZE );

}









void http_connection::process(){        
    
    //前面main函数已经读到了所有数据，append连接对象进请求队列当中，子线程拿到对应的对象进来此函数，先进行请求解析。
    HTTP_CODE read_ret =  process_read();     

    if(read_ret == NO_REQUEST)
    {
        modifyfd(m_epoll_fd , m_sockfd ,EPOLLIN);
        return;
    }


    //生成响应
    bool write_ret = process_write(read_ret);   //响应要根据你读到的结果去响应
    if(!write_ret)
    {   
        close_conection();
    }
    modifyfd(m_epoll_fd , m_sockfd ,EPOLLOUT);  //写入完成后更改out事件，向客户端回写
} 


//  一次性把所有数据都读出来 proactor模式  非阻塞 (读出来的数据就是完整的http服务器的请求报文信息) 
bool http_connection::read(){
    if(m_read_index >= READ_BUFFER_SIZE)
        return false;
    int read_bytes = 0; //读到的字节
    while(true)
    {
        read_bytes = recv(m_sockfd , &m_read_buf + m_read_index , READ_BUFFER_SIZE - m_read_index , 0); //从传进来的m_sockfd里读 (每一个客户端的fd都不一样)
        
        if(read_bytes == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)     //表示没有数据 非阻塞读会有两个错误
            {
                break;
            }    
            return false;
        }
        else if(read_bytes == 0)    //对方关闭 客户端关闭连接
        {
            return false;
        }

        m_read_index += read_bytes;

    }
    printf("reading data : %s\n" , m_read_buf);      //读取到的数据保存在该连接对象的 m_read_buf缓冲区里。
    return true;
}


// 主状态机 解析HTTP请求
http_connection::HTTP_CODE http_connection::process_read(){     
    
    LINE_STATUS line_status  = LINE_OK;
    HTTP_CODE res = NO_REQUEST;

    char *text = 0; 

    //while循环的目的是： 需要一行一行的去解析 碰到\r\n代表解析完了一行数据  最终结果的读取到一行数据 
    while(( (m_check_state == CHECK_STATE_CONTENT)&& (line_status == LINE_OK) )|| ((line_status = parse_line()) == LINE_OK))  
    {   
        //解析到了一行完整的数据,或者解析到了请求体，请求体代表已经到最底部了，也是完整的数据    

        text = get_line();     //获取到一行数据就交给函数去解析
        m_start_line = m_checked_index;
        printf("got 1 line for HTTP : %s\n" , text);

        switch (m_check_state)      //根据 m_check_state当前状态
        {
            case CHECK_STATE_REQUESTLINE:   //请求首行
            {
                res = parse_request_line(text);        
                if(res == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:                //请求头
            {
                res = parse_headers(text);
                if(res == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if(res == GET_REQUEST)
                {
                    return do_request();           
                }
            }
            case CHECK_STATE_CONTENT:           //请求体
            {
                res = parse_content(text);
                if(res == GET_REQUEST)
                {
                    return do_request();        
                }
                line_status = LINE_OPEN;    //如果失败了 就把状态改成 行数据不完整状态
                break;
            }
        
            default:
            {
                return INTERNAL_ERROR;
            }

        }
    }
    return NO_REQUEST;      //整个主状态机的状态

}





//得到一个完整的正确的HTTP请求时,分析目标文件的属性(m_url), 
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，
// 并告诉调用者获取文件成功，我们得到了m_url,然后要在服务器找到这个资源，把数据写给客户端
//其实这个函数的用意就是你完整的解析完了客户端的请求，需要do_request进行响应客户端
http_connection::HTTP_CODE http_connection::do_request(){   
    
    strcpy( m_real_file, doc_root );    // doc_root 就是资源的路径"/home/hua/Myproject/5_project/resources" ，函数目的是把doc_root拷贝到 m_real_file
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );  //再把获取到的m_url拼接上面的路径即会得到 "/home/hua/Myproject/5_project/resources/index.html"
    

    //1 拼接完成后就是  "/home/hua/Myproject/5_project/resources/index.html"
    if ( stat( m_real_file, &m_file_stat ) < 0 )      // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    {   
        return NO_RESOURCE;
    }

    if ( ! ( m_file_stat.st_mode & S_IROTH ) )  // 判断访问权限
    {
        return FORBIDDEN_REQUEST;
    }

    if ( S_ISDIR( m_file_stat.st_mode ) )       // 判断是否是目录
    {
        return BAD_REQUEST;
    }

    //2 用open函数打开拼接好的路径的文件 (该文件是存储在服务器上的， 是客户端请求的文件) ，拿到该文件的FD
    int fd = open( m_real_file, O_RDONLY );
    
    //3 创建内存映射，第四个参数传入拿到的FD，文件映射到内存后会返回一个地址，地址就是服务器需要响应回去的资源 ，这个地址是预备给响应请求时回写数据用到的。
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );    //把我们要发送的数据利用mmap内存映射函数映射到内存中，用 m_file_adress接收
    close( fd );
    return FILE_REQUEST;
}




// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_connection::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );        
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );            //生成响应首行   
            add_headers( strlen( error_404_form ) );            //响应头
            if ( ! add_content( error_404_form ) ) {            //响应体
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:                                          //////最后成功响应的报文 全部数据！！！ 
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;

            /*补充一下，通过使用 iovec 结构和 m_iv 数组，可以在一次操作中同时传递多个缓冲区，
            从而减少了多次读写的开销，提高了性能。在这段代码中，m_iv 数组的使用是为了将响应内容和
            文件内容同时发送给客户端。*/
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}


//  一次性把所有数据都写好 proactor模式   非阻塞 写HTTP响应
bool http_connection::write()
{
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modifyfd( m_epoll_fd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
   
        temp = writev(m_sockfd, m_iv, m_iv_count);           // 分散写
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modifyfd( m_epoll_fd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }
        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            unmap();
            modifyfd(m_epoll_fd, m_sockfd, EPOLLIN);
            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            } 
        }
    }
}







///////////////////////////////////////解析HTTP请求几个小函数

//  解析一行，判断的依据是 \r\n  把所有数据当成是一个数组（包含请求行请求头）
http_connection::LINE_STATUS http_connection::parse_line(){     
    char cur;
    for(; m_checked_index < m_read_index ; m_checked_index++)
    {
        cur = m_read_buf[m_checked_index];
        if( cur == '\r')
        {
            if( (m_checked_index + 1)  == m_read_index)
            {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_index +1] == '\n')
            {   
    
                m_read_buf[m_checked_index++] = '\0';           
                m_read_buf[m_checked_index++] ='\0';                    //   GET / HTTP/1.1\r\n  ->  GET / HTTP/1.1\0\0 
                return LINE_OK;     //表示完整的解析到了一行
            }
            return LINE_BAD;
        }
        else if(cur == '\n')
        {
            if( (m_checked_index > 1) && (m_read_buf[m_checked_index-1] == '\r') )
            {
                m_read_buf[m_checked_index -1 ] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }

        //return LINE_OPEN;
    }
    return LINE_OK;
}

// 解析HTTP请求行,获取请求方法,目标URL,HTTP版本 主要是要得到三个信息
http_connection::HTTP_CODE http_connection::parse_request_line(char *text){     
    
    // GET /index.html HTTP/1.1     现在是已经获取到了这一行数据，接下来要做的就是把这一行数据拆分(解析出来)。

    m_url = strpbrk(text , " \t");        //检测 空格+\t 在哪个地方先出现 ，下标赋给m_url 因此 m_url字符串就是   _/index.html HTTP/1.1
    
    //1 获取方法
    *m_url ++ = '\0';       // GET\0/index.html HTTP/1.1
 
    char * method = text;   //此时的 text就是      GET\0/index.html HTTP/1.1
    if(strcasecmp(method , "GET") == 0) //判断method是不是 GET方法
    {
        m_method = GET;
    }
    else
    { 
        return BAD_REQUEST;
    }

    //2 先获取第三个信息 版本： HTTP/1.1
    m_version = strpbrk(m_url , " \t"); //为什么传入m_url 因为现在的m_url代表的是 _/index.html HTTP/1.1 , m_version就指向了 HTTP/1.1
    if( !m_version)
    {
        return BAD_REQUEST;
    }
    *m_version ++  = '\0';       //  _/index.html\0HTTP/1.1

    /*
    if(strcasecmp(m_version, "HTTP/1.0") != 0)
    {
        return BAD_REQUEST;
    }
    */
    // http://192.168.12.1:10000/index.html
    if(strncasecmp(m_url , "http://" , 7) == 0)
    {
        m_url += 7; //  192.168.12.1:10000/index.html
        m_url = strchr(m_url , '/');  //  完了后 m_url代表的就是 ： /index.html 
    }

    if(! m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;     //主状态机检查状态变成检查请求头 因为你请求行已经解析完了 所以check_state状态要发生改变  检查状态编程检查请求头

    return NO_REQUEST;
}
 
// 解析请求头
http_connection::HTTP_CODE http_connection::parse_headers(char *text){     
    
    if( text[0] == '\0' )  // 遇到空行，表示头部字段解析完毕
    {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) 
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } 
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) 
    {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) 
        {
            m_linger = true;
        }
    } 
    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) 
    {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } 
    else if ( strncasecmp( text, "Host:", 5 ) == 0 ) 
    {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } 
    else 
    {
        printf( "oop! unknow header %s\n", text );
    }

    return NO_REQUEST;
}
 
// 解析请求体 本项目没有真正去解析请求体  只是简单的判断有无完整的被读入
http_connection::HTTP_CODE http_connection::parse_content(char *text){     
    if(m_read_index >= (m_content_length + m_checked_index))
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}



///////////////////////////////////////响应HTTP请求几个小函数

//响应首行
bool http_connection::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

// 往写缓冲中写入待发送的数据  写到 m_write_buf 数组里
bool http_connection::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_connection::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_connection::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_connection::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_connection::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_connection::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_connection::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

//  用到了内存映射函数 ，就要释放内存映射，此函数专门用于释放内存映射 munmap()操作 
void http_connection::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}





