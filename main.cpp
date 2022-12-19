#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<signal.h>
#include"locker.h"
#include"http_connection.h"
#include"threadpool.h"

#define MAX_FD 65535        //����ļ�����������
#define  MAX_EVENT_NUMBER 10000 //����������¼�����

//1 ����źŲ�׽
void addsig(int sig , void(handler)(int)){
    struct sigaction sa;        //ע���ź�
    memset(&sa , '\0' , sizeof(sa));      //������ã���sa�ڶ���Ϊ�� ��memset��һ����ʼ�������������ǽ�ĳһ���ڴ��е�ȫ������Ϊָ����ֵ��
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);  //���źż��е����еı�־λ��Ϊ1,������Ϊ����
    sigaction(sig , &sa , nullptr);     //��׽�������� sig �ź�

}

//2 ��װ��epoll�������Ҫ�������ļ�������
extern void addfd(int epoll_fd, int fd , bool one_shot);      //extern�ؼ��ָ��߱�����num��������Ǵ��ڵģ����ǲ�������֮ǰ�����ģ��㵽��ĵط����Ұ�,���ò���ͬһ���ļ��еı������ߺ�����Ҳ����˵ֻ�е�һ��������һ��ȫ�ֱ���ʱ��extern�����Ż�������

//3 ��װ��epollɾ���ļ�������
extern void removefd (int epoll_fd , int fd);
 
//4 ��װ�޸�epoll�ļ�������
extern void modify(int epoll_fd , int fd , int ev);

int main(int argc , char * argv[]){     

    if(argc <= 1)       //��������Ҫ����һ���˿ں�
    {
        printf("�밴���������� %s port_number\n" ,basename(argv[0]));
        exit(-1);
    }

    //��ȡ�˿ں�
    int port = atoi(argv[1]);   //stoi�Ĳ�����const string*��atoi�Ĳ�����const char*������ֱ�ӽ�char��Ϊstoi�����Ĳ���

    //�� SIGPIE�źŽ��д���
    addsig(SIGPIPE, SIG_IGN);
 
    //��ʼ���̳߳�
    threadpool<http_connection> *pool  = nullptr;       //http_connection���ӵ�һ�������� 
    try
    {
        pool = new threadpool<http_connection>;
    }
    catch(...)
    {
        exit(-1);
    }


    //�����������ڱ������пͻ�����Ϣ
    http_connection *users = new http_connection[MAX_FD];
    
    // ��ʼ��д�������
    int listen_fd = socket(PF_INET , SOCK_STREAM , 0);

    // ���ö˿ڸ���(ע��Ҫ�ڰ�֮ǰ����)
    int reuse = 1;          // 1��ʾ���Ը��� ��0��ʾ���ɸ���
    setsockopt(listen_fd , SOL_SOCKET , SO_REUSEADDR, &reuse , sizeof(reuse));
    // ��
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listen_fd , (struct sockaddr*)&address, sizeof(address));
    
    //����
    listen(listen_fd , 5);


    //����epoll����,�¼�����
    struct epoll_event events[MAX_EVENT_NUMBER];    //��⴫��������
    int epoll_fd = epoll_create(1);

    //���������ļ���������ӵ�epoll������  , �����дһ������ ִ��epoll_ctl()�����Ĳ���
    addfd(epoll_fd , listen_fd , false);            //��������������Ҫע��oneshot�¼� 
    http_connection::m_epoll_fd = epoll_fd;     //���е����ж��󶼹���һ�� epollfd


    while(1)        // ���̲߳��ϵ�ѭ�����������������޷����仯
    {
        int num = epoll_wait(epoll_fd , events , MAX_EVENT_NUMBER , -1);
        if(num < 0 && errno != EINTR)
        {
            printf("epoll call failure\n");
            break;
        }

        for(int i = 0 ; i<num ; i++)        //����events�¼����飬������޷����仯���ļ�������
        {
            int cur_sockfd = events[i].data.fd;     //��ǰ�����¼��仯���ļ�������

            if(cur_sockfd == listen_fd) //��ʾ�пͻ������ӽ���
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlenth = sizeof(client_address);
                int connect_fd = accept(listen_fd , (struct sockaddr*)&client_address , &client_addrlenth);

                if(http_connection::m_user_count >= MAX_FD)     //��������
                {   
                    close(connect_fd);  //���ͻ���дһ����Ϣ���������ڲ���æ
                    continue;
                }

                users[connect_fd].init(connect_fd , client_address);  // ���¿ͻ������ݳ�ʼ�����ŵ������� �������б���ÿһ�����ӽ����ͻ��˵� �ļ���������Ϣ�͵�ַ��Ϣ
            }       
            else if(events[i].events &(EPOLLRDHUP | EPOLLHUP | EPOLLERR))    //�Է��쳣�Ͽ��ȴ����¼�
            {
                users[cur_sockfd].close_conection();            //6 �쳣�Ͽ�����
            }

            else if(events[i].events & EPOLLIN)     //��ʾ��⵽�˶��¼������������仯 ���Զ��¼����������ж�����
            {
                if( users[cur_sockfd].read())   // һ���԰��������ݶ������� proactorģʽ
                {
                    pool->append(users + cur_sockfd);   //һ���Զ���ɹ��˺� ���Ͱ������������߳�ȥ����ҵ���߼����������һ��ָ�룬����user������ָ��+�ļ���������λ����
                }
                else
                {
                    users[cur_sockfd].close_conection();
                }

            }
            else if(events[i].events & EPOLLOUT)   //��ʾ��⵽��д�¼������������仯 ����д�¼�����������д����
            {
                if( !users[cur_sockfd].write())     // һ����д����������
                {
                    users[cur_sockfd].close_conection();
                }
            }
        }

    }

    close(epoll_fd);
    close(listen_fd);
    delete[]users;
    delete[]pool;

    return 0;

}