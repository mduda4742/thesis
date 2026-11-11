/* perf.h - lekki pomiar czasu sekcji przez licznik cykli DWT.
 *
 * Wyniki trafiaja do NAZWANYCH zmiennych globalnych (perf_ch_t). Globalne sa w tablicy
 * symboli nawet w Release bez -g, wiec odczytasz je:
 *   - debuggerem (po nazwie, lub po adresie gdy brak -g),
 *   - przez UART (perf_report() -> printf, bez debuggera).
 *
 * Uzycie:
 *   perf_ch_t perf_nn = { "nn" };          // na poziomie pliku
 *   perf_init();                           // RAZ na starcie (wlacza DWT)
 *   PERF_BEGIN(perf_nn); ds_cnn_run(...); PERF_END(perf_nn);
 */
#ifndef PERF_H
#define PERF_H

#include <stdint.h>
#include "stm32g4xx.h"   // DWT, CoreDebug, maski

#ifndef PERF_CPU_HZ
#define PERF_CPU_HZ 160000000UL   // 160 MHz -> 160 cykli = 1 us
#endif

typedef struct {
  const char       *name;   // etykieta sekcji
  volatile uint32_t last;   // ostatni pomiar [cykle]
  volatile uint32_t max;    // najgorszy od startu [cykle]
  volatile uint32_t count;  // liczba pomiarow (do sredniej)
  volatile uint64_t sum;    // suma cykli (64-bit, by nie przepelnic) -> srednia = sum/count
  uint32_t          _t0;    // start (wewnetrzne)
} perf_ch_t;

// Wlacz licznik cykli DWT (wywolaj RAZ na starcie, przed pierwszym PERF_BEGIN).
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

// Srednia [cykle] = suma / liczba pomiarow (0 gdy jeszcze nic nie zmierzono).
#define PERF_AVG_CYC(ch)  ((ch).count ? (uint32_t)((ch).sum / (ch).count) : 0u)

// Wyzeruj statystyki kanalu (np. by liczyc srednia od nowa).
#define PERF_RESET(ch)    do {                                \
    (ch).last = 0; (ch).max = 0; (ch).count = 0; (ch).sum = 0;\
  } while (0)

// Przeliczniki cykle -> czas (do odczytu recznego / w debuggerze).
// Srednia w us:  PERF_US(PERF_AVG_CYC(perf_nn))
#define PERF_US(cyc)  ((float)(cyc) / ((float)PERF_CPU_HZ / 1000000.0f))
#define PERF_MS(cyc)  ((float)(cyc) / ((float)PERF_CPU_HZ / 1000.0f))

#endif /* PERF_H */
