/* PicoDrive's wrapper for emu2413
 */

#include <stddef.h>
#ifndef __HIGHSCORE__
#include "emu2413/emu2413.h"
#endif

/* smallest valid packed state: u32 address + 0x40 registers */
#define YM2413_STATE_MIN (4 + 0x40)

#ifndef __HIGHSCORE__
// the one instance that can be in a Mark III
extern OPLL *opll;
#endif

void YM2413_regWrite(unsigned data);
void YM2413_dataWrite(unsigned data);

size_t ym2413_pack_state(void *buf_, size_t size);
void ym2413_unpack_state(const void *buf_, size_t size);
