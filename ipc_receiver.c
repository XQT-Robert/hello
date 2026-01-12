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
          "  %s socket [port]\n"
          "  %s shm\n",
          prog, prog);
}

static double elapsed_seconds(struct timespec start, struct timespec end) {
  return (double)(end.tv_sec - start.tv_sec) +
         (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

static uint64_t consume_buffer(const char *buffer, size_t size) {
  uint64_t sum = 0;
  for (size_t i = 0; i < size; ++i) {
    sum += (unsigned char)buffer[i];
  }
  return sum;
}

static int recv_all(int fd, void *data, size_t size) {
  char *ptr = data;
  size_t remaining = size;
  while (remaining > 0) {
    ssize_t received = recv(fd, ptr, remaining, 0);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (received == 0) {
      return -1;
    }
    ptr += (size_t)received;
    remaining -= (size_t)received;
  }
  return 0;
}

static int run_socket(uint16_t port) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(server_fd);
    return 1;
  }

  if (listen(server_fd, 1) < 0) {
    perror("listen");
    close(server_fd);
    return 1;
  }

  printf("Waiting for connection on port %u...\n", port);
  int client_fd = accept(server_fd, NULL, NULL);
  if (client_fd < 0) {
    perror("accept");
    close(server_fd);
    return 1;
  }

  uint64_t total_bytes = 0;
  uint32_t chunk_size = 0;
  if (recv_all(client_fd, &total_bytes, sizeof(total_bytes)) != 0 ||
      recv_all(client_fd, &chunk_size, sizeof(chunk_size)) != 0) {
    perror("recv header");
    close(client_fd);
    close(server_fd);
    return 1;
  }

  char *buffer = malloc(chunk_size);
  if (!buffer) {
    fprintf(stderr, "malloc failed\n");
    close(client_fd);
    close(server_fd);
    return 1;
  }

  struct timespec start;
  struct timespec end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  size_t remaining = (size_t)total_bytes;
  uint64_t checksum = 0;
  while (remaining > 0) {
    size_t recv_size = remaining < chunk_size ? remaining : chunk_size;
    if (recv_all(client_fd, buffer, recv_size) != 0) {
      perror("recv");
      free(buffer);
      close(client_fd);
      close(server_fd);
      return 1;
    }
    checksum += consume_buffer(buffer, recv_size);
    remaining -= recv_size;
  }

  clock_gettime(CLOCK_MONOTONIC, &end);

  double seconds = elapsed_seconds(start, end);
  double mb = (double)total_bytes / (1024.0 * 1024.0);
  printf("Copy (socket) received %.2f MB in %.3f s (%.2f MB/s), checksum=%lu\n",
         mb, seconds, mb / seconds, (unsigned long)checksum);

  free(buffer);
  close(client_fd);
  close(server_fd);
  return 0;
}

static int run_shm(void) {
  int fd = -1;
  for (int i = 0; i < 50; ++i) {
    fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd >= 0) {
      break;
    }
    usleep(100000);
  }
  if (fd < 0) {
    perror("shm_open");
    return 1;
  }

  shm_region_t *region = mmap(NULL, sizeof(shm_region_t),
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (region == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  struct timespec start;
  struct timespec end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  uint64_t checksum = 0;
  while (1) {
    sem_wait(&region->can_read);
    if (region->done) {
      sem_post(&region->can_write);
      break;
    }
    size_t size = region->chunk_size;
    checksum += consume_buffer(region->buffer, size);
    sem_post(&region->can_write);
  }

  clock_gettime(CLOCK_MONOTONIC, &end);

  double seconds = elapsed_seconds(start, end);
  double mb = (double)region->total_bytes / (1024.0 * 1024.0);
  printf("Zero-copy-ish (shared memory) received %.2f MB in %.3f s (%.2f MB/s), checksum=%lu\n",
         mb, seconds, mb / seconds, (unsigned long)checksum);

  munmap(region, sizeof(shm_region_t));
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "socket") == 0) {
    uint16_t port = DEFAULT_PORT;
    if (argc > 2) {
      port = (uint16_t)strtoul(argv[2], NULL, 10);
    }
    return run_socket(port);
  }

  if (strcmp(argv[1], "shm") == 0) {
    return run_shm();
  }

  usage(argv[0]);
  return 1;
}
