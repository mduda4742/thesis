/* Shim zgodnosci dla CMSIS-NN (sciezka q7).
 *
 * __SXTB16_RORn to PRAWDZIWA instrukcja ARMv7E-M: "sxtb16 Rd, Rm, ROR #n"
 * (rdzen Cortex-M4 ja ma). Starszy rdzen CMSIS dostarczany ze sterownikami STM32
 * (Drivers/CMSIS/Include/cmsis_gcc.h) nie opakowal wariantu z ROR w funkcje C -
 * ma osobno __SXTB16 i __ROR. read_and_pad() w arm_nnsupportfunctions.h wola
 * __SXTB16_RORn, wiec definiujemy go jako zlozenie tych dwoch. Wynik bitowo
 * identyczny (co najwyzej dwie instrukcje zamiast jednej - bez znaczenia, liczone
 * raz na okno). To NIE jest obejscie - to brakujace opakowanie realnej instrukcji.
 *
 * Wymuszany przez -include TYLKO przy kompilacji targetu CMSISNN (patrz CMakeLists).
 */
#ifndef CMSIS_NN_COMPAT_H
#define CMSIS_NN_COMPAT_H

#ifndef __SXTB16_RORn
#define __SXTB16_RORn(op1, rotate) __SXTB16(__ROR((uint32_t)(op1), (rotate)))
#endif

#endif /* CMSIS_NN_COMPAT_H */
