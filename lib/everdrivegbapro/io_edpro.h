#ifndef IO_EDPRO_H
#define IO_EDPRO_H

#include "../sys.h"

typedef enum { EDPRO_NOT_FOUND, EDPRO_FOUND, EDPRO_DETECT_ERROR } EdProDetection;

EdProDetection edpro_detect(void);
bool edpro_init(void);
bool edpro_read_sector(u32 sector, u8* destination, u16 count);
bool edpro_write_sector(u32 sector, const u8* source, u16 count);
bool edpro_sync(void);

#endif // IO_EDPRO_H
