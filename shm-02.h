#define  NOT_READY  -1
#define  FILLED     0
#define  TAKEN      1
#define READY    2
#include <stddef.h>
#include <pthread.h>
#include <semaphore.h>
struct Memory {
        pthread_mutex_t lock;
        pthread_mutexattr_t mutexAttr;
        int id;
        int status;
	       int msg_type;
     	int size;
        int flag;
        int free;
        int vm_done;
        int m_done;
        int size_alloc;
        int size_dealloc;
        int64_t r_size;
        int ptr[1000000];
};

