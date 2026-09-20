#ifndef _ENDIAN_H
#define _ENDIAN_H

#include <netinet/in.h>

#define be16toh(x) ntohs(x)
#define htobe16(x) htons(x)
#define be32toh(x) ntohl(x)
#define htobe32(x) htonl(x)

#define le16toh(x) (x)
#define htole16(x) (x)
#define le32toh(x) (x)
#define htole32(x) (x)

#endif
