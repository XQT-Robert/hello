#ifndef IPC_COMMON_H
#define IPC_COMMON_H

#include <semaphore.h>
#include <stddef.h>
#include <stdint.h>

#define SHM_NAME "/ipc_shm_test"
#define DEFAULT_PORT 9090
#define DEFAULT_TOTAL_MB 256
#define DEFAULT_CHUNK_KB 64
#define SHM_BUFFER_SIZE (1U << 20)

typedef struct {
  sem_t can_write;
  sem_t can_read;
  size_t chunk_size;
  size_t total_bytes;
  int done;
  char buffer[SHM_BUFFER_SIZE];
} shm_region_t;

#endif
