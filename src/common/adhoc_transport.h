#ifndef COMMON_ADHOC_TRANSPORT_H
#define COMMON_ADHOC_TRANSPORT_H

#include "emucfg.h"
#include "common/adhoc.h"

#define ADHOC_DATATYPE_ANY      0xff
#define ADHOC_DATATYPE_ACK      0x01
#define ADHOC_DATATYPE_SYNC     0x02
#define ADHOC_DATATYPE_INPUT    0x04
#define ADHOC_DATATYPE_STATE    0x08
#define ADHOC_DATATYPE_USER1    0x10
#define ADHOC_DATATYPE_USER2    0x20
#define ADHOC_DATATYPE_USER3    0x40
#define ADHOC_DATATYPE_USER4    0x80

#define ADHOC_DATASIZE_ACK      (1 + sizeof(int))
#define ADHOC_DATASIZE_SYNC     (1 + sizeof(unsigned char))
#define ADHOC_DATASIZE_INPUT    (1 + sizeof(ADHOC_DATA))
#define ADHOC_DATASIZE_STATE    (1 + 0x3ff)
#define ADHOC_DATASIZE_USER1    (1)
#define ADHOC_DATASIZE_USER2    (1)
#define ADHOC_DATASIZE_USER3    (1)
#define ADHOC_DATASIZE_USER4    (1)

int adhocLoadModules(void);
int adhocUnloadModules(void);

int adhocInit(const char *matchingData);
int adhocTerm(void);
int adhocSelect(void);

int adhocSend(void *buffer, int length, int type);
int adhocRecv(void *buffer, int timeout, int type);

int adhocSendRecvAck(void *buffer, int length, int timeout, int type);
int adhocRecvSendAck(void *buffer, int length, int timeout, int type);

int adhocSync(void);
void adhocWait(int data_size);

#endif /* COMMON_ADHOC_TRANSPORT_H */
