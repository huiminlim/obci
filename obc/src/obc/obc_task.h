/*
    obc_task.h

    Created: 12/12/2020 10:36:53 PM
    Author: user
*/

#include <ioport.h>

#define MY_LED    IOPORT_CREATE_PIN(PORTB, 7) // Digital pin 13

#define OBC_CSP_PRIO 5
#define OBC_NORM_PRIO 1
#define OBC_CSP_TRANSACTION_TIMEOUT 1000

void led_blinky(void *pvParameters) ;
void csp_twoway_client(void *pvParameters) ;
//void csp_client (void *pvParameters);