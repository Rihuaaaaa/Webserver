#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include<sys/epoll.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include"locker.h"
#include<sys/uio.h>
#include<string.h>
class http_connection
{
    
public:

    static int m_epoll_fd; //  1 ����socket�¼�����ע�ᵽͬһ��epoll������ static����
    static int m_user_count ;  //2 ͳ���û�����

    static const int FILENAME_LEN = 200;        // �ļ�������󳤶�

    static const int READ_BUFFER_SIZE = 2048;   // 5 ����������С
    static const int WRITE_BUFFER_SIZE = 2048;  //6 д��������С
    bool read();    //�������Ķ�
    bool write();   //��������д


    /* 9 �������״̬ */
    enum METHOD { GET = 0 ,POST , HEAD , PUT , DELETE , OPTION , CONNECT};

    /*
        �����ͻ�������ʱ����״̬����״̬
        CHECK_STATE_REQUESTLINE:��ǰ���ڷ���������
        CHECK_STATE_HEADER:��ǰ���ڷ���ͷ���ֶ�
        CHECK_STATE_CONTENT:��ǰ���ڽ���������
    */
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    

    
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };  //��״̬��(������ĳһ�е�״̬)�����ֿ���״̬�����еĶ�ȡ״̬���ֱ��ʾ1.��ȡ��һ���������� 2.�г��� 3.���������Ҳ�����
    

    /*
        ����������HTTP����Ŀ��ܽ�������Ľ����Ľ��
        NO_REQUEST          :   ������������Ҫ������ȡ�ͻ�����
        GET_REQUEST         :   ��ʾ�����һ����ɵĿͻ�����
        BAD_REQUEST         :   ��ʾ�ͻ������﷨����
        NO_RESOURCE         :   ��ʾ������û����Դ
        FORBIDDEN_REQUEST   :   ��ʾ�ͻ�����Դû���㹻�ķ���Ȩ��
        FILE_REQUEST        :   �ļ�����,��ȡ�ļ��ɹ�
        INTERNAL_ERROR      :   ��ʾ�������ڲ�����
        CLOSED_CONNECTION   :   ��ʾ�ͻ����Ѿ��ر�������
    */
    enum HTTP_CODE{NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    
    
    
public:

    http_connection(){} 
    ~http_connection(){}

    void process();   // 1 ����ͻ������� ��ӦҲ��

    void setnonblocking(int fd); //�����ļ�������������

    void init(int sockfd , const sockaddr_in &addr);        //��ʼ���½��յ�����

    void close_conection(); //�ر�����

    
    /* 9 ������һ�麯����process_read�����Է���HTTP���� */
    HTTP_CODE process_read();   //����HTTP���� (��״̬��)
    HTTP_CODE parse_request_line(char *text);   // (��״̬��) - ������������
    HTTP_CODE parse_headers(char *text);   //(��״̬��) - ��������ͷ
    HTTP_CODE parse_content(char *text);   //(��״̬��) - ����������
    LINE_STATUS parse_line();   //��״̬������һ�к���  ��ȡ����һ���ٸ���״̬���� �����������к�����ͷ����(����3��)
    HTTP_CODE do_request();    


    /* 24 ��һ�麯����process_write���������HTTPӦ��*/
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    bool process_write( HTTP_CODE ret );    // ���HTTPӦ��

private:
    
    int m_sockfd ;      //3 ��HTTP���ӵ�socket
    sockaddr_in m_address;  // 4 ͨ�ŵ�socket��ַ

    char m_read_buf[READ_BUFFER_SIZE];  // 7 ��������
    int  m_read_index;                  // 8 ��ʶ�����������Ѿ�����Ŀͻ������ݵ����һ���ֽڵ���һ��λ��

    int m_checked_index ;  //10 ��ǰ���ڷ������ַ��ڶ�����������λ��
    int m_start_line ; //11 ��ǰ���ڽ����е���ʼλ��
    CHECK_STATE m_check_state; //12 ��״̬����ǰ������״̬,�ٶ���һ��init()��ʼ����������Ա
    void init();    //13 ��ʼ�������������Ϣ

    char * get_line(){return m_read_buf + m_start_line;}        //14  ��������get_line()

    //�������еĳ�Ա
    char *m_url; //16 ����Ŀ���ļ����ļ���    -ÿһ���ͻ��������Ӧ����Դ���ǲ�һ���ģ��������Ϊ��Ա����
    char *m_version;    //17 Э��汾 ֻ֧�� HTTP1.1
    METHOD m_method;    //18 ���󷽷�   - ��init()׷�ӳ�ʼ����������Ա

    //����ͷ�ĳ�Ա
    char *m_host;   //19 ������
    bool m_linger; //20 �ж�HTTP�����Ƿ�Ҫ�������� ���� Connection: keep-alive
    int m_content_length;    // 21 HTTP�������Ϣ�ܳ���
    
    //��������Դ
    char m_real_file[ FILENAME_LEN ];       // 22 �ͻ������Ŀ���ļ�������·���������ݵ��� doc_root + m_url, doc_root����վ��Ŀ¼
    char* m_file_address;                   // 23 �ͻ������Ŀ���ļ���mmap���ڴ��е���ʼλ��
    char m_write_buf[ WRITE_BUFFER_SIZE ];  // д������
    int m_write_idx;                        // д�������д����͵��ֽ���
    struct stat m_file_stat;                // Ŀ���ļ���״̬��ͨ�������ǿ����ж��ļ��Ƿ���ڡ��Ƿ�ΪĿ¼���Ƿ�ɶ�������ȡ�ļ���С����Ϣ
    struct iovec m_iv[2];                   // ���ǽ�����writev��ִ��д���������Զ�������������Ա������m_iv_count��ʾ��д�ڴ���������
    int m_iv_count;

    int bytes_to_send;      //��Ҫ���͵��ֽ���
    int bytes_have_send;    //�Ѿ����͵��ֽ���
    

};



#endif