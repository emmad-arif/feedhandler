#include <cstddef> 
#include <cstdlib>
#include <new>

template<typename T>
class AltRingBuffer {
public:
    explicit AltRingBuffer(size_t capacity) : capacity_(capacity) {
        //data = static_cast<T*>(::operator new(sizeof(T) * capacity));
        data_ = static_cast<T*>(std::aligned_alloc(alignof(T), sizeof(T) * capacity));

        if (!data_) {
            throw std::bad_alloc();
        }
    }

    AltRingBuffer(const AltRingBuffer& ringBugger) = delete;
    AltRingBuffer& operator=(const AltRingBuffer& ringBuffer) = delete; 

    ~AltRingBuffer() {
        std::free(data_);
    }

private:
    size_t start_ = 0;
    size_t end_ = 0;
    size_t capacity_;

    T* data_;
};