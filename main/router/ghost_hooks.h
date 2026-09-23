#pragma once
struct pbuf;
struct netif;
int ghost_ip4_input(struct pbuf *p, struct netif *inp);
int ghost_ip4_canforward(struct pbuf *p, unsigned int dest);
#define LWIP_HOOK_IP4_INPUT(p, inp) ghost_ip4_input((p), (inp))
#define LWIP_HOOK_IP4_CANFORWARD(p, dest) ghost_ip4_canforward((p), (dest))
