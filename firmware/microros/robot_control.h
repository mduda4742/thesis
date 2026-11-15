#ifndef ROBOT_CONTROL_H
#define ROBOT_CONTROL_H

/* logic of controlling the robot */

#include <stdint.h>

typedef struct {
  int8_t lin;   /* linear:   1 = farward, 0 = no motion, -1 = backward */
  int8_t ang;   /* angular: -1 = left,    0 = no motion,  1 = right  */
} robot_state_t;

/* Initializes the robot state (linear and angular are set to 0).  */
void robot_control_init(robot_state_t *s);

/* Updates the robot state based on the received command. */
void robot_control_on_cmd(robot_state_t *s, uint8_t cmd);

#endif /* ROBOT_CONTROL_H */
