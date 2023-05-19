#ifndef QUEUE_H
#define QUEUE_H

#define len 10

struct Queue{
    int buff[len];
    int in;
    int out;
};

typedef struct Queue Queue;

void enqueue(Queue* q, int num){
    q->buff[q->in] = num;
    q->in = ((q->in)+1) % len;
}

int dequeue(Queue* q){
    if(q->in == q->out) return -1;
    int num = q->buff[q->out];
    q->out = ((q->out)+1) % len;
    return num;
}
#endif
