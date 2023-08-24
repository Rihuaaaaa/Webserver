#include"http_connection.h"

//��̬����һ��Ҫ��ʼ��
int http_connection::m_epoll_fd = -1;
int http_connection::m_user_count =0;

//��վ�ĸ�Ŀ¼(��Ŀ�ĸ�·��)
const char* doc_root = "/home/hua/Myproject/5_project/resources";

// ����HTTP��Ӧ��һЩ״̬��Ϣ
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//�����ļ�������������
void setnonblocking(int fd){
    int old_flag = fcntl(fd , F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL , new_flag);
}


// ��epoll�������Ҫ�������ļ�������
void addfd(int epoll_fd, int fd , bool one_shot){
    struct epoll_event event;
    event.data.fd = fd ;
    event.events = EPOLLIN |EPOLLET |  EPOLLRDHUP ;        //ˮƽģʽ���Եģʽ

    if(one_shot)
    {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD , fd , &event);       //ע���¼� ע������Ҫ������ޱ仯���ļ�������
    
    //�����ļ�������������
    setnonblocking(fd);
}   

// ��epoll���Ƴ���Ҫ�������ļ�������
void removefd (int epoll_fd , int fd){
    epoll_ctl(epoll_fd , EPOLL_CTL_DEL , fd , 0);
    close(fd);
} 

// �޸�epoll�ļ�������
void modifyfd(int epoll_fd , int fd , int ev){
    struct epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;      //�޸��¼���ֵev | EPOLLONESHOT | EPOLLRDHUP
    epoll_ctl(epoll_fd , EPOLL_CTL_MOD , fd , &event);
}

// ��ʼ���½��յ����� ,�ڿͻ�������users�г�ʼ��ÿһ�������ӵĿͻ�����Ϣ
void http_connection::init(int sockfd , const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;

    //���ö˿ڸ���
    int reuse = 1;
    setsockopt(m_sockfd , SOL_SOCKET , SO_REUSEADDR, &reuse , sizeof(reuse));
    //��ӵ�epoll������
    addfd(m_epoll_fd , sockfd , true);
    m_user_count++;     //���û���+1

    init();
}

//�쳣�¼� �ر�����
void http_connection::close_conection(){
    if(m_sockfd != -1)
    {
        removefd(m_epoll_fd, m_sockfd);
        m_sockfd = -1;      //��Ϊ-1��û������
        m_user_count--;     //�ر�һ�����ӣ��ͻ�������-1
    }
}
 
// ��ʼ���������
void http_connection::init(){
    
    int bytes_to_send = 0;    //��Ҫ���͵��ֽ���
    int bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;            //��ʼ״̬Ϊ ������������
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
    
    //ǰ��main�����Ѿ��������������ݣ�append���Ӷ����������е��У����߳��õ���Ӧ�Ķ�������˺������Ƚ������������
    HTTP_CODE read_ret =  process_read();     

    if(read_ret == NO_REQUEST)
    {
        modifyfd(m_epoll_fd , m_sockfd ,EPOLLIN);
        return;
    }


    //������Ӧ
    bool write_ret = process_write(read_ret);   //��ӦҪ����������Ľ��ȥ��Ӧ
    if(!write_ret)
    {   
        close_conection();
    }
    modifyfd(m_epoll_fd , m_sockfd ,EPOLLOUT);  //д����ɺ����out�¼�����ͻ��˻�д
} 


//  һ���԰��������ݶ������� proactorģʽ  ������ (�����������ݾ���������http����������������Ϣ) 
bool http_connection::read(){
    if(m_read_index >= READ_BUFFER_SIZE)
        return false;
    int read_bytes = 0; //�������ֽ�
    while(true)
    {
        read_bytes = recv(m_sockfd , &m_read_buf + m_read_index , READ_BUFFER_SIZE - m_read_index , 0); //�Ӵ�������m_sockfd��� (ÿһ���ͻ��˵�fd����һ��)
        
        if(read_bytes == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)     //��ʾû������ ��������������������
            {
                break;
            }    
            return false;
        }
        else if(read_bytes == 0)    //�Է��ر� �ͻ��˹ر�����
        {
            return false;
        }

        m_read_index += read_bytes;

    }
    printf("reading data : %s\n" , m_read_buf);      //��ȡ�������ݱ����ڸ����Ӷ���� m_read_buf�������
    return true;
}


// ��״̬�� ����HTTP����
http_connection::HTTP_CODE http_connection::process_read(){     
    
    LINE_STATUS line_status  = LINE_OK;
    HTTP_CODE res = NO_REQUEST;

    char *text = 0; 

    //whileѭ����Ŀ���ǣ� ��Ҫһ��һ�е�ȥ���� ����\r\n�����������һ������  ���ս���Ķ�ȡ��һ������ 
    while(( (m_check_state == CHECK_STATE_CONTENT)&& (line_status == LINE_OK) )|| ((line_status = parse_line()) == LINE_OK))  
    {   
        //��������һ������������,���߽������������壬����������Ѿ�����ײ��ˣ�Ҳ������������    

        text = get_line();     //��ȡ��һ�����ݾͽ�������ȥ����
        m_start_line = m_checked_index;
        printf("got 1 line for HTTP : %s\n" , text);

        switch (m_check_state)      //���� m_check_state��ǰ״̬
        {
            case CHECK_STATE_REQUESTLINE:   //��������
            {
                res = parse_request_line(text);        
                if(res == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:                //����ͷ
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
            case CHECK_STATE_CONTENT:           //������
            {
                res = parse_content(text);
                if(res == GET_REQUEST)
                {
                    return do_request();        
                }
                line_status = LINE_OPEN;    //���ʧ���� �Ͱ�״̬�ĳ� �����ݲ�����״̬
                break;
            }
        
            default:
            {
                return INTERNAL_ERROR;
            }

        }
    }
    return NO_REQUEST;      //������״̬����״̬

}





//�õ�һ����������ȷ��HTTP����ʱ,����Ŀ���ļ�������(m_url), 
// ���Ŀ���ļ����ڡ��������û��ɶ����Ҳ���Ŀ¼����ʹ��mmap����ӳ�䵽�ڴ��ַm_file_address����
// �����ߵ����߻�ȡ�ļ��ɹ������ǵõ���m_url,Ȼ��Ҫ�ڷ������ҵ������Դ��������д���ͻ���
//��ʵ�����������������������Ľ������˿ͻ��˵�������Ҫdo_request������Ӧ�ͻ���
http_connection::HTTP_CODE http_connection::do_request(){   
    
    strcpy( m_real_file, doc_root );    // doc_root ������Դ��·��"/home/hua/Myproject/5_project/resources" ������Ŀ���ǰ�doc_root������ m_real_file
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );  //�ٰѻ�ȡ����m_urlƴ�������·������õ� "/home/hua/Myproject/5_project/resources/index.html"
    

    //1 ƴ����ɺ����  "/home/hua/Myproject/5_project/resources/index.html"
    if ( stat( m_real_file, &m_file_stat ) < 0 )      // ��ȡm_real_file�ļ�����ص�״̬��Ϣ��-1ʧ�ܣ�0�ɹ�
    {   
        return NO_RESOURCE;
    }

    if ( ! ( m_file_stat.st_mode & S_IROTH ) )  // �жϷ���Ȩ��
    {
        return FORBIDDEN_REQUEST;
    }

    if ( S_ISDIR( m_file_stat.st_mode ) )       // �ж��Ƿ���Ŀ¼
    {
        return BAD_REQUEST;
    }

    //2 ��open������ƴ�Ӻõ�·�����ļ� (���ļ��Ǵ洢�ڷ������ϵģ� �ǿͻ���������ļ�) ���õ����ļ���FD
    int fd = open( m_real_file, O_RDONLY );
    
    //3 �����ڴ�ӳ�䣬���ĸ����������õ���FD���ļ�ӳ�䵽�ڴ��᷵��һ����ַ����ַ���Ƿ�������Ҫ��Ӧ��ȥ����Դ �������ַ��Ԥ������Ӧ����ʱ��д�����õ��ġ�
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );    //������Ҫ���͵���������mmap�ڴ�ӳ�亯��ӳ�䵽�ڴ��У��� m_file_adress����
    close( fd );
    return FILE_REQUEST;
}




// ���ݷ���������HTTP����Ľ�����������ظ��ͻ��˵�����
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
            add_status_line( 404, error_404_title );            //������Ӧ����   
            add_headers( strlen( error_404_form ) );            //��Ӧͷ
            if ( ! add_content( error_404_form ) ) {            //��Ӧ��
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
        case FILE_REQUEST:                                          //////���ɹ���Ӧ�ı��� ȫ�����ݣ����� 
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;

            /*����һ�£�ͨ��ʹ�� iovec �ṹ�� m_iv ���飬������һ�β�����ͬʱ���ݶ����������
            �Ӷ������˶�ζ�д�Ŀ�������������ܡ�����δ����У�m_iv �����ʹ����Ϊ�˽���Ӧ���ݺ�
            �ļ�����ͬʱ���͸��ͻ��ˡ�*/
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}


//  һ���԰��������ݶ�д�� proactorģʽ   ������ дHTTP��Ӧ
bool http_connection::write()
{
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // ��Ҫ���͵��ֽ�Ϊ0����һ����Ӧ������
        modifyfd( m_epoll_fd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
   
        temp = writev(m_sockfd, m_iv, m_iv_count);           // ��ɢд
        if ( temp <= -1 ) {
            // ���TCPд����û�пռ䣬��ȴ���һ��EPOLLOUT�¼�����Ȼ�ڴ��ڼ䣬
            // �������޷��������յ�ͬһ�ͻ�����һ�����󣬵����Ա�֤���ӵ������ԡ�
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
            // û������Ҫ������
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







///////////////////////////////////////����HTTP���󼸸�С����

//  ����һ�У��жϵ������� \r\n  ���������ݵ�����һ�����飨��������������ͷ��
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
                return LINE_OK;     //��ʾ�����Ľ�������һ��
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

// ����HTTP������,��ȡ���󷽷�,Ŀ��URL,HTTP�汾 ��Ҫ��Ҫ�õ�������Ϣ
http_connection::HTTP_CODE http_connection::parse_request_line(char *text){     
    
    // GET /index.html HTTP/1.1     �������Ѿ���ȡ������һ�����ݣ�������Ҫ���ľ��ǰ���һ�����ݲ��(��������)��

    m_url = strpbrk(text , " \t");        //��� �ո�+\t ���ĸ��ط��ȳ��� ���±긳��m_url ��� m_url�ַ�������   _/index.html HTTP/1.1
    
    //1 ��ȡ����
    *m_url ++ = '\0';       // GET\0/index.html HTTP/1.1
 
    char * method = text;   //��ʱ�� text����      GET\0/index.html HTTP/1.1
    if(strcasecmp(method , "GET") == 0) //�ж�method�ǲ��� GET����
    {
        m_method = GET;
    }
    else
    { 
        return BAD_REQUEST;
    }

    //2 �Ȼ�ȡ��������Ϣ �汾�� HTTP/1.1
    m_version = strpbrk(m_url , " \t"); //Ϊʲô����m_url ��Ϊ���ڵ�m_url������� _/index.html HTTP/1.1 , m_version��ָ���� HTTP/1.1
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
        m_url = strchr(m_url , '/');  //  ���˺� m_url����ľ��� �� /index.html 
    }

    if(! m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;     //��״̬�����״̬��ɼ������ͷ ��Ϊ���������Ѿ��������� ����check_state״̬Ҫ�����ı�  ���״̬��̼������ͷ

    return NO_REQUEST;
}
 
// ��������ͷ
http_connection::HTTP_CODE http_connection::parse_headers(char *text){     
    
    if( text[0] == '\0' )  // �������У���ʾͷ���ֶν������
    {
        // ���HTTP��������Ϣ�壬����Ҫ��ȡm_content_length�ֽڵ���Ϣ�壬
        // ״̬��ת�Ƶ�CHECK_STATE_CONTENT״̬
        if ( m_content_length != 0 ) 
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // ����˵�������Ѿ��õ���һ��������HTTP����
        return GET_REQUEST;
    } 
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) 
    {
        // ����Connection ͷ���ֶ�  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) 
        {
            m_linger = true;
        }
    } 
    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) 
    {
        // ����Content-Lengthͷ���ֶ�
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } 
    else if ( strncasecmp( text, "Host:", 5 ) == 0 ) 
    {
        // ����Hostͷ���ֶ�
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
 
// ���������� ����Ŀû������ȥ����������  ֻ�Ǽ򵥵��ж����������ı�����
http_connection::HTTP_CODE http_connection::parse_content(char *text){     
    if(m_read_index >= (m_content_length + m_checked_index))
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}



///////////////////////////////////////��ӦHTTP���󼸸�С����

//��Ӧ����
bool http_connection::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

// ��д������д������͵�����  д�� m_write_buf ������
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

//  �õ����ڴ�ӳ�亯�� ����Ҫ�ͷ��ڴ�ӳ�䣬�˺���ר�������ͷ��ڴ�ӳ�� munmap()���� 
void http_connection::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}





