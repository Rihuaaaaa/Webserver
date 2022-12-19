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

#define MAX_FD 65535        //最大文件描述符个数
#define  MAX_EVENT_NUMBER 10000 //监听的最大事件数量

//1 添加信号捕捉
void addsig(int sig , void(handler)(int)){
    struct sigaction sa;        //注册信号
    memset(&sa , '\0' , sizeof(sa));      //清空作用，将sa内都置为空 ；memset是一个初始化函数，作用是将某一块内存中的全部设置为指定的值。
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);  //将信号集中的所有的标志位置为1,都设置为阻塞
    sigaction(sig , &sa , nullptr);     //捕捉传进来的 sig 信号

}

//2 封装向epoll中添加需要监听的文件描述符
extern void addfd(int epoll_fd, int fd , bool one_shot);      //extern关键字告诉编译器num这个变量是存在的，但是不是在这之前声明的，你到别的地方找找吧,引用不在同一个文件中的变量或者函数。也就是说只有当一个变量是一个全局变量时，extern变量才会起作用

//3 封装从epoll删除文件描述符
extern void removefd (int epoll_fd , int fd);
 
//4 封装修改epoll文件描述符
extern void modify(int epoll_fd , int fd , int ev);

int main(int argc , char * argv[]){     

    if(argc <= 1)       //参数至少要传递一个端口号
    {
        printf("请按照命令运行 %s port_number\n" ,basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port = atoi(argv[1]);   //stoi的参数是const string*，atoi的参数是const char*。可以直接将char作为stoi函数的参数

    //对 SIGPIE信号进行处理
    addsig(SIGPIPE, SIG_IGN);
 
    //初始化线程池
    threadpool<http_connection> *pool  = nullptr;       //http_connection连接的一个任务类 
    try
    {
        pool = new threadpool<http_connection>;
    }
    catch(...)
    {
        exit(-1);
    }


    //创建数组用于保存所有客户端信息
    http_connection *users = new http_connection[MAX_FD];
    
    // 开始编写网络代码
    int listen_fd = socket(PF_INET , SOCK_STREAM , 0);

    // 设置端口复用(注意要在绑定之前设置)
    int reuse = 1;          // 1表示可以复用 ，0表示不可复用
    setsockopt(listen_fd , SOL_SOCKET , SO_REUSEADDR, &reuse , sizeof(reuse));
    // 绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listen_fd , (struct sockaddr*)&address, sizeof(address));
    
    //监听
    listen(listen_fd , 5);


    //创建epoll对象,事件数组
    struct epoll_event events[MAX_EVENT_NUMBER];    //检测传出的数组
    int epoll_fd = epoll_create(1);

    //将监听的文件描述符添加到epoll对象中  , 另外编写一个函数 执行epoll_ctl()函数的操作
    addfd(epoll_fd , listen_fd , false);            //监听描述符不需要注册oneshot事件 
    http_connection::m_epoll_fd = epoll_fd;     //类中的所有对象都共享一个 epollfd


    while(1)        // 主线程不断地循环检测监听描述符有无发生变化
    {
        int num = epoll_wait(epoll_fd , events , MAX_EVENT_NUMBER , -1);
        if(num < 0 && errno != EINTR)
        {
            printf("epoll call failure\n");
            break;
        }

        for(int i = 0 ; i<num ; i++)        //遍历events事件数组，检测有无发生变化的文件描述符
        {
            int cur_sockfd = events[i].data.fd;     //当前发生事件变化的文件描述符

            if(cur_sockfd == listen_fd) //表示有客户端连接进来
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlenth = sizeof(client_address);
                int connect_fd = accept(listen_fd , (struct sockaddr*)&client_address , &client_addrlenth);

                if(http_connection::m_user_count >= MAX_FD)     //连接数满
                {   
                    close(connect_fd);  //给客户端写一个信息，服务器内部正忙
                    continue;
                }

                users[connect_fd].init(connect_fd , client_address);  // 将新客户的数据初始化，放到数组中 ，数组中保存每一个连接进来客户端的 文件描述符信息和地址信息
            }       
            else if(events[i].events &(EPOLLRDHUP | EPOLLHUP | EPOLLERR))    //对方异常断开等错误事件
            {
                users[cur_sockfd].close_conection();            //6 异常断开函数
            }

            else if(events[i].events & EPOLLIN)     //表示检测到了读事件描述符发生变化 ，对读事件描述符进行读操作
            {
                if( users[cur_sockfd].read())   // 一次性把所有数据都读出来 proactor模式
                {
                    pool->append(users + cur_sockfd);   //一次性读完成功了后 ，就把他交给工作线程去处理业务逻辑，传入的是一个指针，即在user数组首指针+文件描述符的位数，
                }
                else
                {
                    users[cur_sockfd].close_conection();
                }

            }
            else if(events[i].events & EPOLLOUT)   //表示检测到了写事件描述符发生变化 ，对写事件描述符进行写操作
            {
                if( !users[cur_sockfd].write())     // 一次性写完所有数据
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