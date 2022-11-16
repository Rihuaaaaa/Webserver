#ifndef LOCKER_H
#define LOCKER_H

#include<pthread.h>
#include<exception>
#include<semaphore.h>
//�߳�ͬ�����Ʒ�װ��

//��������
class locker{
public:
    locker(){
        int res = pthread_mutex_init(&m_mutex, nullptr);        //�����ɹ����֮��᷵���㣬�����κη���ֵ����ʾ�����˴���
        if(res != 0)
        {
            throw std::exception();
        }
    }
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    //�ṩ�й�������һЩ��ز���
    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;            //�����ɹ����֮��᷵��0
    }

    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t* get(){
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

//����������
class condition{
public:
    condition(){

        int res = pthread_cond_init(&m_cond , nullptr);
        if(res != 0)
        {
            throw std::exception();
        }

    }
    ~condition(){
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *mutex){
        return pthread_cond_wait(&m_cond, mutex) == 0; 
    }

    bool timedwait(pthread_mutex_t *mutex , struct timespec t ){
        return pthread_cond_timedwait(&m_cond, mutex , &t) == 0; 
    }

    bool signal(){      //����һ�������ȴ����߳�
        return pthread_cond_signal(&m_cond) == 0; 
    }

    bool broadcast(){       //�������еȴ����߳�
        return pthread_cond_broadcast(&m_cond) == 0; 
    }

private:
    pthread_cond_t m_cond;
};

//�ź�����
class semaphore{
public:
    semaphore(){
        int res = sem_init(&m_sem , 0, 0);
        if(res !=0)
        {
            throw std::exception();
        }
    }
    semaphore(int nums){
        int res = sem_init(&m_sem , 0, nums);
        if(res !=0)
        {
            throw std::exception();
        }
    }
    ~semaphore(){
        sem_destroy(&m_sem);
    }

    bool wait(){            //�ź���-1,���ֵΪ0������
        return sem_wait(&m_sem) == 0;
    }

    bool post(){            //�����ź���
        return sem_post(&m_sem) ==0 ;
    }

private:
    sem_t m_sem;
};





#endif