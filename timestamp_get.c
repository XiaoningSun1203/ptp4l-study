#include "timestamp_get.h"
#include <stdbool.h>
#include <dirent.h>
#include <errno.h>

#define UIO_ROOT    "/sys/class/uio"
#define UIO_TYPE    "fpga"
volatile int32_t* timestamp_baseaddr;

bool getDeviceName(char* devName)
{
    DIR *dir;
    FILE *fd;
    struct dirent *dir_e;
    const char *name = UIO_TYPE;
    char namePath[256];
    char nameBuf[64];

    dir = opendir(UIO_ROOT);
    if (dir == NULL)
    {
        printf("failed to open directory %s ", UIO_ROOT);
        return false;
    }

    dir_e = readdir(dir);

    if (dir_e == NULL)
    {
        printf("failed to read directory %s ", UIO_ROOT);
        goto error;
    }

    while ((dir_e = readdir(dir)))
    {
        memset(namePath, 0, sizeof(namePath));
        memset(nameBuf, 0, sizeof(nameBuf));

        if (dir_e)
        {
            if (strstr(dir_e->d_name, "."))
            {
                continue;
            }
            snprintf(namePath, 256, UIO_ROOT "/%s/name", dir_e->d_name);

            fd = fopen(namePath, "r");

            if (fd == NULL)
            {
                printf("fopen failed with error: %d", errno);
                goto error;
            }

            if (fscanf(fd, "%63s\n", nameBuf) == EOF)
            {
                printf("fscanf failed with error: %d", errno);
            }

            if (!strcmp(name, nameBuf))
            {
                sprintf(devName, "%s", dir_e->d_name);
                closedir(dir);
                fclose(fd);
                return true;
            }

            fclose(fd);
        }
    }

error:
    closedir(dir);
    return false;
}
int timestamp_mmap(void)
{
    int fd, fpgaFd;
    char devName[16] = {0};
    char devPath[32] = {0};

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if(fd < 0){
        printf("can't open /dev/mem\r\n");
        return -1;
    }
    if(!getDeviceName(devName))
    {
        printf("open: failed to getDeviceName,%s ",UIO_TYPE);
    }

    sprintf(devPath, "/dev/%s", devName);
    fpgaFd = open(devPath, O_RDWR | O_SYNC);

    if(fpgaFd < 0){
        printf("can't open /dev/%s\r\n",devName);
        close(fd);
        return -1;
    }

    int32_t fpgaBaseAddr = 0xA0000000;   // uio2 fpga mmap0 addr

    int32_t *fpgaAddr = (uintptr_t)mmap((void *)&fpgaBaseAddr, UIO_FPGA_MAP_SIZE, PROT_READ, MAP_SHARED, fpgaFd, 0);
    if (NULL == (void *)fpgaAddr)
    {
        printf("failed fpga mmap\r\n");
        close(fpgaFd);
        close(fd);
        return -1;
    }

    if(fpgaAddr[ETH0_MAC_LOCK_STATE_REG] == 0x5)
    {
        printf("fheth0\n");
        timestamp_baseaddr = (int32_t*)mmap(NULL,DEVICE_IOMEM_SIZE, PROT_READ, MAP_SHARED, fd, DEVICE_IOMEM_ETH0_ADDR);
    }
    else if(fpgaAddr[ETH1_MAC_LOCK_STATE_REG] == 0x5)
    {
        printf("fheth1\n");
        timestamp_baseaddr = (int32_t*)mmap(NULL,DEVICE_IOMEM_SIZE, PROT_READ, MAP_SHARED, fd, DEVICE_IOMEM_ETH1_ADDR);
    }
    //Due to the fpga isn't ready now, don't set the default, when receive data, will check the base address again.
    close(fpgaFd);
    close(fd);

    return 0;
}

int timestamp_munmap(void)
{
    munmap((void *) timestamp_baseaddr, DEVICE_IOMEM_SIZE);

    return 0;
}

int64_t timestamp_announce_get(void)
{
    int64_t announce;
    int32_t sec,nsec;

    if(timestamp_baseaddr != NULL)
    {
        sec = *(timestamp_baseaddr + (TSTAMP_ANNOUNCE_SEC_LSB >> 2));
        nsec = *(timestamp_baseaddr + (TSTAMP_ANNOUNCE_NS >> 2));
        announce = sec * 1000000000ULL + nsec;
    }
    else
    {
        timestamp_mmap();
    }

    //printf("announce-sec: 0x%08x, announce-ns: 0x%08x\r\n",sec,nsec);
    return announce;
}


int64_t timestamp_sync_get(void)
{
    int64_t sync;
    int32_t sec,nsec;

    if(timestamp_baseaddr != NULL)
    {
        sec = *(timestamp_baseaddr + (TSTAMP_SYNC_SEC_LSB >> 2));
        nsec = *(timestamp_baseaddr + (TSTAMP_SYNC_NS >> 2));
        sync = sec * 1000000000ULL + nsec;
    }
    else
    {
        timestamp_mmap();
    }

    //printf("sync-sec: 0x%08x, sync-ns: 0x%08x\r\n",sec,nsec);
    return sync;
}

int64_t timestamp_dlyreq_get(void)
{
    int64_t dlyreq;
    int32_t sec,nsec;

    if(timestamp_baseaddr != NULL)
    {
        sec = *(timestamp_baseaddr + (TSTAMP_DLYREQ_SEC_LSB >> 2));
        nsec = *(timestamp_baseaddr + (TSTAMP_DLYREQ_NS >> 2));
        dlyreq = sec * 1000000000ULL + nsec;
    }
    else
    {
        timestamp_mmap();
    }
    //printf("dlyreq-sec: 0x%08x, dlyreq-ns: 0x%08x\r\n",sec,nsec);
    return dlyreq;
}


int64_t timestamp_followup_get(void)
{
    int64_t followup;
    int32_t sec,nsec;
    
    if(timestamp_baseaddr != NULL)
    {
        sec = *(timestamp_baseaddr + (TSTAMP_FOLLOWUP_SEC_LSB >> 2));
        nsec = *(timestamp_baseaddr + (TSTAMP_FOLLOWUP_NS >> 2));
        followup = sec * 1000000000ULL + nsec;
    }
    else
    {
        timestamp_mmap();
    }
    //printf("t4-sec: 0x%08x, t4-ns: 0x%08x\r\n",sec,nsec);
    return followup;
}

int64_t timestamp_dlyrsp_get(void)
{
    int64_t dlyrsp;
    int32_t sec,nsec;
    
    if(timestamp_baseaddr != NULL)
    {
        sec = *(timestamp_baseaddr + (TSTAMP_DLYRSP_SEC_LSB >> 2));
        nsec = *(timestamp_baseaddr + (TSTAMP_DLYRSP_NS >> 2));
        dlyrsp = sec * 1000000000ULL + nsec;
    }
    else
    {
        timestamp_mmap();
    }
    //printf("dlyrsp-sec: 0x%08x, dlyrsp-ns: 0x%08x\r\n",sec,nsec);
    return dlyrsp;
}
