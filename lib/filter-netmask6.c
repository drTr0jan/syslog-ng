/*
 * Copyright (c) 2014 BalaBit S.a.r.l., Luxembourg, Luxembourg
 * Copyright (c) 2014 Zoltan Fried
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "filter-netmask6.h"
#include "gsocket.h"
#include "logmsg.h"

#include <stdlib.h>
#include <string.h>

#if ENABLE_IPV6
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

typedef struct _FilterNetmask6
{
  FilterExprNode super;
  struct in6_addr address;
  int prefix;
  gboolean is_valid;
} FilterNetmask6;

static inline uint64_t
_calculate_mask_by_prefix(int prefix)
{
  return (uint64_t) (~0) << (64 - prefix);
}

static inline uint64_t
_mask(uint64_t base, uint64_t mask)
{
  if (G_BYTE_ORDER == G_BIG_ENDIAN)
    return GUINT64_SWAP_LE_BE(base & mask);
  else
    return GUINT64_SWAP_LE_BE(GUINT64_SWAP_LE_BE(base) & mask);
}

static void
_get_network_address(unsigned char *ipv6, int prefix, struct in6_addr *network)
{
  struct ipv6_parts
  {
    uint64_t lo;
    uint64_t hi;
  } ipv6_parts;

  int length;

  memcpy(&ipv6_parts, ipv6, sizeof(ipv6_parts));
  if (prefix <= 64)
    {
      ipv6_parts.lo = _mask(ipv6_parts.lo, _calculate_mask_by_prefix(prefix));
      length = sizeof(uint64_t);
    }
  else
    {
      ipv6_parts.hi = _mask(ipv6_parts.hi, _calculate_mask_by_prefix(prefix - 64));
      length = 2 * sizeof(uint64_t);
    }

  memcpy(network->s6_addr, &ipv6_parts, length);
}

static inline gboolean
_in6_addr_compare(const struct in6_addr* address1, const struct in6_addr* address2)
{
  return memcmp(address1, address2, sizeof(struct in6_addr)) == 0;
}

static gboolean
_eval(FilterExprNode *s, LogMessage **msgs, gint num_msg)
{
  FilterNetmask6 *self = (FilterNetmask6 *) s;
  LogMessage *msg = msgs[0];
  gboolean result = FALSE;
  struct in6_addr network_address;
  struct in6_addr address;

  if (!self->is_valid)
    return s->comp;

  if (msg->saddr && g_sockaddr_inet6_check(msg->saddr))
    {
      address = ((struct sockaddr_in6 *) &msg->saddr->sa)->sin6_addr;
    }
  else if (!msg->saddr || msg->saddr->sa.sa_family == AF_UNIX)
    {
      address = in6addr_loopback;
    }
  else
    {
      return s->comp;
    }

  memset(&network_address, 0, sizeof(struct in6_addr));
  _get_network_address((unsigned char *) &address, self->prefix, &network_address);
  result = _in6_addr_compare(&network_address, &self->address);

  return result ^ s->comp;
}

FilterExprNode *
filter_netmask6_new(gchar *cidr)
{
  FilterNetmask6 *self = g_new0(FilterNetmask6, 1);
  struct in6_addr packet_addr;
  gchar address[INET6_ADDRSTRLEN] = "";
  gchar *slash = strchr(cidr, '/');

  if (strlen(cidr) >= INET6_ADDRSTRLEN + 5 || !slash)
    {
      strcpy(address, cidr);
      self->prefix = 128;
    }
  else
    {
      self->prefix = atol(slash + 1);
      if (self->prefix > 0 && self->prefix < 129)
        {
          strncpy(address, cidr, slash - cidr);
          address[slash - cidr] = 0;
        }
    }

  self->is_valid = ((strlen(address) > 0) && inet_pton(AF_INET6, address, &packet_addr) == 1);
  if (self->is_valid)
    _get_network_address((unsigned char *) &packet_addr, self->prefix, &self->address);
  else
    self->address = in6addr_loopback;

  self->super.eval = _eval;
  return &self->super;
}

#endif

