//
//  使用LRU算法的装置
//  client和worker处于不同的线程中
//
#include "zhelpers.h"
#include <pthread.h>
 
#define NBR_CLIENTS 10
#define NBR_WORKERS 3
 
//  出队操作，使用一个可存储任何类型的数组实现
#define DEQUEUE(q) memmove (&(q)[0], &(q)[1], sizeof (q) - sizeof (q [0]))
 
//  使用REQ套接字实现基本的请求-应答模式
//  由于s_send()和s_recv()不能处理0MQ的二进制套接字标识，
//  所以这里会生成一个可打印的字符串标识。
//
static void *
client_task (void *args)
{
    void *context = zmq_init (1);
    void *client = zmq_socket (context, ZMQ_REQ);
    s_set_id (client);          //  设置可打印的标识
    zmq_connect (client, "ipc://frontend.ipc");
 
    //  发送请求并获取应答信息
    s_send (client, "HELLO");
    char *reply = s_recv (client);
    printf ("Client: %s\n", reply);
    free (reply);
    zmq_close (client);
    zmq_term (context);
    return NULL;
}
 
//  worker使用REQ套接字实现LRU算法
//
static void *
worker_task (void *args)
{
    void *context = zmq_init (1);
    void *worker = zmq_socket (context, ZMQ_REQ);
    s_set_id (worker);          //  设置可打印的标识
    zmq_connect (worker, "ipc://backend.ipc");
 
    //  告诉代理worker已经准备好
    s_send (worker, "READY");
 
    while (1) {
        //  将消息中空帧之前的所有内容（信封）保存起来，
        //  本例中空帧之前只有一帧，但可以有更多。
        char *address = s_recv (worker);
        char *empty = s_recv (worker);
        assert (*empty == 0);
        free (empty);
 
        //  获取请求，并发送回应
        char *request = s_recv (worker);
        printf ("Worker: %s\n", request);
        free (request);
 
        s_sendmore (worker, address);
        s_sendmore (worker, "");
        s_send     (worker, "OK");
        free (address);
    }
    zmq_close (worker);
    zmq_term (context);
    return NULL;
}
 
int main (void)
{
    //  准备0MQ上下文和套接字
    void *context = zmq_init (1);
    void *frontend = zmq_socket (context, ZMQ_ROUTER);
    void *backend  = zmq_socket (context, ZMQ_ROUTER);
    zmq_bind (frontend, "ipc://frontend.ipc");
    zmq_bind (backend,  "ipc://backend.ipc");
 
    int client_nbr;
    for (client_nbr = 0; client_nbr < NBR_CLIENTS; client_nbr++) {
        pthread_t client;
        pthread_create (&client, NULL, client_task, NULL);
    }
    int worker_nbr;
    for (worker_nbr = 0; worker_nbr < NBR_WORKERS; worker_nbr++) {
        pthread_t worker;
        pthread_create (&worker, NULL, worker_task, NULL);
    }
    //  LRU逻辑
    //  - 一直从backend中获取消息；当有超过一个worker空闲时才从frontend获取消息。
    //  - 当woker回应时，会将该worker标记为已准备好，并转发woker的回应给client
    //  - 如果client发送了请求，就将该请求转发给下一个worker
 
    //  存放可用worker的队列
    int available_workers = 0;
    char *worker_queue [10];
 
    while (1) {
        zmq_pollitem_t items [] = {
            { backend,  0, ZMQ_POLLIN, 0 },
            { frontend, 0, ZMQ_POLLIN, 0 }
        };
        zmq_poll (items, available_workers? 2: 1, -1);
 
        //  处理backend中worker的队列
        if (items [0].revents & ZMQ_POLLIN) {
            //  将worker的地址入队
            char *worker_addr = s_recv (backend);
            assert (available_workers < NBR_WORKERS);
            worker_queue [available_workers++] = worker_addr;
 
            //  跳过空帧
 
            char *empty = s_recv (backend);
            assert (empty [0] == 0);
            free (empty);
 
            // 第三帧是“READY”或是一个client的地址
            char *client_addr = s_recv (backend);
 
            //  如果是一个应答消息，则转发给client
            if (strcmp (client_addr, "READY") != 0) {
                empty = s_recv (backend);
                assert (empty [0] == 0);
                free (empty);
                char *reply = s_recv (backend);
                s_sendmore (frontend, client_addr);
                s_sendmore (frontend, "");
                s_send     (frontend, reply);
                free (reply);
                if (--client_nbr == 0)
                    break;      //  处理N条消息后退出
            }
            free (client_addr);
        }
        if (items [1].revents & ZMQ_POLLIN) {
            //  获取下一个client的请求，交给空闲的worker处理
            //  client请求的消息格式是：[client地址][空帧][请求内容]
            char *client_addr = s_recv (frontend);
            char *empty = s_recv (frontend);
            assert (empty [0] == 0);
            free (empty);
            char *request = s_recv (frontend);
 
            s_sendmore (backend, worker_queue [0]);
            s_sendmore (backend, "");
            s_sendmore (backend, client_addr);
            s_sendmore (backend, "");
            s_send     (backend, request);
 
            free (client_addr);
            free (request);
 
            //  将该worker的地址出队
            free (worker_queue [0]);
            DEQUEUE (worker_queue);
            available_workers--;
        }
    }
    zmq_close (frontend);
    zmq_close (backend);
    zmq_term (context);
    return 0;
}