#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>

#include "../dev/ioctl.h"

#define IOCTL_DRIVER_NAME "/dev/ioctl"

int open_driver(const char* driver_name);
void close_driver(const char* driver_name, int fd_driver);

int open_driver(const char* driver_name) {

    printf("* Open Driver\n");

    int fd_driver = open(driver_name, O_RDWR);
    if (fd_driver == -1) {
        printf("ERROR: could not open \"%s\".\n", driver_name);
        printf("    errno = %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

	return fd_driver;
}

void close_driver(const char* driver_name, int fd_driver) {

    printf("* Close Driver\n");

    int result = close(fd_driver);
    if (result == -1) {
        printf("ERROR: could not close \"%s\".\n", driver_name);
        printf("    errno = %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}


int main(void) {

  int fd_ioctl = open_driver(IOCTL_DRIVER_NAME);
  struct timespec start, end;
  uint32_t request_num = 1000000;

  clock_gettime(CLOCK_REALTIME, &start);

  for (uint32_t i = 0; i < request_num; i++) {
      uint32_t inode_num = i;
      if (ioctl(fd_ioctl, IOCTL_BASE_GET_MUIR, &inode_num) < 0) {
          perror("Error ioctl PL_AXI_DMA_GET_NUM_DEVICES");
          exit(EXIT_FAILURE);
      }
  }

  clock_gettime(CLOCK_REALTIME, &end);

  long run_time = 1000000000 * (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec);
  printf("Average latency: %f nsec\n", (double)run_time / request_num);

	close_driver(IOCTL_DRIVER_NAME, fd_ioctl);

	return EXIT_SUCCESS;
}


