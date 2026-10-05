#include "io_edpro.h"

#define FIFO *(vu16*)0x08000080
#define STATUS *(vu16*)0x08000082
#define CONTROL *(vu16*)0x0800008E
#define ROM_STATUS 0x4403
#define FIFO_EMPTY 0x80
#define POLL_LIMIT 1000000
#define QUIET_LIMIT 100000

static bool EWRAM_BSS detected;
static bool EWRAM_BSS initialized;
static bool EWRAM_BSS needs_init;
static bool EWRAM_BSS protocol_error;
static u8 EWRAM_BSS buffer[1024] __attribute__((aligned(4)));

static void EWRAM_CODE command(u8 value) {
  FIFO = 0x2B;
  FIFO = 0xD4;
  FIFO = value;
  FIFO = value ^ 0xFF;
}

static bool EWRAM_CODE receive(u8* destination, u32 count, u32* budget) {
  while (count) {
    if (!*budget)
      return false;
    (*budget)--;
    if (!(STATUS & FIFO_EMPTY)) {
      *destination++ = FIFO;
      count--;
    }
  }
  return true;
}

static bool EWRAM_CODE status(u8* result, u32* budget) {
  u8 reply[4];
  command(0x10);
  if (!receive(reply, sizeof(reply), budget) || !(STATUS & FIFO_EMPTY))
    return false;
  *result = reply[3];
  return reply[0] == 0x5A && reply[1] == 0x04 && reply[2] == 0x24;
}

static bool EWRAM_CODE quiet(void) {
  for (u32 i = 0; i < QUIET_LIMIT; i++) {
    if (!(STATUS & FIFO_EMPTY))
      return false;
  }
  return true;
}

static bool EWRAM_CODE enable(void) {
  if (STATUS != ROM_STATUS)
    return false;
  CONTROL = 0xAB;
  u16 value = STATUS;
  return value != ROM_STATUS && value != 0xFFFF && (value & FIFO_EMPTY);
}

static bool EWRAM_CODE disable(void) {
  CONTROL = 0xAA;
  return STATUS == ROM_STATUS;
}

EdProDetection EWRAM_CODE edpro_detect(void) {
  if (detected)
    return EDPRO_FOUND;
  if (protocol_error)
    return EDPRO_DETECT_ERROR;
  if (STATUS != ROM_STATUS)
    return EDPRO_NOT_FOUND;
#if FLASHCARTIO_EDPRO_DISABLE_IRQ != 0
  u16 ime = REG_IME;
  REG_IME = 0;
#endif
  u32 budget = POLL_LIMIT;
  u8 result;
  bool enabled = enable();
  bool success = enabled && status(&result, &budget);
  bool restored = disable();
  detected = success && restored;
  protocol_error = !restored || (enabled && !success);
#if FLASHCARTIO_EDPRO_DISABLE_IRQ != 0
  REG_IME = ime;
#endif
  return detected ? EDPRO_FOUND : protocol_error ? EDPRO_DETECT_ERROR : EDPRO_NOT_FOUND;
}

// Keep every instruction and literal in RAM until AA restores the ROM.
static bool EWRAM_CODE transfer(u32 sector, u16 count, bool initialize) {
  if (protocol_error)
    return false;
#if FLASHCARTIO_EDPRO_DISABLE_IRQ != 0
  u16 ime = REG_IME;
  REG_IME = 0;
#endif
  u32 budget = POLL_LIMIT;
  u8 result;
  bool success = false;
  if (!enable() || !status(&result, &budget)) {
    protocol_error = true;
    goto done;
  }
  if (initialize || needs_init) {
    command(0xC0);
    if (!status(&result, &budget)) {
      protocol_error = true;
      goto done;
    }
    needs_init = result != 0;
    if (needs_init)
      goto done;
  }
  if (!count) {
    success = true;
    goto done;
  }
  command(0xC1);
  for (u32 i = 0; i < 4; i++)
    FIFO = (sector >> (i * 8)) & 0xFF;
  for (u32 i = 0; i < 4; i++)
    FIFO = ((u32)count >> (i * 8)) & 0xFF;
  for (u16 i = 0; i < count; i++) {
    if (!receive(&result, 1, &budget)) {
      protocol_error = true;
      goto done;
    }
    if (result) {
      // Only the single-sector D1 response has a verified recovery sequence.
      if (count == 1 && result == 0xD1 && quiet())
        needs_init = true;
      else
        protocol_error = true;
      goto done;
    }
    if (!receive(buffer + i * 512, 512, &budget)) {
      protocol_error = true;
      goto done;
    }
  }
  if (!(STATUS & FIFO_EMPTY)) {
    protocol_error = true;
    goto done;
  }
  success = true;
done:
  if (!disable()) {
    protocol_error = true;
    success = false;
  }
#if FLASHCARTIO_EDPRO_DISABLE_IRQ != 0
  REG_IME = ime;
#endif
  return success;
}

bool edpro_init(void) {
  initialized = detected && transfer(0, 0, true);
  return initialized;
}

static void copy_sector(u8* destination, u32 bytes) {
#if FLASHCARTIO_DISABLE_DMA == 0
  if (!((u32)destination & 1)) {
    DMA_CTR = 0;
    DMA_SRC = (u32)buffer;
    DMA_DST = (u32)destination;
    DMA_LEN = bytes / 2;
    DMA_CTR = 0x8000;
    while (DMA_CTR & 0x8000) {}
    return;
  }
#endif
  for (u32 i = 0; i < bytes; i++)
    destination[i] = buffer[i];
}

bool edpro_read_sector(u32 sector, u8* destination, u16 count) {
  if (!initialized || protocol_error)
    return false;
  if (!count)
    return true;
  if (!destination || sector > 0xFFFFFFFF - ((u32)count - 1))
    return false;
  while (count) {
    u16 blocks = count > 2 ? 2 : count;
    if (!transfer(sector, blocks, false))
      return false;
    copy_sector(destination, blocks * 512);
    destination += blocks * 512;
    sector += blocks;
    count -= blocks;
  }
  return true;
}
