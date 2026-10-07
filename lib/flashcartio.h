#ifndef FLASHCARTIO_H
#define FLASHCARTIO_H

#include <stdbool.h>
#include "fatfs/ff.h"

typedef enum { NO_FLASHCART, EVERDRIVE_GBA_X5, EZ_FLASH_OMEGA, EVERDRIVE_GBA_PRO } ActiveFlashcart;

extern ActiveFlashcart active_flashcart;
// Historical name: true during both reads and writes. Do not reset/reenter I/O.
extern volatile bool flashcartio_is_reading;

bool flashcartio_activate(void);
bool flashcartio_read_sector(unsigned int sector,
                             unsigned char* destination,
                             unsigned short count);
// Writes are currently supported only on EverDrive GBA Pro. A false result
// can follow a partial multi-sector write; do not blindly retry.
bool flashcartio_write_sector(unsigned int sector,
                              const unsigned char* source,
                              unsigned short count);
bool flashcartio_can_write(void);
bool flashcartio_sync(void);

#endif  // FLASHCARTIO_H
