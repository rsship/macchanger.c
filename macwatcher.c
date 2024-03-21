#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <linux/errqueue.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define PACKET_SIZE 64
#define DEFAULT_HOST "google.com"

#define free_defer(value)                                                      \
  do {                                                                         \
    result = (value);                                                          \
    goto defer;                                                                \
  } while (0)

typedef struct {
  int sock;
  struct sockaddr_in to;
  struct sockaddr_in from;
  int sender_count;
} Echo;

typedef struct {
  struct icmphdr hdr;
  char msg[PACKET_SIZE - sizeof(struct icmphdr)];
} Ping_Packet;

void usage() 
{
  printf("Net Watcher\n");
  printf("    Usage: netwatcher <INTERFACE_NAME> OPTIONS \n");
  printf("           Options:\n");
  printf("                   <REMOTE_HOST> -> remove host of your choice "
         "default one is google.com");
}

char *shift_args(int *argc, char ***argv) 
{
  assert(*argc > 0);
  char *result = **argv;
  *argc -= 1;
  *argv += 1;
  return result;
}

u_char *get_mac_linux(int sock, const char *ifname) 
{
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, ifname);
  int ret = ioctl(sock, SIOCGIFHWADDR, &ifr);
  if (ret < 0) {
    printf("could not sent ioctl\n");
    close(sock);
    exit(1);
  }
  unsigned char *mac = (unsigned char *)ifr.ifr_ifru.ifru_hwaddr.sa_data;
  return mac;
}

int set_mac_linux(int sockfd, const char *interface_name) 
{
  struct ifreq ifr;
  // TURN DOWN INTERFACE;
  printf("TURNING DOWN WI-FI INTERFACE\n");
  strncpy(ifr.ifr_name, interface_name, IFNAMSIZ);
  if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0)
    ifr.ifr_flags &= ~IFF_UP;
  if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
    printf("could not set flags dowl %s\n", interface_name);
    close(sockfd);
    return 1;
  }

  ifr.ifr_hwaddr.sa_family = AF_INET;
  unsigned char new_mac[] = {0x00, 0x16, 0x3E, 0x33, 0x44, 0x66};

  for (int i = 3; i < 6; ++i) {
    new_mac[i] = rand() % 256;
  }

  memcpy(ifr.ifr_hwaddr.sa_data, new_mac, 6);
  if (ioctl(sockfd, SIOCSIFHWADDR, &ifr) < 0) {
    printf("could not set new mac %s\n", interface_name);
    close(sockfd);
    return 1;
  }

  printf("SETTED MAC ADDRESS\n");
  printf("%02X:%02X:%02X:%02X:%02X:%02X\n", new_mac[0], new_mac[1], new_mac[2],
         new_mac[3], new_mac[4], new_mac[5]);

  // TURN UP INTERFACE AGAIN
  printf("TURNING UP WI-FI INTERFACE\n");
  ifr.ifr_flags |= IFF_UP;
  if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
    printf("could not set up flags %s\n", interface_name);
    close(sockfd);
    return 1;
  }

  return 0;
}

typedef struct {
  pthread_t thread_id;
  int thread_num;
} Thread_Info;

void *main_loop(void *icmp_raw) 
{

  Echo echo = *(Echo *)icmp_raw;
  char packet[PACKET_SIZE];
  memset(&packet, 0, PACKET_SIZE);

  Ping_Packet *ping_pkt = (Ping_Packet *)packet;
  ping_pkt->hdr.type = ICMP_ECHO;
  ping_pkt->hdr.code = 0;
  ping_pkt->hdr.un.echo.sequence = htons(1);
  ping_pkt->hdr.un.echo.id = htons(getpid());

  while (1) {
    sleep(1);
    if (sendto(echo.sock, packet, PACKET_SIZE, 0, (struct sockaddr *)&echo.to,
               sizeof(echo.to)) < 0) {
      perror("sendto:");
      printf("errno %d\n", errno);
    }
    // Type
    //    8 for echo message;
    //    0 for echo reply message.

    printf("Send Echo message\n");
    echo.sender_count += 1;
  }
  return NULL;
}

int main(int argc, char **argv) 
{
  if (argc < 2) {
    usage();
    exit(1);
  }
  const char *program = shift_args(&argc, &argv);
  const char *interface_name = shift_args(&argc, &argv);
  const char *optional_host = shift_args(&argc, &argv);
  optional_host = (!optional_host) ? DEFAULT_HOST : optional_host;

  printf("RUNNING %s\n", program);

  Echo echo = {0};
  int result = 0;

#ifdef __linux__
  echo.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
  if (echo.sock < 0) {
    perror("socket:");
    free_defer(1);
  }
  srand(time(NULL));

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
      printf("Received Echo Reply Msg\n");
    }
  }

  if (pthread_join(t_info.thread_id, NULL) < 0) {
    printf("could not join thread\n");
    free_defer(1);
  }
  close(echo.sock);
  return 0;

  unsigned char *cur_mac = get_mac_linux(echo.sock, interface_name);
  if (cur_mac == NULL) {
    printf("COULD NOT GET MAC ADDRESS");
    close(echo.sock);
    return 1;
  }
  printf("CURRENT MAC ADDRESS\n");
  printf("%02X:%02X:%02X:%02X:%02X:%02X\n", cur_mac[0], cur_mac[1], cur_mac[2],
         cur_mac[3], cur_mac[4], cur_mac[5]);

  if (set_mac_linux(echo.sock, interface_name) < 0) {
    printf("COULD NOT SET MAC ADDRESS");
    close(echo.sock);
    return 1;
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
