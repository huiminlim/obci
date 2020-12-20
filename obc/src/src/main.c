/*
    .__..__  __
    |  |[__)/  `
    |__|[__)\__.

	On-board Computer (OBC)
	A hardware subsystem of CubeSat FYP
*/

#include <asf.h>
#include <stdio.h>
#include <uart.h>

// FreeRTOS header files
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>

// CSP header files
#include <csp/csp.h>
#include <csp/interfaces/csp_if_i2c.h>
#include <csp/drivers/i2c.h>

// CubeSat header files
#include <cubesat_config.h>

// OBC header files
#include <obc_task.h>

extern TaskHandle_t I2C_task;

int main(void) {
    /* Initialization Mode */
    // Initialize board
    board_init();

    // Initialize UART
    // Baud Rate: 57600
    uart_init();
    printf("----------- EPS: Initialization Mode -----------\r\n");


    // Initialize GPIO for SPI dependencies
    ioport_init();

    // Initialize CSP
    // 300 bytes per buffer --> I2C MTU = 256 bytes
    // Buffer = MTU + 2 Sync flag + 2 Length field
    csp_buffer_init(2, 300);
    csp_init(OBC_ADDR);
    csp_i2c_init(OBC_ADDR, 0, 400);
    csp_route_set(EPS_ADDR, &csp_if_i2c, CSP_NODE_MAC);
    csp_route_start_task(500, 1);
    csp_rtable_print();	// For debugging purposes
    printf("\r\n");

    // Create FreeRTOS tasks
    // Placeholder blinky task
    extern void led_blinky(void *pvParameters);
    xTaskCreate(led_blinky, "Task to blink led pin 13", 100, NULL, OBC_NORM_PRIO, NULL);

    // Task for the csp server to be loaded
    extern void csp_twoway_client(void *pvParameters);
    xTaskCreate(csp_twoway_client, "Task for CSP server", 250, NULL, OBC_CSP_PRIO, &I2C_task);

    /*-----------------------------------------------------------------------------*/


    /* Autonomous Mode */
    printf("----------- EPS: Autonomous Mode -----------");
    // Start Scheduler
    vTaskStartScheduler();


    /*-----------------------------------------------------------------------------*/

    /* Execution will only reach here if there was insufficient heap to start the scheduler. */
    for ( ;; );
}

