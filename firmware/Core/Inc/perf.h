/* perf.h - lightweight section timing using the DWT cycle counter.
 *
 * Results go to NAMED global variables (perf_ch_t). Globals stay in the symbol table
 * even in Release without -g, so a debugger can read them by name (or by address).
 *
 * Usage:
 *   perf_ch_t perf_nn = { "nn" };          // at file scope
 *   perf_init();                           // ONCE at startup (enables DWT)
 *   PERF_BEGIN(perf_nn); ds_cnn_run(...); PERF_END(perf_nn);
 */
#ifndef PERF_H
#define PERF_H

#include <stdint.h>
#include "stm32g4xx.h"   // DWT, CoreDebug, bit masks

#ifndef PERF_CPU_HZ
#define PERF_CPU_HZ 160000000UL   // 160 MHz -> 160 cycles = 1 us
#endif

typedef struct {
  const char       *name;   // section label
  volatile uint32_t last;   // last measurement [cycles]
  volatile uint32_t max;    // worst case since start [cycles]
  volatile uint32_t count;  // number of measurements
  volatile uint64_t sum;    // total cycles; avg = sum/count
  uint32_t          _t0;    // start timestamp (internal)
} perf_ch_t;

// Enable the DWT cycle counter (call ONCE at startup, before the first PERF_BEGIN).
static inline void perf_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

#define PERF_BEGIN(ch)  do { (ch)._t0 = DWT->CYCCNT; } while (0)

#define PERF_END(ch)    do {                                  \
    uint32_t _d = DWT->CYCCNT - (ch)._t0;                     \
    (ch).last = _d;                                           \
    if (_d > (ch).max) (ch).max = _d;                         \
    (ch).sum += _d;                                           \
    (ch).count++;                                             \
  } while (0)

#define PERF_END_FROM(ch, t0)    do {                         \
    uint32_t _d = DWT->CYCCNT - (t0);                           \
    (ch).last = _d;                                           \
    if (_d > (ch).max) (ch).max = _d;                         \
    (ch).sum += _d;                                           \
    (ch).count++;                                             \
  } while (0)

#endif /* PERF_H */
