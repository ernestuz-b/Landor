#include <iostream>
#include "include/managed_heap/managed_heap.hpp"

// todo: this is just for the time being, it'll depend on the platform ~ern
alignas(std::max_align_t) std::byte heap_store[64*1024]; //64K should be enough for everyone, sure
auto heap = managed_heap::create(heap_store);

int main()
{
    if(!heap)
    {
        std::cerr << "Error: failed to initialize memory" << std::endl;
        return 1;
    }
    std::cout << "Memory system initialized, usable " << heap.capacity_bytes << " bytes." << std::endl;
    return 0;
}
