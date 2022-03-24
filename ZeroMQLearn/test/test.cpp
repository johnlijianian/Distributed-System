#include "zhelpers.hpp"

int main(){
    //  Prepare our context and sockets
    zmq::context_t context(1);

    //  任务分发器
    zmq::socket_t receiver(context, ZMQ_PULL);
    receiver.connect("tcp://localhost:5557");

    //  连接至天气服务
    void *subscriber = zmq_socket (context, ZMQ_SUB);
    zmq_connect (subscriber, "tcp://localhost:5556");
    zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "10001 ", 6);

    //  处理从两个套接字中接收到的消息
    //  这里我们会优先处理从任务分发器接收到的消息
    while (1) {
        //  处理等待中的任务
        bool rc;
        do {
        	zmq::message_t task;
            if ((rc = receiver.recv(&task, ZMQ_DONTWAIT)) == true) {
                //  处理任务
            }
        } while(rc == true);

        //  处理等待中的气象更新
        do {
            zmq::message_t update;
            if ((rc = subscriber.recv(&update, ZMQ_DONTWAIT)) == true) {
                //  处理气象更新

            }
        } while(rc == true);
        
        //  No activity, so sleep for 1 msec
        s_sleep(1);

    }

}