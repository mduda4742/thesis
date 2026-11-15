#include "robot_control.h"
#include "kws_event.h"

/* Step logic: moves v by 1 in the direction of dir, result always in [-1, 1]. */
static int8_t step(int8_t v, int8_t dir)
{
  v += dir;
  if (v > 1)  v = 1;
  if (v < -1) v = -1;
  return v;
}

void robot_control_init(robot_state_t *s)
{
  s->lin = 0;
  s->ang = 0;
}

void robot_control_on_cmd(robot_state_t *s, uint8_t cmd)
{
  switch (cmd) {
    case KWS_GO:       s->lin = step(s->lin, +1); break;
    case KWS_BACKWARD: s->lin = step(s->lin, -1); break;
    case KWS_LEFT:     s->ang = step(s->ang, -1); break;
    case KWS_RIGHT:    s->ang = step(s->ang, +1); break;
    case KWS_STOP:     s->lin = 0; s->ang = 0;    break;
    default:           break;   /* __silence__ or __unknown__ */
  }
}
