// net.h - Ethernet for kmon (net.c): GENET v5, ARP, IPv4, ICMP, UDP, DHCP, TFTP.
#include <stdint.h>

int  net_up(void);                      // init GENET once; 0 = ok
void net_stop(void);                    // DMA and MAC off (before go/doom)
void net_poll(void);                    // handle received frames (ARP, ping replies)
void net_status(void);
int  net_dhcp(void);                    // 0 = got an address
void net_set_ip(uint32_t ip, uint32_t mask, uint32_t gw);
int  net_ping(uint32_t ip, int count);
int  net_mdio_read(int reg);
// TFTP: returns bytes received / 0 sent ok, < 0 on error
long tftp_get(uint32_t server, const char *file, uint8_t *buf, uint32_t max);
long tftp_put(uint32_t server, const char *file, const uint8_t *buf, uint32_t len);
int  parse_ip(const char *s, uint32_t *ip);
