#ifndef F15_DEBUGFRAME_H
#define F15_DEBUGFRAME_H

/* Debug frame dumps (development/verification aid, env-gated):
 *
 *   F15_DUMP_FRAME=<ppm path>  enable; writes once frameTick passes F15_DUMP_AT
 *   F15_DUMP_AT=<tick>         first dump tick (default ~120)
 *   F15_DUMP_EVERY=<n>         repeat every n ticks (path gets _NNNNN suffix)
 *
 * Two backends, same contract: debugDumpFrame() reads the software page
 * surface; debugDumpFrameGL() glReadPixels'es the GL back buffer just before
 * swap. Under GL the page surface only carries cockpit chrome, so the SW dump
 * self-disables via r3dgl_active() and the GL dump is called from
 * r3dgl_present() instead. */
void debugDumpFrame(void);
void debugDumpFrameGL(void);

#endif
