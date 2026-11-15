#ifndef KWS_EVENT_H
#define KWS_EVENT_H

#include <stdint.h>
#include "cmsis_os.h"

/* index of classes (in order of outputs from ds_cnn). */
typedef enum {
  KWS_SILENCE  = 0,
  KWS_UNKNOWN  = 1,
  KWS_GO       = 2,
  KWS_BACKWARD = 3,
  KWS_LEFT     = 4,
  KWS_RIGHT    = 5,
  KWS_STOP     = 6,
} kws_cmd_t;

typedef struct {
  kws_cmd_t cmd;    /* command */
  float     prob;   /* probability of the command from 0 to 1 */
  uint32_t  t0;     /* timestamp of the event */
} kws_event_t;

/* Definition and osMessageQueueNew in app_freertos.c */
extern osMessageQueueId_t kwsEventQueueHandle;

#endif /* KWS_EVENT_H */
