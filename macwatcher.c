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
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT 1025
u_char outpack[0x10000];

void usage() {
  printf("Net Watcher\n");
  printf("    Usage: netwatcher <INTERFACE_NAME>\n");
}

char *shift_args(int *argc, char ***argv) {
  assert(*argc > 0);
  char *result = **argv;
  *argc -= 1;
  *argv += 1;
  return result;
}

unsigned char *get_mac_linux(int sock, const char *ifname) {
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

int set_mac_linux(int sockfd, const char *interface_name) {
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
  int pid;
  struct sockaddr_in *sock_addr;
} IP_Data;

void *main_loop(void *vargp) {
  IP_Data *ip_data = (IP_Data *)vargp;
  struct cmsghdr *cmsg;
  struct msghdr mh;
  static struct iovec iov = {outpack, 0};

  mh.msg_name = (caddr_t)&ip_data->sock_addr;
  mh.msg_namelen = sizeof(ip_data->sock_addr);
  mh.msg_iov = &iov;
  mh.msg_iovlen = sizeof(iov);

  for (;;) {
    do {
      sleep(2);
      /*
        this area for msg creation
      */
      if (sendmsg(ip_data->pid, &mh, 0) < 0) {
        perror("sendmsg():");
      }
    } while (0);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    exit(1);
  }

  const char *program = shift_args(&argc, &argv);
  const char *interface_name = shift_args(&argc, &argv);
  printf("RUNNING %s\n", program);

#ifdef __linux__
  int lissock = socket(AF_INET, SOCK_DGRAM, 0);
  if (lissock < 0) {
    printf("could not create socket\n");
    return 1;
  }
  srand(time(NULL));

  struct sockaddr_in whereto;
  memset(&whereto, 0, sizeof(whereto));
  whereto.sin_port = htons(PORT);
  whereto.sin_family = AF_INET;

  char *google = "google.com";
  struct hostent *hp = gethostbyname(google);
  if (hp == NULL) {
    close(lissock);
    printf("Connection error\n");
    return 1;
  }
  memcpy(&whereto.sin_addr, hp->h_addr_list[0], sizeof(whereto.sin_addr));

  pthread_t thread_id;

  IP_Data ip_data = {
      .pid = lissock,
      .sock_addr = &whereto,
  };
  pthread_create(&thread_id, NULL, main_loop, (void *)&ip_data);

  // TODO: read msg from sender;
  //
  pthread_join(thread_id, NULL);
  close(lissock);
  return 0;

  unsigned char *cur_mac = get_mac_linux(lissock, interface_name);
  if (cur_mac == NULL) {
    printf("COULD NOT GET MAC ADDRESS");
    close(lissock);
    return 1;
  }
  printf("CURRENT MAC ADDRESS\n");
  printf("%02X:%02X:%02X:%02X:%02X:%02X\n", cur_mac[0], cur_mac[1], cur_mac[2],
         cur_mac[3], cur_mac[4], cur_mac[5]);

  if (set_mac_linux(lissock, interface_name) < 0) {
    printf("COULD NOT SET MAC ADDRESS");
    close(lissock);
    return 1;
  }

#endif /* ifdef __linux__*/

#ifdef __APPLE__
  assert(0 && "NOT IMPLEMENTED YET ON MACOS");
#endif /* ifdef _APPLE__ */

#ifdef __WIN32
  assert(0 && "NOT IMPLEMENTED YET ON WINDOWS");
#endif /* ifdef __WIN32__*/
  close(lissock);
  return 0;
}

// void receive_error_msg(int sockfd, struct sockaddr_in whereto) {
//   int res;
//   char cbuf[512];
//   struct iovec iov;
//   struct msghdr msg;
//   struct cmsghdr *cmsg;
//   struct sock_extended_err *e;
//   struct icmphdr icmph;
//   int net_errors = 0;
//   int local_errors = 0;
//   int saved_errno = errno;
//
//   iov.iov_base = &icmph;
//   iov.iov_len = sizeof(icmph);
//   msg.msg_name = (void *)&whereto;
//   msg.msg_namelen = sizeof(whereto);
//   msg.msg_iov = &iov;
//   msg.msg_iovlen = 1;
//   msg.msg_flags = 0;
//   msg.msg_control = cbuf;
//   msg.msg_controllen = sizeof(cbuf);
//
//   // NOTE: receive msg from ping
//   res = recvmsg(sockfd, &msg, MSG_ERRQUEUE | MSG_DONTWAIT);
//   if (res < 0) {
//     close(sockfd);
//     printf("recv msg abort\n");
//     abort();
//   }
//
//   // NOTE: iterate throught each msg and extract each of it type
//   for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
//     if (cmsg->cmsg_level == SOL_IP) {
//       if (cmsg->cmsg_type == IP_RECVERR) {
//         e = (struct sock_extended_err *)CMSG_DATA(cmsg);
//       }
//     }
//   }
//   if (e == NULL) {
//     abort();
//   }
//
//   switch (e->ee_type) {
//   case ICMP_ECHOREPLY:
//     printf("Echo Replay\n");
//     break;
//   case ICMP_DEST_UNREACH:
//     switch (e->ee_code) {
//     case ICMP_NET_UNREACH:
//       printf("Destination Net Unreachable\n");
//       break;
//     case ICMP_HOST_UNREACH:
//       printf("Destination Host unreachable\n");
//       break;
//     case ICMP_PORT_UNREACH:
//       printf("Destination Port unreachable\n");
//       break;
//     default:
//       printf("Dest Unreachable, Bad Code: %d\n", e->ee_code);
//       break;
//     }
//   case ICMP_ECHO:
//     printf("Echo Request\n");
//     break;
//   default:
//     printf("BAD ICMP TYPE: %d\n", e->ee_type);
//   }
// }
