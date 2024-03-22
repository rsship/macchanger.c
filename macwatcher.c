#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <linux/errqueue.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PACKET_SIZE 64
#define DEFAULT_HOST "google.com"
#define SENDER_LIMIT 10

#define free_defer(value)                                                      \
  do {                                                                         \
    result = (value);                                                          \
    goto defer;                                                                \
  } while (0)

typedef struct {
  int sock;
  struct sockaddr_in to;
  struct sockaddr_in from;
  char *interface_name;
} Echo;

int sender_count;

typedef struct {
  struct icmphdr hdr;
  char msg[PACKET_SIZE - sizeof(struct icmphdr)];
} Ping_Packet;

void usage() {
  printf("Net Watcher\n");
  printf("    Usage: netwatcher <INTERFACE_NAME> OPTIONS \n");
  printf("           Options:\n");
  printf("                   <REMOTE_HOST> -> remove host of your choice "
         "default one is google.com");
}

char *shift_args(int *argc, char ***argv) {
  assert(*argc > 0);
  char *result = **argv;
  *argc -= 1;
  *argv += 1;
  return result;
}

u_char *get_mac_linux(int sock, const char *ifname) {
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, ifname);
  int ret = ioctl(sock, SIOCGIFHWADDR, &ifr);
  if (ret < 0) {
    perror("could not get mac address");
    close(sock);
    exit(1);
  }
  unsigned char *mac = (unsigned char *)ifr.ifr_ifru.ifru_hwaddr.sa_data;
  return mac;
}

int set_mac_linux(int sockfd, const char *interface_name, u_char mac[]) {
  struct ifreq ifr;
  // TURN DOWN INTERFACE;
  strncpy(ifr.ifr_name, interface_name, IFNAMSIZ);
  if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
    perror("could not turn down");
    close(sockfd);
    return -1;
  }

  ifr.ifr_flags &= ~IFF_UP;
  if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
    perror("could not set flags down");
    close(sockfd);
    return -1;
  }

  ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
  memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
  if (ioctl(sockfd, SIOCSIFHWADDR, &ifr) < 0) {
    perror("could not set new mac");
    close(sockfd);
    return -1;
  }
  printf("MAC ADDRESS SET DONE\n");
  // TURN UP INTERFACE AGAIN
  if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
    perror("could not back up");
    close(sockfd);
    return -1;
  }

  ifr.ifr_flags |= IFF_UP;
  if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
    perror("could not set up flags");
    close(sockfd);
    return -1;
  }
  close(sockfd);
  return 0;
}

typedef struct {
  pthread_t thread_id;
  int thread_num;
} Thread_Info;

// RFC 1071
uint16_t checksum(uint16_t *addr, int len) {
  int count = len;
  register uint32_t sum = 0;
  uint16_t answer = 0;
  while (count > 1) {
    sum += *(addr++);
    count -= 2;
  }

  if (count > 0) {
    sum += *(uint8_t *)addr;
  }

  while (sum >> 16) {
    sum = (sum & 0xffff) + (sum >> 16);
  }

  answer = ~sum;
  return (answer);
}

void *main_loop(void *icmp_raw) {
  Echo echo = *(Echo *)icmp_raw;
  char packet[PACKET_SIZE];
  memset(&packet, 0, PACKET_SIZE);

  Ping_Packet *ping_pkt = (Ping_Packet *)packet;
  ping_pkt->hdr.type = ICMP_ECHO;
  ping_pkt->hdr.code = 0;
  ping_pkt->hdr.un.echo.sequence = htons(1);
  ping_pkt->hdr.un.echo.id = htons(getpid());
  ping_pkt->hdr.checksum = checksum((uint16_t *)&packet, PACKET_SIZE);

  int udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  srand(time(NULL));
  u_char mac[] = {0x00, 0x16, 0x3E, 0x33, 0x44, 0x66};
  for (int i = 2; i < 6; ++i) {
    mac[i] = rand() % 256;
  }

  bool flag = 1;
  while (flag) {
    sleep(1);

    if (sendto(echo.sock, packet, PACKET_SIZE, 0, (struct sockaddr *)&echo.to,
               sizeof(echo.to)) < 0) {
      perror("sendto:");
    }
    // Type
    //    8 for echo message;
    //    0 for echo reply message.
    Ping_Packet *ping_pkt = (Ping_Packet *)packet;
    if (ping_pkt->hdr.type == 8) {
      printf("sending ICMP MSG\n");
      sender_count += 1;
    }
    if (sender_count > SENDER_LIMIT) {
      /// DESTINATION UNREACHABLE
      /// that's maybed caused by no doing mac randomization
      /// so let's randomize the mac then
      if (get_mac_linux(udp_sock, echo.interface_name) == NULL) {
        printf("COULD NOT GET MAC ADDRESS");
      }
      if (set_mac_linux(udp_sock, echo.interface_name, mac) < 0) {
        printf("COULD NOT SET MAC ADDRESS");
      }
      sender_count = 0;
    }
  }
  close(udp_sock);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    exit(1);
  }
  const char *program = shift_args(&argc, &argv);
  char *interface_name = shift_args(&argc, &argv);
  const char *optional_host = shift_args(&argc, &argv);
  optional_host = (!optional_host) ? DEFAULT_HOST : optional_host;

  printf("RUNNING %s\n", program);

  Echo echo = {0};
  echo.interface_name = interface_name;
  int result = 0;

#ifdef __linux__
  echo.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
  if (echo.sock < 0) {
    perror("socket:");
    free_defer(1);
  }
  int ttl_val = 64;
  if (setsockopt(echo.sock, SOL_IP, IP_TTL, &ttl_val, sizeof(ttl_val)) < 0) {
    printf("Settting socket options to TLL failed\n");
    free_defer(1);
  }

  struct hostent *hp = gethostbyname(optional_host);
  if (hp == NULL) {
    perror("gethostbyname:");
    free_defer(1);
  }
  memset(&echo.to, 0, sizeof(echo.to));
  echo.to.sin_family = AF_INET;
  memcpy(&echo.to.sin_addr.s_addr, hp->h_addr_list[0], 4);

  char buffer[PACKET_SIZE];
  socklen_t from_len = sizeof(echo.from);
  Thread_Info t_info = {0};

  int rc = pthread_create(&t_info.thread_id, NULL, main_loop, (void *)&echo);
  if (rc) {
    printf("Could not start new thread\n");
    free_defer(1);
  }
  // #define	EAGAIN		11	/* Try again */
  // #define	ENOMEM		12	/* Out of memory */
  while (1) {
    // TODO: what if its an hotspot conneciton, hotspot connections just are

    // Message Type 3 (the Most Important one)
    // Type
    //    3
    // Code
    //    0 = net unreachable;
    //    1 = host unreachable;
    //    2 = protocol unreachable;
    //    3 = port unreachable;
    //    4 = fragmentation needed and DF set;
    //    5 = source route failed.

    if (recvfrom(echo.sock, buffer, sizeof(buffer), 0,
                 (struct sockaddr *)&echo.from, &from_len) < 0) {
      printf("recvfrom errno %d\n", errno);
      if (errno == EAGAIN || errno == EINTR)
        printf("got an error try again!");
      continue;
    }

    Ping_Packet *ping_pkg = (Ping_Packet *)buffer;
    if (ping_pkg->hdr.type == 3) {
      switch (ping_pkg->hdr.code) {
      case 0:
        printf("Net Unreachable\n");
        break;
      case 1:
        printf("Host Unreachable\n");
        break;
      case 2:
        printf("Protocol Unreachable\n");
        break;
      case 3:
        printf("Port Unreachable\n");
        break;
      case 5:
        printf("Source Route Failed\n");
        break;
      default:
        printf("Destination Unreacable");
        break;
      }
    }
    if (ping_pkg->hdr.type == 0) {
      printf("ICMP MSG RECEIVED\n");
      sender_count = 0;
    }
  }

  if (pthread_join(t_info.thread_id, NULL) < 0) {
    printf("could not join thread\n");
    free_defer(1);
  }
#endif /* ifdef __linux__*/

#ifdef __APPLE__
  assert(0 && "NOT IMPLEMENTED YET ON MACOS");
#endif /* ifdef _APPLE__ */

#ifdef __WIN32
  assert(0 && "NOT IMPLEMENTED YET ON WINDOWS");
#endif /* ifdef __WIN32__*/
  close(echo.sock);
  return 0;

defer:
  close(echo.sock);
  return result;
}
