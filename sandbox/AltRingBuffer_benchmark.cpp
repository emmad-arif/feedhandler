#include "AltRingBuffer.h"
#include <iostream>

void func() {
    AltRingBuffer<double>* buffer = new AltRingBuffer<double>(10);
    std::cout << "size: " << buffer->size() << ", capacity: " << buffer->capacity() << std::endl;
    delete buffer;
}


int main() {
    func();
}
