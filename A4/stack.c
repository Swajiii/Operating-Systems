#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#define NUM_THREADS 	10
typedef struct Node
{
    int data;
    struct Node *next;
} StackNode;
StackNode *top;
pthread_mutex_t stack_lock = PTHREAD_MUTEX_INITIALIZER;
void push(int v, StackNode **top);
int pop(StackNode **top);
int is_empty(StackNode *top);
void push(int v, StackNode **top)
{
    StackNode *new_node;
    printf("Thread is running Push() operation for value: %d\n", v);
    new_node = (StackNode *)malloc(sizeof(StackNode));
    new_node->data = v;

    pthread_mutex_lock(&stack_lock);
    new_node->next = *top;
    *top = new_node;
    pthread_mutex_unlock(&stack_lock);
}
int pop(StackNode **top)
{
    StackNode *temp;
    int data;

    pthread_mutex_lock(&stack_lock);
    if (is_empty(*top)) {
        pthread_mutex_unlock(&stack_lock);
        return -1;
    }
    data = (*top)->data;
    temp = *top;
    *top = (*top)->next;
    pthread_mutex_unlock(&stack_lock);

    free(temp);
    printf("Thread is running Pop() operation and value is: %d\n", data);
    return data;
}
int is_empty(StackNode *top) {
    if (top == NULL)
        return 1;
    else
        return 0;
}
void* thread_push(void *args)
{
    int *threadId = (int *)args;
    push(*threadId + 1, &top);
    return NULL;
}
// Thread's pop function
void* thread_pop(void *args)
{
    (void)args;
    while (pop(&top) == -1)
        sched_yield();
    return NULL;
}
int main(void)
{
    pthread_t threads_push[NUM_THREADS];
    pthread_t threads_pop[NUM_THREADS];
    int thread_args[NUM_THREADS];
    int i, j;
    for(i=0;i<NUM_THREADS;i++){
        thread_args[i] = i;
        pthread_create(&threads_push[i],NULL,thread_push, (void *)&thread_args[i]);
    }
    for(i=0;i<NUM_THREADS;i++){
        pthread_create(&threads_pop[i],NULL,thread_pop, NULL);
    }
    for(j = 0; j < NUM_THREADS; j++)
    {
        pthread_join(threads_push[j],NULL);
    }
    for(j = 0; j < NUM_THREADS; j++)
    {
        pthread_join(threads_pop[j],NULL);
    }
    pthread_mutex_destroy(&stack_lock);
    pthread_exit(NULL);
    return 0;
}