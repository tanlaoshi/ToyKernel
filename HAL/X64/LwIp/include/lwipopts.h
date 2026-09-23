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
#define MEM_SIZE                    (64 * 1024)
#define MEMP_NUM_PBUF               32
#define MEMP_NUM_RAW_PCB            2
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_TCP_SEG            40
#define MEMP_NUM_NETBUF             0
#define MEMP_NUM_NETCONN            0
#define MEMP_NUM_TCPIP_MSG_API      0
#define MEMP_NUM_TCPIP_MSG_INPKT    0

#define PBUF_POOL_SIZE              64
#define PBUF_POOL_BUFSIZE           512

#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_DHCP                   1
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_AUTOIP                 0
#define LWIP_DNS                    1
#define LWIP_IGMP                   0
#define LWIP_SNMP                   0
#define LWIP_STATS                  0

#define MEMP_NUM_SYS_TIMEOUT        20

/* 窗过小 → store HttpGet 只收到 ~3K 就停（truncated got=3084/14916） */
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * 1024)
#define TCP_WND                     (16 * 1024)
#define TCP_SND_QUEUELEN            (4 * TCP_SND_BUF / TCP_MSS)

#define LWIP_ARP                    1
#define ARP_TABLE_SIZE              4
#define ARP_QUEUEING                1
#define ETHARP_SUPPORT_STATIC_ENTRIES 1

#define IP_FORWARD                  0
#define IP_REASSEMBLY               0
#define IP_FRAG                     0

#define LWIP_NETIF_HOSTNAME         0
#define LWIP_SINGLE_NETIF           1
#define LWIP_NUM_NETIF_CLIENT_DATA  0

#define LWIP_PLATFORM_ASSERT(x)     do { (void)(x); } while (0)

/* DNS/TCP/UDP 随机端口；sys_now 原型在 arch/cc.h */
#define LWIP_RAND()                 ((u32_t)(sys_now() ^ 0xA5A5A5A5u))

#endif
