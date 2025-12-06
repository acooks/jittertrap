#ifndef DECODE_H
#define DECODE_H

#define DECODE_ERRBUF_SIZE 80

/* Ethernet */
#define HDR_LEN_ETHER 14
#define HDR_LEN_ETHER_VLAN 18
#define ADDR_LEN_ETHER 6

#define ETHERTYPE_LLDP 0x88cc /* Link Layer Discovery Protocol */

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

int decode_ethernet(const struct pcap_pkthdr *h, const uint8_t *wirebits,
                    struct flow_pkt *pkt, char *errstr);

int decode_linux_sll(const struct pcap_pkthdr *h, const uint8_t *wirebits,
                     struct flow_pkt *pkt, char *errstr);

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
int decode_ip4(const uint8_t *packet, const uint8_t *end_of_packet,
               struct flow_pkt *pkt, char *errstr);

struct hdr_ipv6 {
	uint32_t vcf;
	uint16_t payload_len;
	uint8_t next_hdr; /* like protocol in ipv4 */
	uint8_t hop_limit;
	struct in6_addr ip6_src;
	struct in6_addr ip6_dst;
} __attribute__((__packed__));

int decode_ip6(const uint8_t *packet, const uint8_t *end_of_packet,
               struct flow_pkt *pkt, char *errstr);

struct hdr_tcp {
	uint16_t sport; /* source port */
	uint16_t dport; /* destination port */
	uint32_t seq;   /* sequence number */
	uint32_t ack;   /* acknowledgement number */
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

int decode_tcp(const struct hdr_tcp *packet, struct flow_pkt *pkt,
               char *errstr);

/* Extended flow packet structure with TCP-specific fields for RTT tracking */
struct flow_pkt_tcp {
	struct flow_pkt base;
	uint32_t seq;               /* TCP sequence number */
	uint32_t ack;               /* TCP acknowledgement number */
	uint8_t flags;              /* TCP flags */
	uint16_t payload_len;       /* TCP payload length (excluding headers) */
};

/* Extended TCP decode that also extracts seq/ack/flags for RTT tracking */
int decode_tcp_extended(const struct hdr_tcp *packet,
                        const uint8_t *end_of_packet,
                        struct flow_pkt_tcp *pkt,
                        char *errstr);

struct hdr_udp {
	uint16_t sport;   /* source port */
	uint16_t dport;   /* destination port */
	uint16_t ip_len;  /* total length */
	uint16_t chcksum; /* udp header + payload checksum */
} __attribute__((__packed__));

int decode_udp(const struct hdr_udp *packet, struct flow_pkt *pkt,
               char *errstr);

struct hdr_icmp {
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
	uint32_t hdr_data; /* purpose depends on type field */
} __attribute__((__packed__));


struct hdr_esp {
	uint32_t spi;
	uint32_t seq;
} __attribute__((__packed__));

int decode_icmp(const struct hdr_icmp *packet, struct flow_pkt *pkt,
                char *errstr);

int decode_igmp(const struct hdr_icmp *packet, struct flow_pkt *pkt,
                char *errstr);

int decode_icmp6(const struct hdr_icmp *packet, const uint8_t *end_of_packet,
                 struct flow_pkt *pkt, char *errstr);

int decode_esp(const struct hdr_esp *packet, struct flow_pkt *pkt,
               char *errstr);
#endif
