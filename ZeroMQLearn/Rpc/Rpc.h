#include <string>
#include <map>
#include <functional>
#include <zmq.hpp>
#include "Serializer.h"


template <typename T>
struct type_xx {typedef T type;};

template <>
struct type_xx<void> {typedef int8_t type;}

template<typename Tuple, std::size_t... Is>
void package_params_impl(Serializer& ds, const Tuple& t, std::index_sequence<Is...>) {
    initializer_list<int>{((ds << std::get<Is>(t)), 0)...};
}

template<typename ...Args>
void package_params(Serializer& ds, const std::tuple<Args...>& t){
    package_params_impl(ds, t, std::index_sequence_for<Args...>{});
}

template<typename Function, typename Tuple, std::size_t... Index>
decltype(auto) invoke_impl(Function&& func, Tuple&& t, std::index_sequence<Index...>) {
    return func(std::get<Index>(std::forward<Tuple>(t))...);
}

// 使用tuple作为参数
template<typename Function, typename Tuple>
decltype(auto) invoke(Function&& func, Tuple&& t){
    constexpr auto size = std::tuple_size<typename std::decay<Tuple>::type>::value;
    return invoke_impl(std::forward<Function>(fun), std::forward<Tuple>(t), std::make_index_sequence<size>{});
}

// 用于是否返回void的情况
template <typename R, typename F, typename ArgsTuple>
typename std::enable_if<std::is_same<R, void>::value, typename type_xx<R>::type>::type
call_helper(F f, ArgsTuple args) {
    invoke(f, args);
    return 0;
}

template <typename R, typename F, typename ArgsTuple>
typename std::enable_if<!std::is_same<R, void>::value, typename type_xx<R>::type>::type
call_helper(F f, ArgsTuple args) {
    return invoke(f, args);
}

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
            using args_type = std::tuple<typename std::decay<Params>::type...>;
            args_type args = std::make_tuple(ps); // 将参数封装成tuple

            Serializer ds;
            ds << name;
            package_params(ds, args);
            return net_call<R>(ds);
        }

        template<typename R>
        value_t<R> call(std::string name) {
            Serializer ds;
            ds << name;
            return net_call<R>(ds);
        }

    private:
        Serializer* call_(std::string name, const char* data, int len);

        template<typename F>
        value_t<R> net_call(Serializer& ds);

        template<typename F>
        void callproxy(F fun, Serializer* pr, const char* data, int len);

        template<typename F, typename S>
        void callproxy(F funn, S* s, Serializer* pr, const char* data, int len);

        // 函数指针
        template<typename R, typename ...Params>
        void callproxy_(R(*func)(Params...), Serializer* pr, const char* data, int len) {
            callproxy_(std::function<R(Params...)>(func), pr, data, len);
        }

        // 类成员指针
        template<typename R, typename C, typename S, typename... Params>
        void callproxy_(R(C::* func)(Params...), S* s, Serializer* pr, const char* data, int len) {

            using args_type = std::tuple<typename std::decay<Params>::type...>;

            Serializer ds(StreamBuffer(data, len));
            constexpr auto N = std::tuple_size<typename std::decay<args_type>::type>::value;
            args_type args = ds.get_tuple <args_type> (std::make_index_sequence<N>{});

            auto ff = [=](Params... ps) -> R{
                return (s->*func)(ps...);
            };
            typename type_xx<R>::type r = call_helper<R>(ff, args);

            value_t<R> val;
            val.set_code(RPC_ERR_SUCCESS);
            val.set_val(r);
            (*pr) << val;
        }

        // functional
        template<typename R, typename ... Params>
        void callproxy_(std::function<R(Params... ps)> func, Serializer* pr, const char* data, int len) {
            using args_type = std::tuple<typename std::decay<Params>::type...>;

            Serializer ds(StreamBuffer(data, len));
            constexpr auto N = std::tuple_size<typename std::decay<args_type>::type>::value;
            args_type args = ds.get_tuple <args_type> (std::make_index_sequence<N>{});

            typename type_xx<R>::type r = call_helper<R>(func, args);

            value_t<R> val;
            val.set_code(RPC_ERR_SUCCESS);
            val.set_val(r);
            (*pr) << val;
        }

    private:
        std::map<std::string, std::function<void(Serializer*, const char*, int)>> m_handlers;

        zmq::context_t m_context;
        std::unique_ptr<zmq::socket_t, std::function<void(zmq::socket_t*)>> m_socket;

        rpc_err_code m_error_code;

        int m_role;
};

inline RPC::RPC() : m_context(1) {
    m_error_code = RPC_ERR_SUCCESS;
}

inline RPC::~RPC(){
    m_context.close();
}

// network
inline void RPC::as_client(std::string ip, int port) {
    m_role = RPC_CLIENT;
    m_socket = std::unique_ptr<zmq::socket_t, std::function<void(zmq::socket_t*)>>
    (new zmq::socket_t(m_context, ZMQ_REQ), [](zmq::socket_t* sock){
        sock->close();
        delete sock;
        sock = nullptr;
    });
    ostringstream os;
    os << "tcp://" << ip << ":" << port;
    m_socket->connect(os.str());
}

inline void RPC::as_server(int port) {
    m_role = RPC_SERVER;
    m_socket = std::unique_ptr<zmq::socket_t, std::function<void(zmq::socket_t*)>>
    (new zmq::socket_t(m_context,ZMQ_REP),[](zmq::socket_t* sock){
        sock->close();
        delete sock;
        sock = nullptr;
    });
}

inline void RPC::send(zmq::message_t& data) {
    m_socket->send(data);
}

inline void RPC::recv(zmq::message_t& data) {
    m_socket->recv(&data);
}

inline void RPC::set_timeout(uint32_t ms) {
    // only client can set
    if (m_role == RPC_CLIENT) {
        m_socket->setsockopt(ZMQ_RCVTIMEO, ms);
    }
}

inline Serializer* buttonrpc::call_(std::string name, const char* data, int len) {
    Serializer* ds = new Serializer();
    if (m_handlers.find(name) == m_handlers.end()) {
        (*ds) << value_t<int>::code_type(RPC_ERR_FUNCTION_NOT_BIND);
        (*ds) << value_t<int>::msg_type("function not bind: "+ name);
        reutrn ds;
    }

    auto fun = m_handlers[name];
    fun(ds, data, len);
    ds->reset();
    return ds;
}

inline void RPC::run() {
    // only sserver can call
    if (m_role != RPC_SERVER) {
        return;
    }
    while (1) {
        zmq::message_t data;
        recv(data);
        StreamBuffer iodev((char *)data.data(), data.size());
        Serializer ds(iodev);

        std::string funname;
        ds >> funname;
        Serializer *r = call_(funname, ds.current(), ds.size()-funname.size());
        
        zmq::message_t retmsg(r->size());
        memcpy(retmsg.data(), r->data(), r->size());
        send(retmsg);
        delete r;
    }
}

template<typename F>
inline void RPC::bind(std::string name, F func) {
    m_handlers[name] = std::bind(&RPC::callproxy<F>, this, func, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

template<typename F, typename S>
inline void RPC::bind(std::string name, F func, S* s) {
    m_handlers[nname] = std::bind(&RPC::callproxy<F, S>, this, func, s, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}


template<typename F, typename S>
inline void RPC::callproxy(F fun, Serializer* pr, const char* data, int len) {
    callproxy_(fun, pr, data, len);
}

template <typename F, typename S>
inline void RPC::callproxy(F fun, S* s, Serializer* pr, const char * data, int len) {
    callproxy_(fun, s, pr, data, len);
}


template <typename R>
inline RPC::value_t<R> RPC::net_call(Serializer& ds) {
    zmq::message_t request(ds.size() + 1);
    memcpy(request.data(), ds.data(), ds.size());
    if (m_error_code != RPC_ERR_ERCV_TIMEOUT) {
        send(request);
    }
    zmq::message_t reply;
    recv(reply);
    value_t<R> val;
    if (reply.size() == 0) {
        // timeout
        m_error_code = RPC_ERR_ERCV_TIMEOUT;
        val.set_code(RPC_ERR_ERCV_TIMEOUT);
        val.set_msg("recv timeout");
        return val;
    }
    m_error_code = RPC_ERR_SUCCESS;
    ds.clear();
    ds.write_raw_data((char*)reply.data(), reply.size());
    ds.reset();

    ds >> val;
    return val;
}