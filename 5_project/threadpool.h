#ifndef THREADPOOL
#define THREADPOOL

#include<pthread.h>
#include<exception>
#include<list>
#include"locker.h"
#include<cstdio>

//线程池模板类 ，为了代码的复用, 模板参数T就是任务类
template<class T>
class threadpool{

public:
    threadpool(int thread_number = 8, int max_requests = 10000);;
    ~threadpool();
    bool append(T* request);     //添加任务到线程池中的append函数
    
private:
    static void* myworker(void *arg);   //线程的操作函数        注意：静态成员函数没有this指针，非静态成员函数有this指针
    void run();     //运行线程池函数 ，线程池创建好也要运行，即从工作队列中去取数据

private:    
    int m_thread_number;  //线程的数量

    pthread_t *m_threads;    //线程池数组，大小为m_thread_number
    std::list<T*>m_work_queue;        //请求队列

    int m_max_requests;    //请求队列中最多允许的等待处理的请求数量

    locker m_queue_locker;  //互斥锁  因为工作队列是所有线程共享的

    semaphore m_queue_semstat;  //信号量用来判断是否有任务需要处理

    bool m_stop;    //是否结束线程
};



template<class T>   //1 构造函数
threadpool<T>::threadpool(int thread_number , int max_requests):m_thread_number(thread_number),m_max_requests(max_requests),m_stop(false),m_threads(nullptr){
        if(thread_number <=0 || max_requests <=0)
        {
            throw std::exception();
        }

        m_threads = new pthread_t[m_thread_number];

        for(int i = 0 ; i<thread_number ; i++)      //创建thread_number个线程，并将他们设置成线程脱离
        {
            printf("create the %d thread\n" , i);
            if( pthread_create(m_threads + i , nullptr , myworker, this)  != 0 )      //c++中worker是一个静态的函数，不是全局函数，第四个参数传递this对象
            {
                delete[]m_threads;
                throw std::exception();
            }

            if( pthread_detach(m_threads[i]))
            {
                delete[]m_threads;
                throw std::exception();
            }
        }

        //构造函数完成后 线程池里就有8个线程对应的ID了 , pthread_create()的第一个参数为传出参数，线程创建成功后，子线程的线程ID被写到该变量中。因此m_threads + i是关键
    }


template<class T>       //2 析构函数
threadpool<T>::~threadpool(){
    delete[]m_threads;
    m_stop = true;
}




//添加任务到线程池中的append函数
template<class T>       
bool threadpool<T>::append(T* request){
    m_queue_locker.lock();
    if(m_work_queue.size()  > m_max_requests)
    {
        m_queue_locker.unlock();
        return false;
    }

    m_work_queue.push_back(request);
    m_queue_locker.unlock();

    //队列中增加了一个任务 信号量也增加1 因此信号量是从这里开始初始化为1的   要注意 此处就开始唤醒了线程！！ 对应的是run中的wait()
    //信号量的作用也是来判断任务是否需要被处理
    m_queue_semstat.post();        //信号量加1  通知线程进行消费(唤醒)
   
    return true;

}


template<class T>
void* threadpool<T>::myworker(void *arg){   //4 静态的成员函数不能够访问非静态的成员变量，改变一下pthread_creat()的第四个参数，将this作为参数传递到myworker,这样子就能拿到线程池类的对象

    threadpool * pool = (threadpool*)arg;               //this拿到threadpool对象
    pool->run();        //线程池创建好也要运行，即从工作队列中去取数据
    return pool;
}

template<class T>   
void threadpool<T>::run(){      //run函数是一直在循环的 

    while(!m_stop)
    {
        //对应append中的post,通过这种方式就可以让线程池里的线程明白有没有任务可以去做
        //信号量有值就不阻塞，用了之后信号量个数减1，没有值就阻塞在这(信号量值如果为0) 
        m_queue_semstat.wait();             
        
        m_queue_locker.lock();      //操作队列属于临界区 要上锁
        if(m_work_queue.empty())
        {
            m_queue_locker.unlock();
            continue;
        }

        T *request = m_work_queue.front();      //拿到任务队列中的第一个任务
        m_work_queue.pop_front();           // 然后从队列中消除
        m_queue_locker.unlock();

        if(!request)
        {
            continue;
        }

        request->process();     // 拿出来了任务 就开始进行任务处理函数
    }
    
}

#endif