#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/twi.h>

#include <csp/csp.h>
#include <csp_io.h>
#include <csp_port.h>
#include <csp_conn.h>
#include <csp_route.h>
#include <csp_promisc.h>
#include <csp_qfifo.h>

#include <csp/drivers/i2c.h>
#include <error.h>
#include <csp/csp_buffer.h>
#include <driver_debug.h>
//#include <util/log.h>

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
//#include "csp_qfifo.h"

/* Bit rate register  */
#define CPU_CLK 16000000
#define BITRATE(speed)	((CPU_CLK / (2 * 1000 * (uint32_t)speed) - 8) / (1 << ((TWSR & (_BV(TWPS0) | _BV(TWPS1))) << 1))) // (1 << (n << 1)) is 4^n
int i2cflag = 0;

static i2c_frame_t *rx_frame = NULL;
static i2c_frame_t *tx_frame = NULL;

static xQueueHandle tx_queue = NULL;
static xQueueHandle rx_queue = NULL;
static i2c_callback_t rx_callback = NULL;

static volatile uint16_t tx_cnt;
static volatile uint16_t rx_cnt;

static int device_mode;

static uint16_t i2c_speed;
static uint8_t i2c_addr;

static int i2c_busy = 0;
extern TaskHandle_t I2C_task;

/* Last TX time */
static portTickType last_tx = 0;

void send_ack() {
    TWCR &= ~(1 << TWSTO);
    TWCR |= (1 << TWINT) | (1 << TWEA);
}

void send_nack() {
    TWCR &= ~(1 << TWEA);
    TWCR &= ~(1 << TWSTO);

    TWCR |= (1 << TWINT);
}

void send_start() {
    TWCR &= ~(1 << TWSTO);
    TWCR |= (1 << TWINT) | (1 << TWSTA);
}

void send_stop() {
    TWCR &= ~(1 << TWSTA);
    TWCR |= (1 << TWINT) | (1 << TWSTO);
}

void send_data() {
    TWCR &= ~((1 << TWSTA) | (1 << TWSTO));
    TWCR |= (1 << TWINT);
}


int i2c_init(int handle, int mode, uint8_t addr, uint16_t speed,
             int queue_len_tx, int queue_len_rx, i2c_callback_t callback) {

    /* Validate mode */
    if (mode != I2C_MASTER && mode != I2C_SLAVE) {
        return E_INVALID_PARAM;
    }

    /* Initialise message queues */
    if (mode == I2C_MASTER) {
        tx_queue = xQueueCreate(queue_len_tx, sizeof(i2c_frame_t *));

        if (tx_queue == 0) {
            return E_OUT_OF_MEM;
        }
    }

    /* Register either callback or create RX queue */
    if (callback != NULL) {
        rx_callback = callback;
    }
    else {
        rx_queue = xQueueCreate(queue_len_rx, sizeof(i2c_frame_t *));

        if (rx_queue == 0) {
            return E_OUT_OF_MEM;
        }

    }

    /* Set device mode */
    device_mode = mode;

    /* Setup hardware */
    i2c_speed = speed;
    i2c_addr = addr;

    TWCR = 0; // Reset TWI hardware
    TWBR = BITRATE(i2c_speed); // Set bit rate register
    //printf("TWBR is %d\n", TWBR);
    TWAR = i2c_addr << 1; // Set slave address in the two wire address register
    //printf("add %d\n", TWAR);
    TWCR = _BV(TWEA) | _BV(TWEN) | _BV(TWIE); // Enable acknowledge, twi and interrupts

    return E_NO_ERR;

}


int i2c_send(int handle, i2c_frame_t *frame, uint16_t timeout) {

    if ((frame->len == 0) || (frame->len > I2C_MTU)) {
        return E_INVALID_BUF_SIZE;
    }


    if (device_mode == I2C_SLAVE) {
        printf("Device is I2C slave, so it cannot initiate a I2C frame\r\n");
        return E_INVALID_PARAM;
    }

    if (xQueueSendToBack(tx_queue, &frame, 0) != pdTRUE) {
        printf("QUEUE send fail\r\n");
        return E_NO_BUFFER;
    }

    //printf("\nsent");

    /* If bus is idle, send start condition */
    portENTER_CRITICAL();

    if (i2c_busy == 0) {
        //printf("TX-USR\r\n");
        last_tx = xTaskGetTickCount() + 1;
        send_start();
        i2c_busy = 1;
    }

    portEXIT_CRITICAL()	;

    return E_NO_ERR;

}


csp_packet_t *i2c_receive(csp_socket_t *sock, uint32_t timeout) {
    printf("\n csp_accpet socket %d", sock->socket);
    printf("\n qis %d", (sock->socket));

    if (sock == NULL) {
        return NULL;
    }

    if (sock->socket == NULL) {
        return NULL;
    }

    printf("\n csp_accpet socket %d", sock->socket);
    csp_packet_t *conn;
    printf("\nq%d", uxQueueMessagesWaiting(sock->socket));

//	if(uxQueueMessagesWaiting(sock->socket)!=0){
    if (xQueueReceive(sock->socket, &conn, timeout) == pdTRUE) {
        printf("\n data %d", conn->data[0]);
        return conn;
//	}
    }

    return NULL;

}

int i2c_store_frame(i2c_frame_t *frame , uint16_t timeout) {
    printf("\n qb4 is %d", uxQueueMessagesWaiting(tx_queue));

    if (uxQueueMessagesWaiting(tx_queue) == 0) {
        printf("\nattempting");
        xQueueSendToBack(tx_queue, &frame , timeout);
    }

    printf("\n qaft is %d", uxQueueMessagesWaiting(tx_queue));
    return 0;
}


void TWI_vect(void) __attribute__((signal));
void TWI_vect(void) {
    static uint8_t status;
    static signed portBASE_TYPE xTaskWoken;
    static uint8_t flag;
    static uint8_t flag2;
    status = TWSR & 0xF8;
    xTaskWoken = pdFALSE;
    flag = 0;

    last_tx = 0;

    switch (status) {

    /**
        SLAVE TRANSMIT EVENTS
    */

    case TW_ST_SLA_ACK:						// 0xA8 SLA+R received, ACK returned
    case TW_ST_ARB_LOST_SLA_ACK:			// 0xB0 Arbitration lost in SLA+RW, SLA+R received, ACK returned
    case TW_ST_DATA_ACK:					// 0xB8 Data transmitted, ACK received

        //printf("\nSlave Transmit -- ACK received");

        if (tx_frame == NULL) {
            if (xQueueReceiveFromISR(tx_queue, &tx_frame, &xTaskWoken) == pdFALSE) {
                TWCR |= _BV(TWSTO);
                break;
            }

            /* Reset counter */
            tx_cnt = 0;

        }

        /* Send next byte */
        if (tx_cnt < tx_frame->len) {
            TWDR = tx_frame->data[tx_cnt++];

        }
        else {
            printf("Error: i2c too long \r\n");
        }

        flag2 = _BV(TWEA);
        break;

    case TW_ST_DATA_NACK:				// 0xC0 Data transmitted, NACK received
    case TW_ST_LAST_DATA:				// 0xC8 Last data byte transmitted, ACK received
        //printf("\nSlave Transmit -- NACK/ACK received");

        csp_buffer_free_isr(tx_frame);
        tx_frame = NULL;
        flag2 = _BV(TWEA);

        break;

    /**
        SLAVE RECEIVE EVENTS
    */

    /* Beginning of new RX Frame */
    case TW_SR_SLA_ACK: 					// 0x60 SLA+W received, ACK returned
    case TW_SR_ARB_LOST_SLA_ACK:			// 0x68 Arbitration lost (in master mode) and addressed as slave, ACK returned
        //printf("\nSlave Receive -- ACK received");
        i2c_busy = 1;
        flag2 = _BV(TWEA);

        break;

    /* DATA received */
    case TW_SR_DATA_ACK: 					// 0x80 Data received, ACK returned
    case TW_SR_DATA_NACK: 					// 0x88 Data received, NACK returned
        //printf("\nSlave Received -- ACK return");

        i2c_busy = 1;

        /* Get buffer */
        if (rx_frame == NULL) {
            rx_frame = csp_buffer_get_isr(I2C_MTU);

            //printf("\nSlave Receive -- Getting buffer");

            if (rx_frame == NULL) {
                //printf("\nSlave Receive -- no buffer found\r\n");
                break;
            }

            rx_frame->len_rx = 0;
            rx_frame->len = 0;
            rx_frame->dest = i2c_addr;
        }

        /* Store data */
        if (rx_frame->len < I2C_MTU) {
            rx_frame->data[rx_frame->len++] = TWDR;
        }

        flag2 = _BV(TWEA);

        break;

    /* End of frame */
    case TW_SR_STOP: 						// 0xA0 Stop condition received or repeated start
        //printf("\nSlave Receive -- Stop");
        i2c_busy = 0;

        /* Break if no RX frame */
        if (rx_frame == NULL) {
            flag2 = _BV(TWEA);
            break;
        }

        /* Deliver frame */
        if (rx_callback != NULL) {
            //for(int i = 0 ; i<3; i++)
            //receive_frame.data[i]=rx_frame->data[i];
            (*rx_callback)(rx_frame, &xTaskWoken);
            //printf("\nSlave Receive --  Frame delivered");
            //printf("\r\nData is %d", rx_frame->data);

            //csp_buffer_free_isr(rx_frame);

        }
        else {
            //printf("\nSlave Receive -- Queue");

            if (xQueueSendFromISR(rx_queue, &rx_frame, (signed portBASE_TYPE *) &xTaskWoken) != pdTRUE) {
                csp_buffer_free_isr(rx_frame);
            }

            //printf("\n q has %d", uxQueueMessagesWaitingFromISR(rx_queue));
        }

        /* Prepare reply or next frame */
        if (device_mode == I2C_SLAVE) {
            //printf("\nSlave");

            if (tx_frame != NULL) {
                //printf("\ntxnotempty");
                csp_buffer_free_isr(tx_frame);
                tx_frame = NULL;
            }

            if (rx_frame->len == 0) {
                //printf("\nrxlennot0");
                csp_buffer_free_isr(rx_frame);
                rx_frame = NULL;
            }
            else {
                //printf("\ntxisrx");
                tx_frame = rx_frame;
            }

            tx_cnt = 0;
        }
        else {
            /* Try new transmission */
            if (uxQueueMessagesWaitingFromISR(tx_queue) > 0 || tx_frame != NULL) {
                //printf("\nnewtrans");
                flag |= _BV(TWSTA);
                i2c_busy = 1;
            }

        }

        if (i2cflag == 0) {
            //printf("\nwake");
            //printf("--- I2C interrupt done ---\r\n\r\n");

            xTaskWoken = xTaskResumeFromISR(I2C_task);
            i2cflag = 1;
        }

        rx_frame = NULL;
        flag2 = _BV(TWEA);

        break;

    /**
        MASTER TRANSMIT EVENTS
    */

    /* Frame START has been signalled */
    case TW_START: 							// 0x08 START has been transmitted
    case TW_REP_START: 				// 0x10 Repeated START has been transmitted
        //printf("\nMaster Transmit -- Start");

        i2c_busy = 1;

        /* First check if the previous frame was completed */
        if (tx_frame == NULL) {

            /* Receive next frame for transmission */
            if (xQueueReceiveFromISR(tx_queue, &tx_frame,
                                     &xTaskWoken) == pdFALSE) {

                TWCR |= _BV(TWSTO);
                break;
            }

            /* Reset counter */
            tx_cnt = 0;

        }

        /* Send destination addr */
        TWDR = tx_frame->dest << 1;
        flag2 = _BV(TWEA);

        break;

    /* Data ACK */
    case TW_MT_SLA_ACK: 		// 0x18 SLA+W has been transmitted, ACK received
    case TW_MT_DATA_ACK: // 0x28 Data byte has been transmitted,  ACK received
        //printf("\nMaster Transmit -- ACK received");

        i2c_busy = 1;

        /* If there is data left in the tx_frame */
        if (tx_frame != NULL && tx_cnt < tx_frame->len) {
            TWDR = *(tx_frame->data + tx_cnt);
            tx_cnt++;
            flag2 = _BV(TWEA);

            break;

        }

    /* Deliberate falltrough to stop */


    case TW_MT_SLA_NACK: 				// 0x20 SLA+W transmitted, NACK returned
    case TW_MT_DATA_NACK: 				// 0x30 Data transmitted, NACK returned

        //printf("\nMaster Transmit -- NACK return");

        /* Clear TX frame */
        if (tx_frame != NULL) {
            //printf("\nMaster Transmit -- NACK returned, clear tx frame");

            csp_buffer_free_isr(tx_frame);

            tx_frame = NULL;
        }

        /* Try new transmission */
        if (uxQueueMessagesWaitingFromISR(tx_queue) > 0) {

            /* Set START condition */
            flag |= _BV(TWSTA);
            flag |= _BV(TWSTO);
            i2c_busy = 1;
        }
        else {
            /* Set STOP condition */
            flag |= _BV(TWSTO);
            i2c_busy = 0;
        }

        flag2 = _BV(TWEA);

        break;

    case TW_MT_ARB_LOST: 		// 0x38 Arbitration lost, return to slave mode.
        //printf("\nf");


        tx_cnt = 0;
        flag |= _BV(TWSTA);
        i2c_busy = 1;
        flag2 = _BV(TWEA);

        break;

    /**
        MASTER RECEIVE EVENTS
    */



    case TW_MR_SLA_ACK:					// 0x40 SLA+R transmitted, ACK received
        //printf("\nslark");

        i2c_busy = 1;
        flag &= ~((1 << TWSTA) | (1 << TWSTO));
        flag2 = _BV(TWEA);

        break;

    case TW_MR_DATA_ACK:					// 0x50 Data received, ACK returned
        //printf("\ndark");

        i2c_busy = 1;


        if (rx_frame != NULL && rx_cnt < rx_frame->len) {
            *(rx_frame->data + rx_cnt) = TWDR;
            rx_cnt++;
            flag2 = _BV(TWEA);

            break;

        }

//		else{
//	        TWCR &= ~(1<<TWEA);

//		}


    case TW_MR_SLA_NACK:				// 0x48 SLA+R transmitted, NACK received
        //printf("\nslnak");
        flag |= _BV(TWSTA);
        flag2 = _BV(TWEA);

        break;

    case TW_MR_DATA_NACK:					// 0x58 Data received, NACK returned

//		/* Clear TX frame */
        //printf("\ndatnak");

        if (rx_frame != NULL) {
//			for(int i=0; i<rx_cnt; i++){
//				receive_frame.data[i]= rx_frame->data[i];
//			}
            //printf("\ntried");
            rx_frame = NULL;
            rx_cnt = 0;
        }

        /* Set STOP condition */
        flag |= _BV(TWSTO);
        i2c_busy = 0;
        flag2 = _BV(TWEA);

        break;

    /**
        TWI ERROR EVENTS
    */
    case TW_BUS_ERROR: 								// 0x00 Bus error

    default:
        printf("\nh");

        if (tx_frame != NULL) {
            //printf("\ntried");

            csp_buffer_free_isr(tx_frame);
            tx_frame = NULL;
        }

        if (rx_frame != NULL) {
            //printf("\ntried");

            csp_buffer_free_isr(rx_frame);
            rx_frame = NULL;
        }

        TWCR = 0; // Reset TWI hardware
        TWBR = BITRATE(i2c_speed); // Set bit rate register
        TWAR = i2c_addr << 1; // Set slave address in the two wire address register
        TWCR = _BV(TWEA) | _BV(TWEN) | _BV(TWIE); // Enable acknowledge, twi and interrupts
        flag2 = _BV(TWEA);
        i2c_busy = 0;

        break;
    }



    TWCR = flag | _BV(TWEN) | _BV(TWIE) | _BV(TWINT);
    TWCR |= flag2;

    if (xTaskWoken == pdTRUE) {
        taskYIELD();
    }
}
