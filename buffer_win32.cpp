#include "buffer.hpp"
#include "system.hpp"

namespace jup {

/* TODO: Finish or remove

static constexpr u64 NIBUF_MAX_SIZE = 1024*1024*1024;

static void* nibuf_base_loc = (void*)0x40000000000;
static int nibuf_max_count = 1024;


void* Buffer_alloc::alloc_new(u64 min_size, u64* out_size) {
    assert(nibuf_max_count > 0);
    --nibuf_max_count;
    nibuf_base_loc += NIBUF_MAX_SIZE;

    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);

    // Round up to next page boundary
    u64 m = min_size % sysinfo.dwPageSize;
    if (m > 0) min_size += sysinfo.dwPageSize - m;
    
    void* ptr = VirtualAlloc(nibuf_base_loc, min_size, MEM_COMMIT, PAGE_READWRITE);
    assert_win(ptr);
            
}

void* Buffer_alloc::alloc_extend(void* base, u64 cur_size, u64 min_size, u64* out_size);
void Buffer_alloc::free(void* base)
    
*/

} /* end of namespace jup */
