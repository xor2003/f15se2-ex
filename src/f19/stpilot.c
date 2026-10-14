/* START.EXE — roster/hall-of-fame globals. The EN f19_loadHallfame (seg000:0xa4fb)
 * uses fopen/fread/fclose directly and lives in stpinp.c. */
#include "f19.h"

typedef struct {
    char data[0x50];        /* hallfame record, 0x50 bytes */
} HallfameEntry;

int16 hallfameCount;        /* ds:0x9b52 */
HallfameEntry hallfameBuf[10]; /* ds:0x9d54 */
