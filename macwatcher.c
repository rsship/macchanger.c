#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <linux/errqueue.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <pthread.h>
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
  struct sockaddr_in sock_addr;
} ICMP;

void usage() 
{
  printf("Net Watcher\n");
  printf("    Usage: netwatcher <INTERFACE_NAME>\n");
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

// int receive_error_msg()
// {
//   return 0;
// }

typedef struct {
  pthread_t thread_id;
  int thread_num;
} Thread_Info;

void *main_loop(void *icmp_raw) 
{

  ICMP icmp = *(ICMP *)icmp_raw;
  char packet[PACKET_SIZE];
  memset(&packet, 0, PACKET_SIZE);

  struct icmphdr *icmp_hdr = (struct icmphdr *)packet;
  icmp_hdr->type = ICMP_ECHO;
  icmp_hdr->code = 0;
  icmp_hdr->un.echo.sequence = htons(1);
  icmp_hdr->un.echo.id = htons(getpid() & 0xFFFF);

  while (1) 
  {
    sleep(1);
    if (sendto(icmp.sock, packet, PACKET_SIZE, 0,
               (struct sockaddr *)&icmp.sock_addr,
               sizeof(icmp.sock_addr)) < 0) {
      perror("sendto:");
      // close(icmp.sock);
      // break;
    }
    printf("sending new ICMP message\n");
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
  const char *whereto = shift_args(&argc, &argv);
  whereto = (!whereto) ? DEFAULT_HOST : whereto;

  printf("RUNNING %s\n", program);

  ICMP icmp = {0};
  int result = 0;

#ifdef __linux__
  icmp.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
  if (icmp.sock < 0) {
    perror("socket:");
    free_defer(1);
  }
  srand(time(NULL));

  struct hostent *hp = gethostbyname(whereto);
  if (hp == NULL) {
    perror("gethostbyname:");
    free_defer(1);
  }
  memset(&icmp.sock_addr, 0, sizeof(icmp.sock_addr));
  icmp.sock_addr.sin_family = AF_INET;
  memcpy(&icmp.sock_addr.sin_addr.s_addr, hp->h_addr_list[0], 4);
  // char *tmp =  "142.250.187.142";
  // memcpy(&icmp.sock_addr.sin_addr.s_addr, tmp, 4);

  char buffer[1024];
  struct sockaddr_in from;
  socklen_t from_len = sizeof(from);

  Thread_Info t_info = {0};

  int rc = pthread_create(&t_info.thread_id, NULL, main_loop, (void *)&icmp);
  if (rc) {
    printf("Could not start new thread\n");
    free_defer(1);
  }

  // #define	EAGAIN		11	/* Try again */
  // #define	ENOMEM		12	/* Out of memory */

  while (1) {
    if (recvfrom(icmp.sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&from,
                 &from_len) > 0) {
      printf("Received ICMP echo reply\n");
    } else {
      if (errno == EAGAIN || errno == EINTR) break;
      else {
      }
    }
  }

  if (pthread_join(t_info.thread_id, NULL) < 0) {
    printf("could not join thread\n");
    free_defer(1);
  }


  close(icmp.sock);
  return 0;

  unsigned char *cur_mac = get_mac_linux(icmp.sock, interface_name);
  if (cur_mac == NULL) {
    printf("COULD NOT GET MAC ADDRESS");
    close(icmp.sock);
    return 1;
  }
  printf("CURRENT MAC ADDRESS\n");
  printf("%02X:%02X:%02X:%02X:%02X:%02X\n", cur_mac[0], cur_mac[1], cur_mac[2],
         cur_mac[3], cur_mac[4], cur_mac[5]);

  if (set_mac_linux(icmp.sock, interface_name) < 0) {
    printf("COULD NOT SET MAC ADDRESS");
    close(icmp.sock);
    return 1;
  }

#endif /* ifdef __linux__*/

#ifdef __APPLE__
  assert(0 && "NOT IMPLEMENTED YET ON MACOS");
#endif /* ifdef _APPLE__ */

#ifdef __WIN32
  assert(0 && "NOT IMPLEMENTED YET ON WINDOWS");
#endif /* ifdef __WIN32__*/
  close(icmp.sock);
  return 0;

defer:
  close(icmp.sock);
  return result;
}
