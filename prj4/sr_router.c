/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sr_arpcache.h"
#include "sr_if.h"
#include "sr_protocol.h"
#include "sr_router.h"
#include "sr_rt.h"
#include "sr_utils.h"

#define DEBUG_LOCAL 0
#define PRINT_DEBUG(msg, ...)            \
  do {                                   \
    if (DEBUG_LOCAL) {                   \
      fprintf(stderr, msg, __VA_ARGS__); \
    }                                    \
  } while (0)

#define PRINT_HEADER(msg, len, line)                                         \
  do {                                                                       \
    if (DEBUG_LOCAL) {                                                       \
      fprintf(stderr, "----------------- Line %d ----------------\n", line); \
      print_hdrs(msg, len);                                                  \
    }                                                                        \
  } while (0)

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/
void sr_init(struct sr_instance *sr) {
  /* REQUIRES */
  assert(sr);

  /* Initialize cache and cache cleanup thread */
  sr_arpcache_init(&(sr->cache));

  pthread_attr_init(&(sr->attr));
  pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
  pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
  pthread_t thread;

  pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);

  /* Add initialization code here! */

} /* -- sr_init -- */

/*---------------------------------------------------------------------
 * Method: ip_black_list(struct sr_ip_hdr *iph)
 * Scope:  Local
 *
 * This method is called each time the sr_handlepacket() is called.
 * Block IP addresses in the blacklist and print the log.
 * - Format : "[IP blocked] : <IP address>"
 * - e.g.) [IP blocked] : 10.0.2.100
 *
 *---------------------------------------------------------------------*/
int ip_black_list(struct sr_ip_hdr *iph) {
  char ip_blacklist[20] = "10.0.2.0"; /* DO NOT MODIFY */
  char mask[20] = "255.255.255.0";    /* DO NOT MODIFY */
  uint32_t ip_blacklist_int = ntohl(inet_addr(ip_blacklist));
  uint32_t mask_int = ntohl(inet_addr(mask));

  /* src ip & mask == blacklist ip */
  if ((ntohl(iph->ip_src) & mask_int) == ip_blacklist_int) {
    struct in_addr ip_src_addr;
    ip_src_addr.s_addr = iph->ip_src;
    printf("[IP blocked] : %s\n", inet_ntoa(ip_src_addr));
    return 1;
  }

  /* dst ip & mask == blacklist ip */
  if ((ntohl(iph->ip_dst) & mask_int) == ip_blacklist_int) {
    struct in_addr ip_dst_addr;
    ip_dst_addr.s_addr = iph->ip_dst;
    printf("[IP blocked] : %s\n", inet_ntoa(ip_dst_addr));
    return 1;
  }

  return 0;
}
/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/
void sr_handlepacket(struct sr_instance *sr, uint8_t *packet /* lent */,
                     unsigned int len, char *interface /* lent */) {
  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);

  /*
      We provide local variables used in the reference solution.
      You can add or ignore local variables.
  */
  uint8_t *new_pck;     /* new packet */
  unsigned int new_len; /* length of new_pck */

  unsigned int len_r; /* length remaining, for validation */
  uint16_t checksum;  /* checksum, for validation */

  struct sr_ethernet_hdr *e_hdr0, *e_hdr; /* Ethernet headers */
  struct sr_ip_hdr *i_hdr0, *i_hdr;       /* IP headers */
  struct sr_arp_hdr *a_hdr0, *a_hdr;      /* ARP headers */
  struct sr_icmp_hdr *ic_hdr0;            /* ICMP header */
  struct sr_icmp_t3_hdr *ict3_hdr;        /* ICMP type3 header */
  struct sr_icmp_t11_hdr *ict11_hdr;      /* ICMP type11 header */

  struct sr_if *ifc;            /* router interface */
  uint32_t ipaddr;              /* IP address */
  struct sr_rt *rtentry;        /* routing table entry */
  struct sr_arpentry *arpentry; /* ARP table entry in ARP cache */
  struct sr_arpreq *arpreq;     /* request entry in ARP cache */
  struct sr_packet *en_pck;     /* encapsulated packet in ARP cache */

  /* validation */
  if (len < sizeof(struct sr_ethernet_hdr)) return;
  len_r = len - sizeof(struct sr_ethernet_hdr);
  e_hdr0 = (struct sr_ethernet_hdr *)packet; /* e_hdr0 set */

  PRINT_HEADER(packet, len, __LINE__);

  /* IP packet arrived */
  if (e_hdr0->ether_type == htons(ethertype_ip)) {
    /* validation */
    if (len_r < sizeof(struct sr_ip_hdr)) return;
    len_r = len_r - sizeof(struct sr_ip_hdr);
    i_hdr0 =
        (struct sr_ip_hdr *)(((uint8_t *)e_hdr0) +
                             sizeof(struct sr_ethernet_hdr)); /* i_hdr0 set */

    if (i_hdr0->ip_v != 0x4) return;

    checksum = i_hdr0->ip_sum;
    i_hdr0->ip_sum = 0;
    if (checksum != cksum(i_hdr0, sizeof(struct sr_ip_hdr))) return;
    i_hdr0->ip_sum = checksum;

    /* check destination */
    for (ifc = sr->if_list; ifc != NULL; ifc = ifc->next) {
      if (i_hdr0->ip_dst == ifc->ip) break;
    }

    /* check ip black list */
    if (ip_black_list(i_hdr0)) {
      /* Drop the packet */
      return;
    }

    /* destined to router interface */
    if (ifc != NULL) {
      /* with ICMP */
      if (i_hdr0->ip_p == ip_protocol_icmp) {
        /* validation */
        if (len_r < sizeof(struct sr_icmp_hdr)) return;

        ic_hdr0 =
            (struct sr_icmp_hdr *)(((uint8_t *)i_hdr0) +
                                   sizeof(struct sr_ip_hdr)); /* ic_hdr0 set */

        /* echo request type */
        if (ic_hdr0->icmp_type == 0x08) {
          /* validation */
          checksum = ic_hdr0->icmp_sum;
          ic_hdr0->icmp_sum = 0;
          if (checksum != cksum(ic_hdr0, len - sizeof(struct sr_ethernet_hdr) -
                                             sizeof(struct sr_ip_hdr)))
            return;
          ic_hdr0->icmp_sum = checksum;

          /* modify to echo reply */
          i_hdr0->ip_ttl = INIT_TTL;
          ipaddr = i_hdr0->ip_src;
          i_hdr0->ip_src = i_hdr0->ip_dst;
          i_hdr0->ip_dst = ipaddr;
          i_hdr0->ip_sum = 0;
          i_hdr0->ip_sum = cksum(i_hdr0, sizeof(struct sr_ip_hdr));

          ic_hdr0->icmp_type = 0x00;
          ic_hdr0->icmp_sum = 0;
          ic_hdr0->icmp_sum =
              cksum(ic_hdr0, len - sizeof(struct sr_ethernet_hdr) -
                                 sizeof(struct sr_ip_hdr));

          rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_dst);
          if (rtentry != NULL) {
            ifc = sr_get_interface(sr, rtentry->interface);
            memcpy(e_hdr0->ether_shost, ifc->addr, ETHER_ADDR_LEN);
            arpentry = sr_arpcache_lookup(&(sr->cache), ipaddr);
            if (arpentry != NULL) {
              memcpy(e_hdr0->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
              free(arpentry);
              /* send */
              PRINT_HEADER(packet, len, __LINE__);
              sr_send_packet(sr, packet, len, rtentry->interface);
            } else {
              /* queue */
              PRINT_DEBUG("ICMP echo request %d: queue\n", __LINE__);
              arpreq = sr_arpcache_queuereq(&(sr->cache), ipaddr, packet, len,
                                            rtentry->interface);
              sr_arpcache_handle_arpreq(sr, arpreq);
            }
          }

          /* done */
          return;
        }

        /* other types */
        else
          return;
      }
      /* with TCP or UDP */
      else if (i_hdr0->ip_p == ip_protocol_tcp ||
               i_hdr0->ip_p == ip_protocol_udp) {
        /* validation */
        if (len_r + sizeof(struct sr_ip_hdr) < ICMP_DATA_SIZE) return;

        /* generate ICMP port unreachable packet */
        new_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr) +
                  sizeof(struct sr_icmp_t3_hdr);
        new_pck = (uint8_t *)calloc(1, new_len);

        /* ICMP header, type 3, code 3 */
        ict3_hdr =
            (struct sr_icmp_t3_hdr *)(new_pck + sizeof(struct sr_ethernet_hdr) +
                                      sizeof(struct sr_ip_hdr));
        ict3_hdr->icmp_type = 0x03;
        ict3_hdr->icmp_code = 0x03;
        ict3_hdr->unused = 0;
        ict3_hdr->next_mtu = 0;
        memcpy(ict3_hdr->data, i_hdr0, ICMP_DATA_SIZE);
        ict3_hdr->icmp_sum = 0;
        ict3_hdr->icmp_sum = cksum(ict3_hdr, sizeof(struct sr_icmp_t3_hdr));

        /* IP header */
        i_hdr = (struct sr_ip_hdr *)(new_pck + sizeof(struct sr_ethernet_hdr));
        i_hdr->ip_hl = 0x5;
        i_hdr->ip_v = 0x4;
        i_hdr->ip_tos = 0;
        i_hdr->ip_len =
            htons(sizeof(struct sr_ip_hdr) + sizeof(struct sr_icmp_t3_hdr));
        i_hdr->ip_id = 0;
        i_hdr->ip_off = htons(IP_DF);
        i_hdr->ip_ttl = INIT_TTL;
        i_hdr->ip_p = ip_protocol_icmp;
        i_hdr->ip_src = i_hdr0->ip_dst;
        i_hdr->ip_dst = i_hdr0->ip_src;
        i_hdr->ip_sum = 0;
        i_hdr->ip_sum = cksum(i_hdr, sizeof(struct sr_ip_hdr));

        /* Ethernet header */
        e_hdr = (struct sr_ethernet_hdr *)new_pck;
        e_hdr->ether_type = htons(ethertype_ip);
        rtentry = sr_findLPMentry(sr->routing_table, i_hdr->ip_dst);
        if (rtentry != NULL) {
          ifc = sr_get_interface(sr, rtentry->interface);
          memcpy(e_hdr->ether_shost, ifc->addr, ETHER_ADDR_LEN);
          arpentry = sr_arpcache_lookup(&(sr->cache), i_hdr->ip_dst);
          if (arpentry != NULL) {
            memcpy(e_hdr->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
            free(arpentry);
            /* send */
            PRINT_HEADER(new_pck, new_len, __LINE__);
            sr_send_packet(sr, new_pck, new_len, rtentry->interface);
          } else {
            /* queue */
            PRINT_DEBUG("ICMP port unreachable %d: queue\n", __LINE__);
            arpreq = sr_arpcache_queuereq(&(sr->cache), i_hdr->ip_dst, new_pck,
                                          new_len, rtentry->interface);
            sr_arpcache_handle_arpreq(sr, arpreq);
          }
        }

        /* done */
        free(new_pck);
        return;
      }
      /* with others */
      else
        return;
    }
    /* destined elsewhere, forward */
    else {
      /* refer routing table */
      rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_dst);

      /* routing table hit */
      if (rtentry != NULL) {
        /* check TTL expiration */
        if (i_hdr0->ip_ttl <= 1) {
          /* validation */
          if (len_r + sizeof(struct sr_ip_hdr) < ICMP_DATA_SIZE) return;
          /* generate ICMP time exceeded packet */
          new_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr) +
                    sizeof(struct sr_icmp_t11_hdr);
          new_pck = (uint8_t *)calloc(1, new_len);

          /* ICMP header, type 11, code 0 */
          ict11_hdr =
              (struct sr_icmp_t11_hdr *)(new_pck +
                                         sizeof(struct sr_ethernet_hdr) +
                                         sizeof(struct sr_ip_hdr));
          ict11_hdr->icmp_type = 0x0b;
          ict11_hdr->icmp_code = 0x00;
          ict11_hdr->unused = 0;
          memcpy(ict11_hdr->data, i_hdr0, ICMP_DATA_SIZE);
          ict11_hdr->icmp_sum = 0;
          ict11_hdr->icmp_sum =
              cksum(ict11_hdr, sizeof(struct sr_icmp_t11_hdr));

          ifc = sr_get_interface(sr, interface);
          /* IP header */
          i_hdr =
              (struct sr_ip_hdr *)(new_pck + sizeof(struct sr_ethernet_hdr));
          i_hdr->ip_hl = 0x5;
          i_hdr->ip_v = 0x4;
          i_hdr->ip_tos = 0;
          i_hdr->ip_len =
              htons(sizeof(struct sr_ip_hdr) + sizeof(struct sr_icmp_t11_hdr));
          i_hdr->ip_id = 0;
          i_hdr->ip_off = htons(IP_DF);
          i_hdr->ip_ttl = INIT_TTL;
          i_hdr->ip_p = ip_protocol_icmp;
          i_hdr->ip_src = ifc->ip;
          i_hdr->ip_dst = i_hdr0->ip_src;
          i_hdr->ip_sum = 0;
          i_hdr->ip_sum = cksum(i_hdr, sizeof(struct sr_ip_hdr));

          /* Ethernet header */
          e_hdr = (struct sr_ethernet_hdr *)new_pck;
          e_hdr->ether_type = htons(ethertype_ip);
          memcpy(e_hdr->ether_shost, ifc->addr, ETHER_ADDR_LEN);
          arpentry = sr_arpcache_lookup(&(sr->cache), i_hdr->ip_dst);
          if (arpentry != NULL) {
            memcpy(e_hdr->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
            free(arpentry);
            /* send */
            PRINT_HEADER(new_pck, new_len, __LINE__);
            sr_send_packet(sr, new_pck, new_len, interface);
          } else {
            /* queue */
            PRINT_DEBUG("ICMP time exceeded %d: queue\n", __LINE__);
            arpreq = sr_arpcache_queuereq(&(sr->cache), i_hdr->ip_dst, new_pck,
                                          new_len, interface);
            sr_arpcache_handle_arpreq(sr, arpreq);
          }
          /* done */
          free(new_pck);
          return;
        } else {
          /* TTL not expired */

          /* set src MAC addr */
          ifc = sr_get_interface(sr, rtentry->interface);
          memcpy(e_hdr0->ether_shost, ifc->addr, ETHER_ADDR_LEN);

          /* refer ARP table */
          arpentry = sr_arpcache_lookup(&(sr->cache), i_hdr0->ip_dst);

          /* hit */
          if (arpentry != NULL) {
            /* set dst MAC addr */
            memcpy(e_hdr0->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);

            /* decrement TTL */
            i_hdr0->ip_ttl--;
            /* recompute checksum */
            i_hdr0->ip_sum = 0;
            i_hdr0->ip_sum = cksum(i_hdr0, sizeof(struct sr_ip_hdr));

            /* forward */
            PRINT_HEADER(packet, len, __LINE__);
            sr_send_packet(sr, packet, len, rtentry->interface);
            free(arpentry);
          }
          /* ARP table miss */
          else {
            /* queue */
            PRINT_DEBUG("line %d: queue\n", __LINE__);
            arpreq = sr_arpcache_queuereq(&(sr->cache), i_hdr0->ip_dst, packet,
                                          len, rtentry->interface);
            sr_arpcache_handle_arpreq(sr, arpreq);
          }
          /* done */
          return;
        }
      }
      /* routing table miss */
      else {
        /* validation */
        if (len_r + sizeof(struct sr_ip_hdr) < ICMP_DATA_SIZE) return;

        /* generate ICMP net unreachable packet */
        new_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr) +
                  sizeof(struct sr_icmp_t3_hdr);
        new_pck = (uint8_t *)calloc(1, new_len);

        /* ICMP header, type 3, code 0 */
        ict3_hdr =
            (struct sr_icmp_t3_hdr *)(new_pck + sizeof(struct sr_ethernet_hdr) +
                                      sizeof(struct sr_ip_hdr));
        ict3_hdr->icmp_type = 0x03;
        ict3_hdr->icmp_code = 0x00;
        ict3_hdr->unused = 0;
        ict3_hdr->next_mtu = 0;
        memcpy(ict3_hdr->data, i_hdr0, ICMP_DATA_SIZE);
        ict3_hdr->icmp_sum = 0;
        ict3_hdr->icmp_sum = cksum(ict3_hdr, sizeof(struct sr_icmp_t3_hdr));

        ifc = sr_get_interface(sr, interface);
        /* IP header */
        i_hdr = (struct sr_ip_hdr *)(new_pck + sizeof(struct sr_ethernet_hdr));
        i_hdr->ip_hl = 0x5;
        i_hdr->ip_v = 0x4;
        i_hdr->ip_tos = 0;
        i_hdr->ip_len =
            htons(sizeof(struct sr_ip_hdr) + sizeof(struct sr_icmp_t3_hdr));
        i_hdr->ip_id = 0;
        i_hdr->ip_off = htons(IP_DF);
        i_hdr->ip_ttl = INIT_TTL;
        i_hdr->ip_p = ip_protocol_icmp;
        i_hdr->ip_src = ifc->ip;
        i_hdr->ip_dst = i_hdr0->ip_src;
        i_hdr->ip_sum = 0;
        i_hdr->ip_sum = cksum(i_hdr, sizeof(struct sr_ip_hdr));

        /* Ethernet header */
        e_hdr = (struct sr_ethernet_hdr *)new_pck;
        e_hdr->ether_type = htons(ethertype_ip);
        rtentry = sr_findLPMentry(sr->routing_table, i_hdr->ip_dst);
        if (rtentry != NULL) {
          ifc = sr_get_interface(sr, rtentry->interface);
          memcpy(e_hdr->ether_shost, ifc->addr, ETHER_ADDR_LEN);
          arpentry = sr_arpcache_lookup(&(sr->cache), i_hdr->ip_dst);
          if (arpentry != NULL) {
            memcpy(e_hdr->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
            free(arpentry);
            /* send */
            PRINT_HEADER(new_pck, new_len, __LINE__);
            sr_send_packet(sr, new_pck, new_len, rtentry->interface);
          } else {
            /* queue */
            PRINT_DEBUG("ICMP Destination unreachable %d: queue\n", __LINE__);
            arpreq = sr_arpcache_queuereq(&(sr->cache), i_hdr->ip_dst, new_pck,
                                          new_len, rtentry->interface);
            sr_arpcache_handle_arpreq(sr, arpreq);
          }
        }

        /* done */
        free(new_pck);
        return;
      }
    }
  }
  /* ARP packet arrived */
  else if (e_hdr0->ether_type == htons(ethertype_arp)) {
    /* validation */
    if (len_r < sizeof(struct sr_arp_hdr)) return;

    a_hdr0 =
        (struct sr_arp_hdr *)(((uint8_t *)e_hdr0) +
                              sizeof(struct sr_ethernet_hdr)); /* a_hdr0 set */

    if (a_hdr0->ar_hrd != htons(arp_hrd_ethernet) ||
        a_hdr0->ar_pro != htons(ethertype_ip) ||
        a_hdr0->ar_hln != ETHER_ADDR_LEN || a_hdr0->ar_pln != 4)
      return;

    /* destined to me */
    ifc = sr_get_interface(sr, interface);
    if (a_hdr0->ar_tip == ifc->ip) {
      /* request code */
      if (a_hdr0->ar_op == htons(arp_op_request)) {
        /* generate reply */
        new_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_arp_hdr);
        new_pck = (uint8_t *)calloc(1, new_len);

        /* Ethernet header */
        e_hdr = (struct sr_ethernet_hdr *)new_pck;
        memcpy(e_hdr->ether_dhost, e_hdr0->ether_shost, ETHER_ADDR_LEN);
        memcpy(e_hdr->ether_shost, ifc->addr, ETHER_ADDR_LEN);
        e_hdr->ether_type = htons(ethertype_arp);

        /* ARP header */
        a_hdr = (struct sr_arp_hdr *)(new_pck + sizeof(struct sr_ethernet_hdr));
        a_hdr->ar_hrd = htons(arp_hrd_ethernet);
        a_hdr->ar_pro = htons(ethertype_ip);
        a_hdr->ar_hln = ETHER_ADDR_LEN;
        a_hdr->ar_pln = 4;
        a_hdr->ar_op = htons(arp_op_reply);
        memcpy(a_hdr->ar_tha, a_hdr0->ar_sha, ETHER_ADDR_LEN);
        a_hdr->ar_tip = a_hdr0->ar_sip;
        memcpy(a_hdr->ar_sha, ifc->addr, ETHER_ADDR_LEN);
        a_hdr->ar_sip = ifc->ip;

        PRINT_HEADER(new_pck, new_len, __LINE__);
        sr_send_packet(sr, new_pck, new_len, interface);
        free(new_pck);
        return;
      }

      /* reply code */
      else if (a_hdr0->ar_op == htons(arp_op_reply)) {
        /* pass info to ARP cache */
        arpreq =
            sr_arpcache_insert(&(sr->cache), a_hdr0->ar_sha, a_hdr0->ar_sip);
        if (arpreq != NULL) {
          /* pending request exist */
          for (en_pck = arpreq->packets; en_pck != NULL;
               en_pck = en_pck->next) {
            /* set dst MAC addr */
            e_hdr = (struct sr_ethernet_hdr *)(en_pck->buf);
            memcpy(e_hdr->ether_dhost, a_hdr0->ar_sha, ETHER_ADDR_LEN);
            /* decrement TTL except for self-generated packets */
            i_hdr = (struct sr_ip_hdr *)(en_pck->buf +
                                         sizeof(struct sr_ethernet_hdr));
            /* check if the packet is self-generated */
            for (ifc = sr->if_list; ifc != NULL; ifc = ifc->next)
              if (ifc->ip == i_hdr->ip_src) break;

            if (ifc == NULL) {
              i_hdr->ip_ttl--;
              i_hdr->ip_sum = 0;
              i_hdr->ip_sum = cksum(i_hdr, sizeof(struct sr_ip_hdr));
            }
            /* send */
            PRINT_HEADER(en_pck->buf, en_pck->len, __LINE__);
            sr_send_packet(sr, en_pck->buf, en_pck->len, en_pck->iface);
          }
          /* done */
          sr_arpreq_destroy(&(sr->cache), arpreq);
          return;
        }
        /* no exist */
        else
          return;
      }
      /* other codes */
      else
        return;
    }
    /* destined to others */
    else
      return;
  }
  /* other packet arrived */
  else
    return;

} /* end sr_ForwardPacket */

struct sr_rt *sr_findLPMentry(struct sr_rt *rtable, uint32_t ip_dst) {
  struct sr_rt *entry, *lpmentry = NULL;
  uint32_t mask, lpmmask = 0;

  ip_dst = ntohl(ip_dst);

  /* scan routing table */
  for (entry = rtable; entry != NULL; entry = entry->next) {
    mask = ntohl(entry->mask.s_addr);
    /* longest match so far */
    if ((ip_dst & mask) == (ntohl(entry->dest.s_addr) & mask) &&
        mask > lpmmask) {
      lpmentry = entry;
      lpmmask = mask;
    }
  }

  return lpmentry;
}
