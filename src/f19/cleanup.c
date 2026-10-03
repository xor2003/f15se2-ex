/* START.EXE — exit f19_cleanup (f15se2 shared/f19_cleanup.c lineage) */
#include "f19.h"

extern void restoreTimerIrqHandler(void);
extern void intDispatch(int16 intNum, uint8 *inRegs, uint8 *outRegs);
extern void far misc_clearKeyFlags(void);

uint8 f19_timerHandlerInstalled;   /* byte dseg:0x16af */

void f19_cleanup(void) {
    uint8 regs[0xe];
    if (f19_timerHandlerInstalled == 1) {
        restoreTimerIrqHandler();
    }
    regs[1] = 0; /* func 0 */
    regs[0] = 3; /* mode 3 (80x25) */
    intDispatch(0x10, regs, regs);
    misc_clearKeyFlags();
}
