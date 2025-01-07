#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
bool nread(int fd, int len, uint8_t *buf) {
  int total = 0, bytes_read;
  while (total < len) {
    bytes_read = read(fd, buf + total, len - total);
    if (bytes_read <= 0) {
      return false;
    }
    total += bytes_read;
  }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
bool nwrite(int fd, int len, uint8_t *buf) {
  int total = 0, bytes_written;
  while (total < len) {
    bytes_written = write(fd, buf + total, len - total);
    if (bytes_written <= 0) {
      return false;
    }
    total += bytes_written;
  }
  return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
bool recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block) {
  uint8_t buffer[HEADER_LEN + JBOD_BLOCK_SIZE];
  if (!nread(fd, HEADER_LEN + JBOD_BLOCK_SIZE, buffer)) {
    return false;
  }
  memcpy(op, buffer, sizeof(uint32_t));
  *ret = buffer[4];
  if (*ret & 0x02) {
    memcpy(block, buffer + HEADER_LEN, JBOD_BLOCK_SIZE);
  }
  return true;
}

/* attempts to send a packet to sd; returns true on success and false on
 * failure */
bool send_packet(int fd, uint32_t op, uint8_t *block) {
  uint8_t buffer[HEADER_LEN + JBOD_BLOCK_SIZE] = {0};
  memcpy(buffer, &op, sizeof(uint32_t));
  if (block != NULL) {
    buffer[4] = 0x02;
    memcpy(buffer + HEADER_LEN, block, JBOD_BLOCK_SIZE);
  } else {
    buffer[4] = 0x00;
  }
  return nwrite(fd, HEADER_LEN + JBOD_BLOCK_SIZE, buffer);
}

/* connect to server and set the global client variable to the socket */
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in server_addr;
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd < 0) {
    perror("socket");
    return false;
  }
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
    perror("inet_pton");
    close(cli_sd);
    return false;
  }
  if (connect(cli_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    close(cli_sd);
    return false;
  }
  return true;
}

void jbod_disconnect(void) {
  if (cli_sd >= 0) {
    close(cli_sd);
    cli_sd = -1;
  }
}

int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint8_t res_ret;
  uint32_t res_op;
  if (!send_packet(cli_sd, op, block)) {
    return -1;
  }
  if (!recv_packet(cli_sd, &res_op, &res_ret, block)) {
    return -1;
  }
  return res_ret & 0x01 ? -1 : 0;
}
