//#include "/home/neu/Desktop/lockfree/lock_free.h"
#include "/home/neu/Desktop/lockfree/lockFree_MDList.h"
#include <thread>

int temp[110];
PriorityQueue que(64);
int i=100,num=0;

static inline uint64_t get_clocktime() { 
    long int        ns; 
    uint64_t        all; 
    time_t          sec; 
    struct timespec spec; 

    clock_gettime(CLOCK_REALTIME, &spec);

    sec   = spec.tv_sec; 
    ns    = spec.tv_nsec; 
    all   = (uint64_t) sec * 1000000000UL + (uint64_t) ns; 
    return all;  
}

void thread_func1(){
    while(num!=100){
        uintptr_t t=que.DeleteMin();
        if(t!=NIL){
            num++;
        }
    }
}

void thread_func2(){
    while(i>0){
        temp[i]=i;
        if(que.insert(i,(uintptr_t)(temp+i))==false)
            printf("%d insert false!\n",i);
        else
            printf("%d insert!\n",i);
        i--;
    }
}

int main(){
    //thread thread1(thread_func1);
    //thread thread2(thread_func2);
    //thread1.join();
    //thread2.join();
    for(int i=1;i<64;++i){
        temp[i]=i;
        if(que.insert(i,(uintptr_t)(temp+i))==false);
            //printf("%d insert false!\n",i);
    }
    for(int i=1;i<64;++i){
        printf("%d\n",*(int*)que.DeleteMin());
    }
    temp[i++]=101;
    que.insert(1,(uintptr_t)(temp+i-1));
    temp[i++]=108;
    que.insert(63,(uintptr_t)(temp+i-1));
    uint64_t start,end;
    start=get_clocktime();
    que.DeleteMin();
    //printf("%d\n",*(int*)que.DeleteMin());
    end=get_clocktime();
    printf("%ld\n",end-start);
    start=get_clocktime();
    que.DeleteMin();
    //printf("%d\n",*(int*)que.DeleteMin());
    end=get_clocktime();
    printf("%ld\n",end-start);
    return 0;
}