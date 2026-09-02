/*
 * lwipopts.h — ToyOS lwIP (NO_SYS raw API, IPv4 only)
 */
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0

#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0
#define LWIP_NETIF_API              0

#define SYS_LIGHTWEIGHT_PROT        0

#define MEM_ALIGNMENT               4U
#define MEM_SIZE                    (32 * 1024)
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_RAW_PCB            2
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_TCP_SEG            16
#define MEMP_NUM_SYS_TIMEOUT        8
#define MEMP_NUM_NETBUF             0
#define MEMP_NUM_NETCONN            0
#define MEMP_NUM_TCPIP_MSG_API      0
#define MEMP_NUM_TCPIP_MSG_INPKT    0

#define PBUF_POOL_SIZE              24
#define PBUF_POOL_BUFSIZE           512

#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_DHCP                   0
#define LWIP_AUTOIP                 0
#define LWIP_DNS                    0
#define LWIP_IGMP                   0
#define LWIP_SNMP                   0
#define LWIP_STATS                  0

#define TCP_MSS                     536
#define TCP_SND_BUF                 2048
#define TCP_WND                     4096
#define TCP_SND_QUEUELEN            (4 * TCP_SND_BUF / TCP_MSS)

#define LWIP_ARP                    1
#define ARP_TABLE_SIZE              4
#define ARP_QUEUEING                0

#define IP_FORWARD                  0
#define IP_REASSEMBLY               0
#define IP_FRAG                     0

#define LWIP_NETIF_HOSTNAME         0
#define LWIP_SINGLE_NETIF           1
#define LWIP_NUM_NETIF_CLIENT_DATA  0

#define LWIP_PLATFORM_ASSERT(x)     do { (void)(x); } while (0)

#endif
