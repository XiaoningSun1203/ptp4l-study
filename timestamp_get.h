#ifndef _TIMESTAMP_GET_H
#define _TIMESTAMP_GET_H

#include "stdint.h"
#include "stdio.h"
#include "stddef.h"
#include "stdlib.h"
#include "string.h"
#include "fcntl.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "sys/mman.h"


//#define DEVICE_IOMEM_ADDR   0xa0070000
#define DEVICE_IOMEM_ETH1_ADDR    0xA1004000   //timestamp register for fheth1
#define DEVICE_IOMEM_ETH0_ADDR    0xA1003000   //timestamp register for fheth0

#define UIO_FPGA_MAP_SIZE 0x20000000    //ZILLNK_ONLY  map_size
#define ETH1_MAC_LOCK_STATE_REG    0x400826   //ZILLNK_ONLY SFP1 lock state register, 0x5: lock, else: unlock
#define ETH0_MAC_LOCK_STATE_REG    0x400806   //ZILLNK_ONLY SFP0 lock state register, 0x5: lock, else: unlock

#define DEVICE_IOMEM_SIZE   32768

/////////////////////////////////////////////////
#define TSTAMP_ANNOUNCE_NS  0x80
#define TSTAMP_ANNOUNCE_SEC_LSB 0x84
#define TSTAMP_ANNOUNCE_SEC_MSB 0x88

#define TSTAMP_SYNC_NS  0x8C
#define TSTAMP_SYNC_SEC_LSB 0x90
#define TSTAMP_SYNC_SEC_MSB 0x94

#define TSTAMP_FOLLOWUP_NS  0x98
#define TSTAMP_FOLLOWUP_SEC_LSB 0x9C
#define TSTAMP_FOLLOWUP_SEC_MSB 0xA0

#define TSTAMP_DLYRSP_NS  0xA4
#define TSTAMP_DLYRSP_SEC_LSB 0xA8 
#define TSTAMP_DLYRSP_SEC_MSB 0xAC

#define TSTAMP_DLYREQ_NS  0xB0
#define TSTAMP_DLYREQ_SEC_LSB 0xB4
#define TSTAMP_DLYREQ_SEC_MSB 0xB8

#define TSTAMP_VALID_STATUS 0xBC
#define TSTAMP_VALID_CLEAR  0xC0
////////////////////////////////////////////////

extern int timestamp_mmap(void);
extern int timestamp_munmap(void);
extern int64_t timestamp_announce_get(void);
extern int64_t timestamp_sync_get(void);
extern int64_t timestamp_followup_get(void);
extern int64_t timestamp_dlyreq_get(void);
extern int64_t timestamp_dlyrsp_get(void);


#endif
