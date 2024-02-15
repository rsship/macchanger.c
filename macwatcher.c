#include <assert.h>
#include <linux/sockios.h>
#include <math.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

unsigned char *get_mac(int sock, const char *ifname) {
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

char *shift_args(int *argc, char ***argv) {
  assert(*argc > 0);
  char *result = **argv;
  *argc -= 1;
  *argv += 1;
  return result;
}

int main(int argc, char **argv) {
  const char *program = shift_args(&argc, &argv);
  const char *interface_name = shift_args(&argc, &argv);
  // const char *interface_name = "wlx90de80910f38";
  printf("RUNNING %s\n", program);

  srand(time(NULL));

  struct ifreq ifr;

  int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0) {
    printf("could not create socket\n");
    return 0;
  }

  unsigned char *cur_mac = get_mac(sockfd, interface_name);
  printf("CURRENT MAC ADDRESS\n");
  printf("%02X:%02X:%02X:%02X:%02X:%02X\n", cur_mac[0], cur_mac[1], cur_mac[2],
         cur_mac[3], cur_mac[4], cur_mac[5]);

  // TURN DOWN INTERFACE;
  strncpy(ifr.ifr_name, interface_name, IFNAMSIZ);
  if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
    printf("could not turn down %s\n", interface_name);
    close(sockfd);
    return 0;
  }

  ifr.ifr_flags &= ~IFF_UP;
  if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
    printf("could not set flags dowl %s\n", interface_name);
    close(sockfd);
    return 0;
  }

  ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
  unsigned char new_mac[] = {0x00, 0x16, 0x3E, 0x33, 0x44, 0x66};

  for (int i = 3; i < 6; ++i) {
    new_mac[i] = rand() % 256;
  }

  memcpy(ifr.ifr_hwaddr.sa_data, new_mac, 6);
  if (ioctl(sockfd, SIOCSIFHWADDR, &ifr) < 0) {
    printf("could not set new mac %s\n", interface_name);
    close(sockfd);
    return 0;
  }

  printf("SETTED MAC ADDRESS\n");
  printf("%02X:%02X:%02X:%02X:%02X:%02X\n", new_mac[0], new_mac[1], new_mac[2],
         new_mac[3], new_mac[4], new_mac[5]);

  printf("MAC ADDRESS SET DONE\n");

  // TURN UP INTERFACE AGAIN
  if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
    printf("could not back up %s\n", interface_name);
    close(sockfd);
    return 0;
  }

  ifr.ifr_flags |= IFF_UP;
  if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
    printf("could not set up flags %s\n", interface_name);
    close(sockfd);
    return 0;
  }

  close(sockfd);
  return 0;
}
