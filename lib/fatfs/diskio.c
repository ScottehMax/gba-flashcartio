/*-----------------------------------------------------------------------*/
/* Low level Flashcart I/O module for FatFs                              */
/*-----------------------------------------------------------------------*/

#include <string.h>
#include "../flashcartio.h"
#include "../sys.h"

#include "ff.h" /* Obtains integer types */

#include "diskio.h" /* Declarations of disk functions */

#define ALIGNED __attribute__((aligned(4)))

static u8 EWRAM_BSS aligned_buff[512 * 4] ALIGNED;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(BYTE driveId) {
  if (driveId != 0 || active_flashcart == NO_FLASHCART)
    return STA_NOINIT;
  return flashcartio_can_write() ? 0 : STA_PROTECT;
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize(BYTE driveId) {
  return disk_status(driveId);
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
  if (pdrv || !buff || !count || sector > 0xFFFFFFFFu - (count - 1))
    return RES_PARERR;
  if (disk_status(pdrv) & STA_NOINIT)
    return RES_NOTRDY;
  if ((u32)buff & 0x1) {
    for (UINT i = 0; i < count; i += 4) {
      const u16 blocks = (count - i > 4) ? 4 : (count - i);

      if (!flashcartio_read_sector(sector + i, aligned_buff, blocks))
        return RES_ERROR;

      memcpy(buff + i * 512, aligned_buff, blocks * 512);
    }
    return RES_OK;
  } else {
    // Public sector counts are u16; never silently truncate a FatFs UINT.
    while (count) {
      u16 blocks = count > 65535 ? 65535 : count;
      if (!flashcartio_read_sector(sector, buff, blocks))
        return RES_ERROR;
      sector += blocks;
      buff += (u32)blocks * 512;
      count -= blocks;
    }
    return RES_OK;
  }
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
  if (pdrv || !buff || !count || sector > 0xFFFFFFFFu - (count - 1))
    return RES_PARERR;
  DSTATUS state = disk_status(pdrv);
  if (state & STA_NOINIT)
    return RES_NOTRDY;
  if (state & STA_PROTECT)
    return RES_WRPRT;
  while (count) {
    u16 blocks = count > 65535 ? 65535 : count;
    if (!flashcartio_write_sector(sector, buff, blocks))
      return RES_ERROR;
    sector += blocks;
    buff += (u32)blocks * 512;
    count -= blocks;
  }
  return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  (void)buff;
  if (pdrv)
    return RES_PARERR;
  if (disk_status(pdrv) & STA_NOINIT)
    return RES_NOTRDY;
  // Fixed 512-byte sectors; formatting and TRIM are disabled in ffconf.h.
  if (cmd == CTRL_SYNC)
    return flashcartio_sync() ? RES_OK : RES_ERROR;
  return RES_PARERR;
}
