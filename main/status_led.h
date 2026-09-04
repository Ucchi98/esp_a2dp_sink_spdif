#ifndef _STATUS_LED_H_
#define _STATUS_LED_H_

#define STATUS_LED_BT_READY        (0)
#define STATUS_LED_BT_DISCONNECTED (1)
#define STATUS_LED_BT_CONNECTED    (2)
#define STATUS_LED_AD_SUSPEND      (3)
#define STATUS_LED_AD_STARTED      (4)

extern void status_led_init(void);
extern void status_led_set_status(int status);

#endif
