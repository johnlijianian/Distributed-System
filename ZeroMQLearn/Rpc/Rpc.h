#include <string>
#include <map>
#include <functional>
#include <zmq.hpp>
#include "Serializer.h"


template <typename T>
struct type_xx {typedef T type;};


class RPC{

    public:
        enum rpc_role{
            RPC_CLIENT;
            RPC_SERVER;
        };

        enum rpc_err_code{
            RPC_ERR_SUCCESS = 0;
            RPC_ERR_FUNCTION_NOT_BIND,
            RPC_ERR_ERCV_TIMEOUT
        };

        template<typename T>
        class value_t {
            public:
                typedef typename type_xx<T>::type type;
                typedef std::string msg_type;
                typedef uint16_t code_type;

                value_t() { code_ = 0; msg_.clear();}
                bool valid() {return (code_ == 0 ? true : false);}
                int error_code() {return code_;}
                std::string error_msg() {return msg_;}
                type val() {return val_;}

                friend Serializer& operator >> (Serializer& in, value_t<T>& d) {
                    in >> d.code_ >> d.msg_;
                    if (d.code_ == 0) {
                        in >> d.val_;
                    }
                    return in;
                }

                friend Serializer& operator << (Serializer& out, value_t<T> d){
                    out << d.code_ << d.msg_ << d.val_;
                    return out;
                }

            private:
                code_type code_;
                msg_type msg_;
                type val_;
        };

        RPC();
        ~RPC();

        // network
        void as_client(std::string ip, int port);
        void as_server(int port);
        void send(zmq::message_t& data);
        void recv(zmq::message_t& data);
        void set_timeout(uint32_t ms);
        void run();

    public:
        // server
        template<typename F>
        void bind(std::string name, F func);

        template<typename F, typename S>
        void bind(std::string name, F func, S* s);

        // client
        template<typename R, typename... Params>
        value_t<R> call(std::string name, Params... ps) {
            using args
        }

        template<typename R>
        value_t<R> call(std::string name) {
            Serializer ds;
            ds << name;
            return
        }
};