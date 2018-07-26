/*
	Copyright 2016 Benjamin Vedder	benjamin@vedder.se

	This file is part of the VESC firmware.

	The VESC firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    The VESC firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#include "app.h"
#include "ch.h"
#include "hal.h"
#include "hw.h"
#include "packet.h"
#include "commands.h"
#include "comm_can.h"
#include "utils.h"

#include <string.h>

#include <mavlink_types.h>
#include <common/mavlink.h>

// Settings
#define BAUDRATE					115200
#define PACKET_HANDLER				1
#define SERIAL_RX_BUFFER_SIZE		10
#define MAX_CAN_AGE					0.1

// Threads
static THD_FUNCTION(packet_process_thread, arg);
static THD_WORKING_AREA(packet_process_thread_wa, 8096);
static thread_t *process_tp = 0;

// Variables
static uint8_t serial_rx_buffer[SERIAL_RX_BUFFER_SIZE];
static int serial_rx_read_pos = 0;
static int serial_rx_write_pos = 0;
static int serial_last_write_pos = 0;
static volatile bool is_running = false;

// Private functions
static void process_packet(unsigned char *data, unsigned int len);
static void send_packet_wrapper(unsigned char *data, unsigned int len);
static void send_packet_wrapper_usb(unsigned char *data, unsigned int len);
static void send_packet(unsigned char *data, unsigned int len);

/*
 * This callback is invoked when a transmission buffer has been completely
 * read by the driver.
 */
static void txend1(UARTDriver *uartp) {
	(void)uartp;
}

/*
 * This callback is invoked when a transmission has physically completed.
 */
static void txend2(UARTDriver *uartp) {
	(void)uartp;
}

/*
 * This callback is invoked on a receive error, the errors mask is passed
 * as parameter.
 */
static void rxerr(UARTDriver *uartp, uartflags_t e) {
	(void)uartp;
	(void)e;
}

/*
 * This callback is invoked when a character is received but the application
 * was not ready to receive it, the character is passed as parameter.
 */
static void rxchar(UARTDriver *uartp, uint16_t c) {
	(void)uartp;
}

/*
 * This callback is invoked when a receive buffer has been completely written.
 */
static void rxend(UARTDriver *uartp) {
	serial_last_write_pos = serial_rx_write_pos;
	serial_rx_write_pos += SERIAL_RX_BUFFER_SIZE / 2;

	if (serial_rx_write_pos >= SERIAL_RX_BUFFER_SIZE)
		serial_rx_write_pos = 0;

	chSysLockFromISR();
	chEvtSignalI(process_tp, (eventmask_t) 1);
	chSysUnlockFromISR();

	uartStartReceive(&HW_UART_DEV, SERIAL_RX_BUFFER_SIZE / 2, &serial_rx_buffer[serial_rx_write_pos]);

}

/*
 * UART driver configuration structure.
 */
static UARTConfig uart_cfg = {
		txend1,
		txend2,
		rxend,
		rxchar,
		rxerr,
		BAUDRATE,
		0,
		USART_CR2_LINEN,
		0
};

static void process_packet(unsigned char *data, unsigned int len) {
	commands_set_send_func(send_packet_wrapper_usb);
	commands_process_packet(data, len);
}

static void send_packet_wrapper(unsigned char *data, unsigned int len) {
	packet_send_packet(data, len, PACKET_HANDLER);
}

static void send_packet_wrapper_usb(unsigned char *data, unsigned int len) {
	packet_send_packet(data, len, 0);
}

static void send_packet(unsigned char *data, unsigned int len) {
	// Wait for the previous transmission to finish.
	while (HW_UART_DEV.txstate == UART_TX_ACTIVE) {
		chThdSleep(1);
	}

	// Copy this data to a new buffer in case the provided one is re-used
	// after this function returns.
	static uint8_t buffer[PACKET_MAX_PL_LEN + 5];
	memcpy(buffer, data, len);

	uartStartSend(&HW_UART_DEV, len, buffer);
}

void app_uartcomm_start(void) {
	packet_init(send_packet, process_packet, PACKET_HANDLER);
	serial_rx_read_pos = 0;
	serial_rx_write_pos = 0;

	if (!is_running) {
		chThdCreateStatic(packet_process_thread_wa, sizeof(packet_process_thread_wa),
				NORMALPRIO, packet_process_thread, NULL);
		is_running = true;
	}

	uartStart(&HW_UART_DEV, &uart_cfg);
	palSetPadMode(HW_UART_TX_PORT, HW_UART_TX_PIN, PAL_MODE_ALTERNATE(HW_UART_GPIO_AF) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUPDR_PULLUP);
	palSetPadMode(HW_UART_RX_PORT, HW_UART_RX_PIN, PAL_MODE_ALTERNATE(HW_UART_GPIO_AF) |
			PAL_STM32_OSPEED_HIGHEST |
			PAL_STM32_PUPDR_PULLUP);
}

void app_uartcomm_stop(void) {
	uartStop(&HW_UART_DEV);
	palSetPadMode(HW_UART_TX_PORT, HW_UART_TX_PIN, PAL_MODE_INPUT_PULLUP);
	palSetPadMode(HW_UART_RX_PORT, HW_UART_RX_PIN, PAL_MODE_INPUT_PULLUP);

	// Notice that the processing thread is kept running in case this call is made from it.
}

void app_uartcomm_configure(uint32_t baudrate) {
	uart_cfg.speed = baudrate;

	if (is_running) {
		uartStart(&HW_UART_DEV, &uart_cfg);
	}
}

void handle_message(mavlink_message_t *msg) {
	if (msg->msgid == MAVLINK_MSG_ID_SERVO_OUTPUT_RAW) {
		mavlink_servo_output_raw_t actuator_outputs;
		mavlink_msg_servo_output_raw_decode(msg, &actuator_outputs);

		// set motor speeds
		uint8_t id = 1;
		comm_can_set_rpm(id, actuator_outputs.servo1_raw);
		id++;
		comm_can_set_rpm(id, actuator_outputs.servo2_raw);
		id++;
		comm_can_set_rpm(id, actuator_outputs.servo3_raw);
		id++;
		comm_can_set_rpm(id, actuator_outputs.servo4_raw);

		mavlink_esc_status_t esc_status = {};

		for (int i = 0; i < CAN_STATUS_MSGS_TO_STORE; i++) {
			can_status_msg *msg = comm_can_get_status_msg_index(i);

			if (msg->id >= 0 && UTILS_AGE_S(msg->rx_time) < MAX_CAN_AGE) {
				esc_status.rpm[i] = msg->rpm;
			}
		}

		mavlink_message_t out_msg = {};

		// system ID, component ID
		unsigned len = mavlink_msg_esc_status_pack(0, 100, &out_msg, &esc_status.rpm[0]);

		// Wait for the previous transmission to finish.
		while (HW_UART_DEV.txstate == UART_TX_ACTIVE) {
			chThdSleep(1);
		}

		// TODO: len should already be the total length but for some reason the payload bytes are ignore
		// and as a result the length is way to short - could be a mavlink 2 bug
		uartStartSend(&HW_UART_DEV, len + MAVLINK_MSG_ID_ESC_STATUS_LEN, (const void *)&out_msg);
	}
}

static THD_FUNCTION(packet_process_thread, arg) {
	(void)arg;

	chRegSetThreadName("uartcomm process");

	process_tp = chThdGetSelfX();

	mavlink_status_t serial_status = {};

	uartStartReceive(&HW_UART_DEV, SERIAL_RX_BUFFER_SIZE / 2, &serial_rx_buffer[serial_rx_write_pos]);
	mavlink_message_t msg;
	for(;;) {
		chEvtWaitAny((eventmask_t) 1);

		for (int i = 0; i < SERIAL_RX_BUFFER_SIZE / 2; i++) {

			if (mavlink_parse_char(MAVLINK_COMM_0, serial_rx_buffer[serial_last_write_pos+i], &msg, &serial_status)) {
				// have a message, handle it
				handle_message(&msg);
			}
		}
	}
}
