#include "ipc_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage:\n"
          "  %s socket <host> [port] [total_mb] [chunk_kb]\n"
          "  %s shm [total_mb] [chunk_kb]\n",
          prog, prog);
}

static uint64_t to_uint64(const char *value, uint64_t fallback) {
  if (!value) {
    return fallback;
  }
  char *end = NULL;
  uint64_t result = strtoull(value, &end, 10);
  if (!end || *end != '\0') {
    return fallback;
  }
  return result;
}

static void fill_pattern(char *buffer, size_t size, uint8_t seed) {
  for (size_t i = 0; i < size; ++i) {
    buffer[i] = (char)(seed + (uint8_t)i);
  }
}

static int send_all(int fd, const void *data, size_t size) {
  const char *ptr = data;
  size_t remaining = size;
  while (remaining > 0) {
    ssize_t sent = send(fd, ptr, remaining, 0);
    if (sent < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    ptr += (size_t)sent;
    remaining -= (size_t)sent;
  }
  return 0;
}

static int run_socket(const char *host, uint16_t port, size_t total_bytes,
                      size_t chunk_size) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    fprintf(stderr, "Invalid host: %s\n", host);
    close(fd);
    return 1;
  }

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    close(fd);
    return 1;
  }

  uint64_t header_total = (uint64_t)total_bytes;
  uint32_t header_chunk = (uint32_t)chunk_size;
  if (send_all(fd, &header_total, sizeof(header_total)) != 0 ||
      send_all(fd, &header_chunk, sizeof(header_chunk)) != 0) {
    perror("send header");
    close(fd);
    return 1;
  }

  char *buffer = malloc(chunk_size);
  if (!buffer) {
    fprintf(stderr, "malloc failed\n");
    close(fd);
    return 1;
  }

  size_t remaining = total_bytes;
  uint8_t seed = 0;
  while (remaining > 0) {
    size_t send_size = remaining < chunk_size ? remaining : chunk_size;
    fill_pattern(buffer, send_size, seed++);
    if (send_all(fd, buffer, send_size) != 0) {
      perror("send");
      free(buffer);
      close(fd);
      return 1;
    }
    remaining -= send_size;
  }

  free(buffer);
  close(fd);
  return 0;
}

static int run_shm(size_t total_bytes, size_t chunk_size) {
  if (chunk_size > SHM_BUFFER_SIZE) {
    fprintf(stderr, "chunk_kb too large (max %u)\n", SHM_BUFFER_SIZE);
    return 1;
  }

  shm_unlink(SHM_NAME);
  int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
  if (fd < 0) {
    perror("shm_open");
    return 1;
  }
  if (ftruncate(fd, sizeof(shm_region_t)) != 0) {
    perror("ftruncate");
    close(fd);
    return 1;
  }

  shm_region_t *region = mmap(NULL, sizeof(shm_region_t),
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (region == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  if (sem_init(&region->can_write, 1, 1) != 0 ||
      sem_init(&region->can_read, 1, 0) != 0) {
    perror("sem_init");
    munmap(region, sizeof(shm_region_t));
    return 1;
  }

  region->total_bytes = total_bytes;
  region->chunk_size = chunk_size;
  region->done = 0;

  size_t remaining = total_bytes;
  uint8_t seed = 0;
  while (remaining > 0) {
    size_t send_size = remaining < chunk_size ? remaining : chunk_size;
    sem_wait(&region->can_write);
    fill_pattern(region->buffer, send_size, seed++);
    region->chunk_size = send_size;
    region->done = 0;
    sem_post(&region->can_read);
    remaining -= send_size;
  }

  sem_wait(&region->can_write);
  region->done = 1;
  sem_post(&region->can_read);

  munmap(region, sizeof(shm_region_t));
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  size_t total_bytes = (size_t)DEFAULT_TOTAL_MB * 1024 * 1024;
  size_t chunk_size = (size_t)DEFAULT_CHUNK_KB * 1024;

  if (strcmp(argv[1], "socket") == 0) {
    if (argc < 3) {
      usage(argv[0]);
      return 1;
    }
    const char *host = argv[2];
    uint16_t port = (uint16_t)to_uint64(argc > 3 ? argv[3] : NULL, DEFAULT_PORT);
    total_bytes = (size_t)to_uint64(argc > 4 ? argv[4] : NULL,
                                    (uint64_t)DEFAULT_TOTAL_MB) *
                  1024 * 1024;
    chunk_size = (size_t)to_uint64(argc > 5 ? argv[5] : NULL,
                                   (uint64_t)DEFAULT_CHUNK_KB) *
                 1024;
    return run_socket(host, port, total_bytes, chunk_size);
  }

  if (strcmp(argv[1], "shm") == 0) {
    total_bytes = (size_t)to_uint64(argc > 2 ? argv[2] : NULL,
                                    (uint64_t)DEFAULT_TOTAL_MB) *
                  1024 * 1024;
    chunk_size = (size_t)to_uint64(argc > 3 ? argv[3] : NULL,
                                   (uint64_t)DEFAULT_CHUNK_KB) *
                 1024;
    return run_shm(total_bytes, chunk_size);
  }

  usage(argv[0]);
  return 1;
}
