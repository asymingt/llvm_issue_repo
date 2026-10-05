#include <pthread.h>
#include <stdio.h>

int Global = 0;

void *Thread1(void *x) {
    Global = 42; // Write from thread 1
    return x;
}

int main() {
    pthread_t t;
    pthread_create(&t, NULL, Thread1, NULL);
    
    Global = 43; // Write from main thread at the same time
    
    pthread_join(t, NULL);
    printf("Global = %d\n", Global);
    return 0;
}
