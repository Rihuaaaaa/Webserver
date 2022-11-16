#ifndef THREADPOOL
#define THREADPOOL

#include<pthread.h>
#include<exception>
#include<list>
#include"locker.h"
#include<cstdio>

//�̳߳�ģ���� ��Ϊ�˴���ĸ���, ģ�����T����������
template<class T>
class threadpool{

public:
    threadpool(int thread_number = 8, int max_requests = 10000);;
    ~threadpool();
    bool append(T* request);     //��������̳߳��е�append����
    
private:
    static void* myworker(void *arg);   //�̵߳Ĳ�������        ע�⣺��̬��Ա����û��thisָ�룬�Ǿ�̬��Ա������thisָ��
    void run();     //�����̳߳غ��� ���̳߳ش�����ҲҪ���У����ӹ���������ȥȡ����

private:    
    int m_thread_number;  //�̵߳�����

    pthread_t *m_threads;    //�̳߳����飬��СΪm_thread_number
    std::list<T*>m_work_queue;        //�������

    int m_max_requests;    //����������������ĵȴ��������������

    locker m_queue_locker;  //������  ��Ϊ���������������̹߳����

    semaphore m_queue_semstat;  //�ź��������ж��Ƿ���������Ҫ����

    bool m_stop;    //�Ƿ�����߳�
};



template<class T>   //1 ���캯��
threadpool<T>::threadpool(int thread_number , int max_requests):m_thread_number(thread_number),m_max_requests(max_requests),m_stop(false),m_threads(nullptr){
        if(thread_number <=0 || max_requests <=0)
        {
            throw std::exception();
        }

        m_threads = new pthread_t[m_thread_number];

        for(int i = 0 ; i<thread_number ; i++)      //����thread_number���̣߳������������ó��߳�����
        {
            printf("create the %d thread\n" , i);
            if( pthread_create(m_threads + i , nullptr , myworker, this)  != 0 )      //c++��worker��һ����̬�ĺ���������ȫ�ֺ��������ĸ���������this����
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

        //���캯����ɺ� �̳߳������8���̶߳�Ӧ��ID�� , pthread_create()�ĵ�һ������Ϊ�����������̴߳����ɹ������̵߳��߳�ID��д���ñ����С����m_threads + i�ǹؼ�
    }


template<class T>       //2 ��������
threadpool<T>::~threadpool(){
    delete[]m_threads;
    m_stop = true;
}




//��������̳߳��е�append����
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

    //������������һ������ �ź���Ҳ����1 ����ź����Ǵ����￪ʼ��ʼ��Ϊ1��   Ҫע�� �˴��Ϳ�ʼ�������̣߳��� ��Ӧ����run�е�wait()
    //�ź���������Ҳ�����ж������Ƿ���Ҫ������
    m_queue_semstat.post();        //�ź�����1  ֪ͨ�߳̽�������(����)
   
    return true;

}


template<class T>
void* threadpool<T>::myworker(void *arg){   //4 ��̬�ĳ�Ա�������ܹ����ʷǾ�̬�ĳ�Ա�������ı�һ��pthread_creat()�ĵ��ĸ���������this��Ϊ�������ݵ�myworker,�����Ӿ����õ��̳߳���Ķ���

    threadpool * pool = (threadpool*)arg;               //this�õ�threadpool����
    pool->run();        //�̳߳ش�����ҲҪ���У����ӹ���������ȥȡ����
    return pool;
}

template<class T>   
void threadpool<T>::run(){      //run������һֱ��ѭ���� 

    while(!m_stop)
    {
        //��Ӧappend�е�post,ͨ�����ַ�ʽ�Ϳ������̳߳�����߳�������û���������ȥ��
        //�ź�����ֵ�Ͳ�����������֮���ź���������1��û��ֵ����������(�ź���ֵ���Ϊ0) 
        m_queue_semstat.wait();             
        
        m_queue_locker.lock();      //�������������ٽ��� Ҫ����
        if(m_work_queue.empty())
        {
            m_queue_locker.unlock();
            continue;
        }

        T *request = m_work_queue.front();      //�õ���������еĵ�һ������
        m_work_queue.pop_front();           // Ȼ��Ӷ���������
        m_queue_locker.unlock();

        if(!request)
        {
            continue;
        }

        request->process();     // �ó��������� �Ϳ�ʼ������������
    }
    
}

#endif