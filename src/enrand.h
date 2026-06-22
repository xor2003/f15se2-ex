#ifndef F15_SE2_ENRAND
#define F15_SE2_ENRAND
/* RNG seeding (enrand.c). Distinct from strand.c's libc-srand seedRandom:
 * this seeds the end module's own srandInit/randSeed RNG. */

void enSeedRandom(void);

#endif /* F15_SE2_ENRAND */
