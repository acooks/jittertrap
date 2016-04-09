#ifndef DECODE_H
#define DECODE_H

#if 0
/* depends on... */
#include <arpa/inet.h>
#endif

/* Ethernet */
#define HDR_LEN_ETHER 14
#define HDR_LEN_ETHER_VLAN 18
#define ADDR_LEN_ETHER 6

struct hdr_ethernet {
	uint8_t dhost[ADDR_LEN_ETHER]; /* Destination host address */
	uint8_t shost[ADDR_LEN_ETHER]; /* Source host address */
	union {
		uint16_t type; /* IP? ARP? RARP? etc */
		struct {
			uint8_t vlan_tag[4];
			uint16_t tagged_type;
		};
	};
#define VLAN_TPID 0x8100
} __attribute__((__packed__));

/* IP */
struct hdr_ipv4 {
	uint8_t ip_vhl;        /* version << 4 | header length >> 2 */
	uint8_t ip_tos;        /* type of service */
	uint16_t ip_len;       /* total length */
	uint16_t ip_id;        /* identification */
	uint16_t ip_off;       /* fragment offset field */
#define IP_RF 0x8000           /* reserved fragment flag */
#define IP_DF 0x4000           /* dont fragment flag */
#define IP_MF 0x2000           /* more fragments flag */
#define IP_OFFMASK 0x1fff      /* mask for fragmenting bits */
	uint8_t ip_ttl;        /* time to live */
	uint8_t ip_p;          /* protocol */
	uint16_t ip_sum;       /* checksum */
	struct in_addr ip_src; /* source address */
	struct in_addr ip_dst; /* dest address */
} __attribute__((__packed__));
#define IP_HL(ip) (((ip)->ip_vhl) & 0x0f)
#define IP_V(ip) (((ip)->ip_vhl) >> 4)

/* TCP */
typedef u_int tcp_seq;

struct hdr_tcp {
	uint16_t sport; /* source port */
	uint16_t dport; /* destination port */
	tcp_seq seq;    /* sequence number */
	tcp_seq ack;    /* acknowledgement number */
	uint8_t offx2;  /* data offset, rsvd */
#define TH_OFF(th) (((th)->offx2 & 0xf0) >> 4)
	uint8_t flags;
#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PUSH 0x08
#define TH_ACK 0x10
#define TH_URG 0x20
#define TH_ECE 0x40
#define TH_CWR 0x80
#define TH_FLAGS (TH_FIN | TH_SYN | TH_RST | TH_ACK | TH_URG | TH_ECE | TH_CWR)
	uint16_t win; /* window */
	uint16_t sum; /* checksum */
	uint16_t urp; /* urgent pointer */
} __attribute__((__packed__));


struct hdr_udp {
	uint16_t sport; /* source port */
	uint16_t dport; /* destination port */
	uint16_t ip_len;   /* total length */
	uint16_t chcksum;  /* udp header + payload checksum */
} __attribute__((__packed__));

struct hdr_icmp {
	uint8_t todo;
} __attribute__((__packed__));

#endif
