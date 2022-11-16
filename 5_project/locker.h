#ifndef LOCKER_H
#define LOCKER_H

#include<pthread.h>
#include<exception>
#include<semaphore.h>
//线程同步机制封装类

//互斥锁类
class locker{
public:
    locker(){
        int res = pthread_mutex_init(&m_mutex, nullptr);        //函数成功完成之后会返回零，其他任何返回值都表示出现了错误
        if(res != 0)
        {
            throw std::exception();
        }
    }
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    //提供有关于锁的一些相关操作
    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;            //上锁成功完成之后会返回0
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

//条件变量类
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

    bool signal(){      //唤醒一个或多个等待的线程
        return pthread_cond_signal(&m_cond) == 0; 
    }

    bool broadcast(){       //唤醒所有等待的线程
        return pthread_cond_broadcast(&m_cond) == 0; 
    }

private:
    pthread_cond_t m_cond;
};

//信号量类
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

    bool wait(){            //信号量-1,如果值为0则阻塞
        return sem_wait(&m_sem) == 0;
    }

    bool post(){            //增加信号量
        return sem_post(&m_sem) ==0 ;
    }

private:
    sem_t m_sem;
};





#endif