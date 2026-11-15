/** 
  * uROS thread: get command from neural network thread by QUEUE -> update robot state -> publish Twist meesage to ROS2 topic /cmd_vel.
**/

#include "uros_app.h"
#include "kws_event.h"
#include "robot_control.h"

#include "main.h"
#include "cmsis_os.h"
#include "perf.h"
  
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <uxr/client/transport.h>
#include <rmw_microros/rmw_microros.h>


#include <geometry_msgs/msg/twist.h>

#define V_LIN  0.15f   /* [m/s]   */
#define V_ANG  0.5f    /* [rad/s] */

#define UROS_PUBLISH_PERIOD_MS 100U

bool   cubemx_transport_open (struct uxrCustomTransport *transport);
bool   cubemx_transport_close(struct uxrCustomTransport *transport);
size_t cubemx_transport_write(struct uxrCustomTransport *transport, const uint8_t *buf, size_t len, uint8_t *err);
size_t cubemx_transport_read (struct uxrCustomTransport *transport, uint8_t *buf, size_t len, int timeout, uint8_t *err);

void *microros_allocate     (size_t size, void *state);
void  microros_deallocate   (void *pointer, void *state);
void *microros_reallocate   (void *pointer, size_t size, void *state);
void *microros_zero_allocate(size_t number_of_elements, size_t size_of_element, void *state);

/* debug variables */
volatile uint32_t uros_stage = 0;      /* 1=transport, 2=waiting for agent, 3=node+pub, 0xE1=error */
volatile int32_t  uros_sent  = 0;      /* number of successful rcl_publish calls */
volatile uint8_t  uros_last_cmd = 0;   /* last message received from queue */
volatile int8_t   uros_lin = 0;        /* current state: linear velocity */
volatile int8_t   uros_ang = 0;        /* current state: angular velocity */

perf_ch_t perf_uros_pub    = { "uros_pub" };     // rcl_publish
perf_ch_t perf_uros_e2e    = { "uros_e2e" };     // NN event created -> command published
perf_ch_t perf_uros_period = { "uros_period" };  // time between consecutive publishes

void StartUrosTask(void *argument)
{
  UART_HandleTypeDef *huart = (UART_HandleTypeDef *)argument;

  uros_stage = 1;
  rmw_uros_set_custom_transport(true, (void *)huart,
      cubemx_transport_open, cubemx_transport_close,
      cubemx_transport_write, cubemx_transport_read);

  rcl_allocator_t freertos_allocator = rcutils_get_zero_initialized_allocator();
  freertos_allocator.allocate      = microros_allocate;
  freertos_allocator.deallocate    = microros_deallocate;
  freertos_allocator.reallocate    = microros_reallocate;
  freertos_allocator.zero_allocate = microros_zero_allocate;
  if (!rcutils_set_default_allocator(&freertos_allocator)) {
    uros_stage = 0xE1;
    for (;;) osDelay(1000);
  }

  /* waiting for agent to respond */
  uros_stage = 2;
  while (rmw_uros_ping_agent(100, 1) != RMW_RET_OK) {
    osDelay(500);
  }

  rcl_allocator_t allocator = rcl_get_default_allocator();
  rclc_support_t  support;
  rcl_node_t      node;
  rcl_publisher_t publisher;

  rclc_support_init(&support, 0, NULL, &allocator);
  rclc_node_init_default(&node, "kws_robot", "", &support);
  rclc_publisher_init_default(&publisher, &node,ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist), "cmd_vel");
  uros_stage = 3;

  robot_state_t state;
  robot_control_init(&state);

  geometry_msgs__msg__Twist twist = {0};

  PERF_BEGIN(perf_uros_period);        
  for (;;) 
  {
    kws_event_t ev;
    bool got_event = false;

    if (osMessageQueueGet(kwsEventQueueHandle, &ev, NULL, UROS_PUBLISH_PERIOD_MS) == osOK) {
      robot_control_on_cmd(&state, ev.cmd);
      uros_last_cmd = ev.cmd;
      got_event = true;
    }
    uros_lin = state.lin;
    uros_ang = state.ang;

    twist.linear.x  =  state.lin * V_LIN;
    twist.angular.z = -state.ang * V_ANG;

    PERF_BEGIN(perf_uros_pub);
    rcl_ret_t rc = rcl_publish(&publisher, &twist, NULL);
    PERF_END(perf_uros_pub);

    if (rc == RCL_RET_OK) {
      uros_sent++;
    }

    if (got_event) {
      PERF_END_FROM(perf_uros_e2e, ev.t0);   // only when this publish carried a new command
    }

    PERF_END(perf_uros_period);             // publish -> publish
    PERF_BEGIN(perf_uros_period);
  }
}
