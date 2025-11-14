#pragma once

/* lwIP minimal NO_SYS configuration for raw API in kernel */

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0

/* Memory options */
#define MEM_ALIGNMENT                   8
#define MEM_SIZE                        (256 * 1024)
#define MEMP_NUM_PBUF                   256
#define PBUF_POOL_SIZE                  256
#define PBUF_POOL_BUFSIZE               1536

/* Protocols */
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_RAW                        1
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_ICMP                       1

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0

/* Disable sequential APIs to allow NO_SYS=1 */
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0

/* Disable IP fragmentation/reassembly to reduce dependencies */
#define IP_FRAG                         0
#define IP_REASSEMBLY                   0

/* TCP configuration */
#define TCP_MSS                         1460
#define TCP_SND_BUF                     (64 * 1024)
#define TCP_SND_QUEUELEN                (48)
#define TCP_WND                         (64 * 1024)

/* Checksums in software */
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1

/* Timeouts */
#define LWIP_TIMERS                     1
#define LWIP_TIMERS_CUSTOM              0

/* Debugging off */
#define LWIP_DEBUG                      0

#define TCP_WND             (60 * 1024)    /* <= 0xFFFF */
#define TCP_SND_BUF         (32 * 1024)    /* достаточно для наших запросов */
#define TCP_SND_QUEUELEN    64             /* >= 2 * TCP_SND_BUF / TCP_MSS */
#define MEMP_NUM_TCP_SEG    TCP_SND_QUEUELEN

