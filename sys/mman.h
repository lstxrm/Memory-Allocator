// Windows shim so the unmodified allocator.h (which does #include "sys/mman.h")
// compiles on Windows. On Linux/macOS it simply forwards to the real header.
#pragma once
#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <cstddef>
  #define PROT_READ      0x1
  #define PROT_WRITE     0x2
  #define MAP_SHARED     0x01
  #define MAP_PRIVATE    0x02
  #define MAP_ANONYMOUS  0x20
  #define MAP_FAILED     ((void*)-1)
  inline void* mmap(void*, size_t length, int, int, int, long long) {
      void* p = VirtualAlloc(NULL, length, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      return p ? p : MAP_FAILED;
  }
  inline int munmap(void* addr, size_t) { return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1; }
#else
  #include_next <sys/mman.h>
#endif