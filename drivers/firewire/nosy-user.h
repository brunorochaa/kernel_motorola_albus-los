#ifndef __nosy_user_h
#define __nosy_user_h

#include <asm/ioctl.h>
#include <asm/types.h>

#define NOSY_IOC_GET_STATS _IOR('&', 0, struct nosy_stats)
#define NOSY_IOC_START     _IO('&', 1)
#define NOSY_IOC_STOP      _IO('&', 2)
#define NOSY_IOC_FILTER    _IOW('&', 2, __u32)

struct nosy_stats {
  __u32 total_packet_count;
  __u32 lost_packet_count;
};

/* 
 * Format of packets returned from the kernel driver:
 *
 *   quadlet with timestamp (microseconds)
 *   quadlet padded packet data...
 *   quadlet with ack
 */

#endif /* __nosy_user_h */
