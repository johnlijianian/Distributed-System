#pragma once
#include <vector>
#include <sstream>
#include <tuple>
#include <algorithm>

using namespace std;

class StreamBuffer : public vector<char> {
    public:
        StreamBuffer() {m_curpos = 0;}
        StringBuffer(const char* in, size_t len) {
            m_curpos = 0;
            insert(begin(), in, in+len);
        } 
        ~StreamBuffer() { }

        void reset() { m_curpos = 0; }
        const char* data() {return &(*this)[0];}
        const char* current() {return &(*this)[m_curpos];}
        void offset(int k) {m_curpos += k;}
        bool is_eof() {return (m_curpos >= size());}
        void input(char *in, size_t len) {insert(end(), in, in+len);}
        int findc(char c){
            iterator itr = find(begin() + m_curpos, end(), c);
            if (itr != end()) {
                return itr - (begin() + m_curpos);
            }
            return -1;
        }

    private:
        // 当前字节流位置
        unsigned int m_curpos;
};

class Serializer{
    public:
        Serializer() { m_byteorder = LittleEndian; }
        ~Serializer() {}

        Serializer(StreamBuffer dev, int byteorder = LittleEndian) {
            m_byteorder = byteorder;
            m_iodevice = dev;
        }

    public:
        enum ByteOrder { // 大小端的问题
            BigEndian,
            LittleEndian
        };

    public:
        void reset() {
            m_iodevice.reset();
        }

        int size() {
            return m_iodevice.size();
        }

        void skip_raw_data(int k) {
            m_iodevice.offset(k);
        }

        const char* data() {
            return m_iodevice.data();
        }

        void byte_orser(char *in, int len) {
            if (m_byteorder == BigEndian) {
                reverse(in, in+len);
            }
        }

        void write_raw_data(char *in, int len) {
            m_iodevice.input(in, len);
            m_iodevice.offset(len);
        }

        const char* current() {
            return m_iodevice.current();
        }

        void clear(){
            m_iodevice.clear();
            reset();
        }

        template <typename T>
        void output_type(T& t);

        template <typename T>
        void input_type(T t);

        // 给一个长度返回当前位置以后x个字节数据
        void get_length_mem(char *p, int len){
            memcpy(p, m_iodevice.current(), len);
            m_iodevice.offset(len);
        }

    public:
        template<typename Tuple,>

        template<typename T>
        Serializer &operator >> (T& i){
            output_type(i);
        }

    private:
        int m_byteorder;
        StreamBuffer m_iodevice;
};

template <typename T>
inline void Serializer::output_type(T& t){
    int len = sizeof(T);
    char *d = new char[len];

    if(!m_iodevice.is_eof()) {
        memcpy(d, m_iodevice.current(), len);
        m_iodevice.offset(len);
        byte_orser(d, len);
        t = *reinterpret_cast<T*>(&d[0]);
    }
    delete []d;
}

template<>
inline void Serializer::output_type(std::string& in){

}

template<typename T>
inline void Serializer::input_type(T t) {
    int len = sizeof(T);
    char *d = new char[len];
    const char *p = reinterpret_cast<const char *>(&t);
    memcpy(d, p, len);
    byte_orser(d, len);
    m_iodevice.input(d, len);
    delete [] d;
}