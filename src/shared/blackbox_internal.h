#ifndef F15_SE2_BLACKBOX_INTERNAL_H
#define F15_SE2_BLACKBOX_INTERNAL_H

#include <stdio.h>

/* Private seam used by diagnostic modules. Keeping the recorder's FILE hidden
 * from gameplay code lets new diagnostics remain separate from blackbox.c. */
FILE *blackbox_internalRecordFile(void);
void blackbox_internalReplayError(const char *format, ...);

#endif /* F15_SE2_BLACKBOX_INTERNAL_H */
