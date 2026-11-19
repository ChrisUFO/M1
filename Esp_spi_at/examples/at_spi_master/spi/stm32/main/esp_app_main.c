/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "stream_buffer.h"

//#include "esp_system.h"
#include "esp_err.h"
#include "m1_log_debug.h"
#include "spi_master.h"
#include "m1_tasks.h"
#include "m1_compile_cfg.h"
#include "m1_esp32_hal.h"
#include "esp_at_list.h"
#include "esp_queue.h"
#include "m1_at_response_parser.h"

#define STREAM_BUFFER_SIZE    	SPI_TRANS_MAX_LEN

#define TAG						"SPI_AT_Master"
#define ESP_SPI_ID				0x00

#define CR_LF					"\r\n"

#define MILLISEC_TO_SEC			1000
#define TICKS_PER_SEC (1000 / portTICK_PERIOD_MS);
#define SEC_TO_MILLISEC(x) (1000*(x))

/* If request is already being served and
 * another request is pending, time period for
 * which new request will wait in seconds
 * */
#define WAIT_TIME_B2B_CTRL_REQ               5
#define DEFAULT_CTRL_RESP_TIMEOUT            30
#define DEFAULT_CTRL_RESP_AP_SCAN_TIMEOUT    (60*3)
#define DEFAULT_CTRL_RESP_CONNECT_AP_TIMEOUT (15*3)

QueueHandle_t esp_spi_msg_queue; // message queue used for communicating read/write start
QueueHandle_t esp_resp_read_sem = NULL;
QueueHandle_t esp_ctrl_req_sem = NULL;
esp_queue_t* ctrl_msg_Q = NULL;

static spi_device_handle_t spi_dev_handle = NULL;
static StreamBufferHandle_t spi_master_tx_ring_buf = NULL;
static SemaphoreHandle_t pxMutex;
static uint8_t initiative_send_flag = 0; // it means master has data to send to slave
static uint32_t plan_send_len = 0; // master plan to send data len

static uint8_t current_send_seq = 0;
static uint8_t current_recv_seq = 0;

static bool esp32_main_init_done = false;

/* uid to link between requests and responses
 * uids are incrementing values from 1 onwards. */
static int32_t uid = 0;
/* uid of request / response */
static int32_t current_uid = 0;

static void spi_mutex_lock(void)
{
    while (xSemaphoreTake(pxMutex, portMAX_DELAY) != pdPASS);
}

static void spi_mutex_unlock(void)
{
    xSemaphoreGive(pxMutex);
}

static void at_spi_master_send_data(uint8_t* data, uint32_t len)
{
	HAL_StatusTypeDef ret;

	spi_transaction_t trans = {
        .cmd = CMD_HD_WRDMA_REG,    // master -> slave command, donnot change
        .length = len * 8,
        .tx_buffer = (void*)data
    };
	ret = spi_device_polling_transmit(spi_dev_handle, &trans);
	current_uid = uid; // Update current request command id after it has been transmitted
}

static void at_spi_master_recv_data(uint8_t* data, uint32_t len)
{
	spi_transaction_t trans = {
        .cmd = CMD_HD_RDDMA_REG,    // master -> slave command, donnot change
        .rxlength = len * 8,
        .rx_buffer = (void*)data
    };
	spi_device_polling_transmit(spi_dev_handle, &trans);
}

// send a single to slave to tell slave that master has read DMA done
static void at_spi_rddma_done(void)
{
    spi_transaction_t end_t = {
        .cmd = CMD_HD_INT0_REG,
    };
    spi_device_polling_transmit(spi_dev_handle, &end_t);
}

// send a single to slave to tell slave that master has write DMA done
static void at_spi_wrdma_done(void)
{
    spi_transaction_t end_t = {
        .cmd = CMD_HD_WR_END_REG,
    };
    spi_device_polling_transmit(spi_dev_handle, &end_t);
}

// when spi slave ready to send/recv data from the spi master, the spi slave will a trigger GPIO interrupt,
// then spi master should query whether the slave will perform read or write operation.
static spi_recv_opt_t query_slave_data_trans_info()
{
    spi_recv_opt_t recv_opt = {};
    spi_transaction_t trans = {
        .cmd = CMD_HD_RDBUF_REG,
        .addr = RDBUF_START_ADDR,
        .rxlength = 4 * 8,
        .rx_buffer = &recv_opt,
    };
    spi_device_polling_transmit(spi_dev_handle, (spi_transaction_t*)&trans);

    return recv_opt;
}

// before spi master write to slave, the master should write WRBUF_REG register to notify slave,
// and then wait for handshake line trigger gpio interrupt to start the data transmission.
static void spi_master_request_to_write(uint8_t send_seq, uint16_t send_len)
{
    spi_send_opt_t send_opt;
    send_opt.magic = 0xFE;
    send_opt.send_seq = send_seq;
    send_opt.send_len = send_len;

    spi_transaction_t trans = {
        .cmd = CMD_HD_WRBUF_REG,
        .addr = WRBUF_START_ADDR,
        .length = 4 * 8,
        .tx_buffer = &send_opt,
    };
    spi_device_polling_transmit(spi_dev_handle, (spi_transaction_t*)&trans);
    // increment
    current_send_seq  = send_seq;
}

// spi master write data to slave
static int8_t spi_write_data(uint8_t* buf, int32_t len)
{
    if (len > SPI_TRANS_MAX_LEN) {
        //M1_LOG_E(TAG, "Send length error, len:%ld\r\n", len);
        return -1;
    }
    at_spi_master_send_data(buf, len);
    at_spi_wrdma_done();
    return 0;
}

// write data to spi tx_ring_buf, this is just for test
static int32_t write_data_to_spi_task_tx_ring_buf(const void* data, size_t size)
{
    int32_t length = size;

    if (data == NULL  || length > STREAM_BUFFER_SIZE) {
        //M1_LOG_E(TAG, "Write data error, len:%ld\r\n", length);
        return -1;
    }

    length = xStreamBufferSend(spi_master_tx_ring_buf, data, size, portMAX_DELAY);
    return length;
}


// notify slave to recv data
static void notify_slave_to_recv(void)
{
    if (initiative_send_flag == 0)
    {
        spi_mutex_lock();
        uint32_t tmp_send_len = xStreamBufferBytesAvailable(spi_master_tx_ring_buf);
        if (tmp_send_len > 0)
        {
            plan_send_len = tmp_send_len > SPI_TRANS_MAX_LEN ? SPI_TRANS_MAX_LEN : tmp_send_len;
            spi_master_request_to_write(current_send_seq + 1, plan_send_len); // to tell slave that the master want to write data
            initiative_send_flag = 1;
        }
        spi_mutex_unlock();
    }
}


static void spi_trans_control_task(void* arg)
{
    esp_err_t ret;
    spi_master_msg_t trans_msg = {0};
    uint32_t send_len = 0;
    esp_queue_elem_t *elem = NULL;
    char *app_resp = NULL;

    uint8_t *trans_data = (uint8_t*)malloc(SPI_TRANS_MAX_LEN * sizeof(uint8_t));
    if (trans_data == NULL)
    {
        //M1_LOG_E(TAG, "malloc fail\r\n");
        return;
    }

    while (1)
    {
        xQueueReceive(esp_spi_msg_queue, (void*)&trans_msg, (TickType_t)portMAX_DELAY);
        spi_mutex_lock();
        spi_recv_opt_t recv_opt = query_slave_data_trans_info();

        if (recv_opt.direct == SPI_WRITE)
        {
            if (plan_send_len == 0) {
                //M1_LOG_E(TAG, "master want send data but length is 0\r\n");
                continue;
            }

            if (recv_opt.seq_num != current_send_seq) {
                //M1_LOG_E(TAG, "SPI send seq error, %x, %x\r\n", recv_opt.seq_num, current_send_seq);
                if (recv_opt.seq_num == 1) {
                    //M1_LOG_E(TAG, "Maybe SLAVE restart, ignore\r\n");
                    current_send_seq = recv_opt.seq_num;
                } else {
                    break;
                }
            }

            //initiative_send_flag = 0;
            send_len = xStreamBufferReceive(spi_master_tx_ring_buf, (void*) trans_data, plan_send_len, 0);

            if (send_len != plan_send_len) {
                //M1_LOG_E(TAG, "Read len expect %lu, but actual read %lur\n", plan_send_len, send_len);
                break;
            }

            ret = spi_write_data(trans_data, plan_send_len);
            if (ret < 0) {
                //M1_LOG_E(TAG, "Load data error\r\n");
                return;
            }

            /* PSK hygiene (PR-11 review): the command bytes just moved
             * out over SPI -- scrub the region of this staging buffer
             * that carried them, so a secret-bearing command
             * (AT+CWJAP="<ssid>","<pwd>") does not persist here between
             * transactions. Task-local buffer, post-transmit: no other
             * reader, no concurrency. */
            {
                volatile uint8_t *wipe = trans_data;
                uint32_t wi;
                for ( wi=0; wi<plan_send_len; wi++ )
                    wipe[wi] = 0;
            }

            // maybe streambuffer filled some data when SPI transmit, just consider it after send done, because send flag has already in SLAVE queue
            uint32_t tmp_send_len = xStreamBufferBytesAvailable(spi_master_tx_ring_buf);
            if (tmp_send_len > 0) {
                plan_send_len = tmp_send_len > SPI_TRANS_MAX_LEN ? SPI_TRANS_MAX_LEN : tmp_send_len;
                spi_master_request_to_write(current_send_seq + 1, plan_send_len);
            } else {
                initiative_send_flag = 0;
            }

        } // if (recv_opt.direct == SPI_WRITE)
        else if (recv_opt.direct == SPI_READ)
        {
            if (recv_opt.seq_num != ((current_recv_seq + 1) & 0xFF)) {
                //M1_LOG_E(TAG, "SPI recv seq error, %x, %x\r\n", recv_opt.seq_num, (current_recv_seq + 1));
                if (recv_opt.seq_num == 1) {
                    //M1_LOG_E(TAG, "Maybe SLAVE restart, ignore\r\n");
                } else {
                    break;
                }
            }

            if (recv_opt.transmit_len > STREAM_BUFFER_SIZE || recv_opt.transmit_len == 0) {
                //M1_LOG_E(TAG, "SPI read len error, %x\r\n", recv_opt.transmit_len);
                break;
            }

            current_recv_seq = recv_opt.seq_num;
            memset(trans_data, 0x0, recv_opt.transmit_len);
            at_spi_master_recv_data(trans_data, recv_opt.transmit_len);
            at_spi_rddma_done();
            trans_data[recv_opt.transmit_len] = '\0';
#ifdef M1_APP_ESP_RESPONSE_PRINT_ENABLE
            printf("%s", trans_data);
            fflush(stdout);    //Force to print even if have not '\n'
#endif // #ifdef M1_APP_ESP_RESPONSE_PRINT_ENABLE
    		/* Allocate app struct for response */
    		app_resp = (uint8_t *)malloc(recv_opt.transmit_len + 1);
    		if (!app_resp)
    		{
    			//M1_LOG_E(TAG, "Failed to allocate app_resp %d\r\n", recv_opt.transmit_len + 1);
    			return;
    		}
    		strcpy(app_resp, trans_data);

    		xSemaphoreGive(esp_ctrl_req_sem);

    		elem = (esp_queue_elem_t*)malloc(sizeof(esp_queue_elem_t));
			if (!elem)
			{
				//M1_LOG_E(TAG, "%s %u: Malloc failed\n",__func__,__LINE__);
				return;
			}
			elem->buf = app_resp;
			elem->buf_len = recv_opt.transmit_len;
			elem->uid = current_uid;
			if ( esp_queue_put(ctrl_msg_Q, (void*)elem) )
			{
				//M1_LOG_E(TAG, "%s %u: ctrl Q put fail\r\n",__func__,__LINE__);
				if (elem)
					free(elem);
				return;
			} // if ( esp_queue_put(ctrl_msg_Q, (void*)elem) )

			xSemaphoreGive(esp_resp_read_sem);
        } // else if (recv_opt.direct == SPI_READ)
        else
        {
            //M1_LOG_D(TAG, "Unknown direct: %d", recv_opt.direct);
            spi_mutex_unlock();
            continue;
        }

        spi_mutex_unlock();
    } // while (1)

    free(trans_data);
    vTaskDelete(NULL);
}


uint8_t spi_AT_app_send_command(ctrl_cmd_t *app_req)
{
	int ret = SUCCESS;

	if (!app_req)
	{
		return CTRL_ERR_INCORRECT_ARG;
	}

	/* 1. Check if any ongoing request present
	 * Send failure in that case */
	ret = xSemaphoreTake(esp_ctrl_req_sem, SEC_TO_MILLISEC(WAIT_TIME_B2B_CTRL_REQ));
	if (ret!=pdPASS)
	{
		return CTRL_ERR_REQ_IN_PROG;
	}
	app_req->msg_type = CTRL_REQ;
	// handle rollover in uid value (range: 1 to INT32_MAX)
	if (uid < INT32_MAX)
		uid++;
	else
		uid = 1;
	app_req->uid = uid;

    write_data_to_spi_task_tx_ring_buf(app_req->at_cmd, app_req->cmd_len);
    notify_slave_to_recv();

    return SUCCESS;
} // uint8_t spi_AT_app_send_command(ctrl_cmd_t *app_req)


static uint8_t *spi_AT_app_get_response(int *read_len, uint32_t *uid, int timeout_sec)
{
	void *data = NULL;
	uint8_t *buf = NULL;
	esp_queue_elem_t *elem = NULL;
	int ret = 0;

	/* 1. Any problems in response, return NULL */
	if (!read_len)
	{
		//M1_LOG_E(TAG, "Invalid input parameter\r\n");
		return NULL;
	}

	/* 2. If timeout not specified, use default */
	if (!timeout_sec)
		timeout_sec = DEFAULT_CTRL_RESP_TIMEOUT;

	/* 3. Wait for response */
	ret = xSemaphoreTake(esp_resp_read_sem, SEC_TO_MILLISEC(timeout_sec));
	if (ret!=pdPASS)
	{
		//M1_LOG_E(TAG, "ESP response timed out after %u sec\r\n", timeout_sec);
		xSemaphoreGive(esp_ctrl_req_sem);
		return NULL;
	}

	/* 4. Fetch response from `esp_queue` */
	data = esp_queue_get(ctrl_msg_Q);
	if (data)
	{
		elem = (esp_queue_elem_t *)data;
		if (!elem)
			return NULL;

		*read_len = elem->buf_len;
		*uid = elem->uid;
		buf = elem->buf;
		free(elem);
		// B.N @ MonstaTek 2025
		if ( esp_queue_check(ctrl_msg_Q) ) // There's still data in the queue?
			xSemaphoreGive(esp_resp_read_sem); // Give the app the chance to read again
		return buf;
	}
	else
	{
		//M1_LOG_E(TAG, "Ctrl Q empty or uninitialized\r\n");
		return NULL;
	}

	return NULL;
} // static uint8_t *spi_AT_app_get_response(int *read_len, uint32_t *uid, int timeout_sec)


/* Conditionally scrub the SPI TX stream buffer's STORAGE after a
 * secret-bearing command (PR-11 review: the plaintext PSK of AT+CWJAP
 * rides join_cmd into spi_master_tx_ring_buf through
 * xStreamBufferSend, and FreeRTOS stream buffers do not zero storage
 * on read -- without this scrub those bytes persist in static RAM
 * until a later, longer command happens to overwrite them).
 *
 * CONDITIONAL BY DESIGN -- the scrub runs only when BOTH hold:
 *  1. The driver's single-flight semaphore (esp_ctrl_req_sem) is free
 *     within its 100 ms bound. The ONLY ring writer is
 *     spi_AT_app_send_command(), serialized on that semaphore, so
 *     holding it means no sender is mid-write and none can start.
 *     If it cannot be taken, the scrub is SKIPPED (never race another
 *     request).
 *  2. The ring is genuinely EMPTY (xStreamBufferIsEmpty). Occupancy,
 *     not free space, is the precondition: after a response TIMEOUT
 *     the semaphore is already released while the command bytes may
 *     still sit undrained in the ring (the slave never pulled the
 *     write). Draining a non-empty ring would pop the OLDEST bytes --
 *     the plaintext command -- into this function's buffers and
 *     silently replace queued TX bytes with zeros. A non-empty ring
 *     is left untouched and the scrub is SKIPPED (documented residual:
 *     those bytes persist in the ring storage until later traffic
 *     overwrites them).
 * The SPI control task never writes the buffer; it only drains it.
 * The scrub goes EXCLUSIVELY through the stream-buffer API (fill with
 * zeroes in chunks, drain each chunk straight back out); it never
 * calls notify_slave_to_recv(), so nothing is ever transmitted, and a
 * completed scrub leaves the ring empty exactly as it found it.
 * Coverage (derived from the in-tree FreeRTOS
 * Middlewares/.../stream_buffer.c): xStreamBufferCreate(N) allocates
 * N+1 storage bytes (xBufferSizeBytes++ before
 * prvInitialiseNewStreamBuffer; xLength = N+1) while
 * xStreamBufferSpacesAvailable reports N when empty -- the fill sends
 * exactly STREAM_BUFFER_SIZE = N bytes, so it overwrites every slot
 * of the N+1-slot storage EXCEPT one: the slot immediately behind the
 * initial write head. That residual slot holds the most recently
 * written pre-scrub byte -- the previous command's final '\n' (every
 * command written into this ring ends CRLF, via CONCAT_CMD_PARAM /
 * the join format string) or a zero from an earlier scrub -- never
 * credential material. A short-send/short-receive break aborts the
 * scrub and leaves the remainder unwritten (the defensive second
 * gates above). The drain
 * target is zeroized before return so it can never become a retention
 * site. */
static void esp_scrub_spi_tx_ring_buf(void)
{
	uint8_t zeros[128] = {0};
	uint8_t scratch[128];
	size_t done = 0;

	if ( (esp_ctrl_req_sem==NULL) || (spi_master_tx_ring_buf==NULL) )
		return;

	if ( xSemaphoreTake(esp_ctrl_req_sem, pdMS_TO_TICKS(100)) != pdPASS )
		return; /* condition 1 failed: another request in flight */

	if ( !xStreamBufferIsEmpty(spi_master_tx_ring_buf) )
	{
		xSemaphoreGive(esp_ctrl_req_sem);
		return; /* condition 2 failed: pending bytes, leave them to the driver */
	}

	while ( done < (size_t)STREAM_BUFFER_SIZE )
	{
		size_t chunk = (size_t)STREAM_BUFFER_SIZE - done;
		if ( chunk > sizeof(zeros) )
			chunk = sizeof(zeros);
		if ( xStreamBufferSend(spi_master_tx_ring_buf, zeros, chunk, 0) != chunk )
			break; /* defensive second gate: space vanished -> abort */
		if ( xStreamBufferReceive(spi_master_tx_ring_buf, (void*)scratch, chunk, 0) != chunk )
			break; /* defensive second gate: drain mismatch -> abort */
		done += chunk;
	}

	/* The drain target only ever held the zeros just pushed, but wipe it
	 * anyway (volatile stores) so no future edit can turn scratch into
	 * a plaintext retention site. */
	{
		volatile uint8_t *wipe = scratch;
		size_t wi;
		for ( wi=0; wi<sizeof(scratch); wi++ )
			wipe[wi] = 0;
	}

	xSemaphoreGive(esp_ctrl_req_sem);
} // static void esp_scrub_spi_tx_ring_buf(void)


static void init_master_hd(spi_device_handle_t* spi)
{
	spi_device_handle_t spi_dev;

	/* queue init */
	ctrl_msg_Q = create_esp_queue();
	if (!ctrl_msg_Q) {
		//M1_LOG_E(TAG, "Failed to create app ctrl msg Q\r\n");
		return;
	}
    // Create the message queue.
    esp_spi_msg_queue = xQueueCreate(5, sizeof(spi_master_msg_t));
    // Create the tx_buf.
    spi_master_tx_ring_buf = xStreamBufferCreate(STREAM_BUFFER_SIZE, 1);
    // Create the semaphore.
    pxMutex = xSemaphoreCreateMutex();

    /* semaphore init */
	esp_ctrl_req_sem = xSemaphoreCreateBinary();
	assert(esp_ctrl_req_sem);
	esp_resp_read_sem = xSemaphoreCreateBinary();
	assert(esp_resp_read_sem );
	/*
	Note that binary semaphores created using
	 * the vSemaphoreCreateBinary() macro are created in a state such that the
	 * first call to 'take' the semaphore would pass, whereas binary semaphores
	 * created using xSemaphoreCreateBinary() are created in a state such that the
	 * the semaphore must first be 'given' before it can be 'taken'
	 *
	*/
	/* Get read semaphore for first time */
	//xSemaphoreTake(esp_resp_read_sem, portMAX_DELAY);
	/* Give req semaphore for first time */
	//xSemaphoreGive(esp_ctrl_req_sem);

    spi_dev = pvPortMalloc(sizeof(spi_device_handle_t));
    assert(spi_dev!=NULL);
    spi_dev->id = ESP_SPI_ID;
    spi_dev->cfg.flags = 0;
    spi_dev->host = pvPortMalloc(sizeof(spi_host_t ));
    assert(spi_dev->host!=NULL);
    *spi = spi_dev;
    esp32_main_init_done = true;

    spi_mutex_lock();

    spi_recv_opt_t recv_opt = query_slave_data_trans_info();
    //M1_LOG_I(TAG, "now direct:%u\r\n", recv_opt.direct);

    if (recv_opt.direct == SPI_READ) { // if slave in waiting response status, master need to give a read done single.
        if (recv_opt.seq_num != ((current_recv_seq + 1) & 0xFF)) {
            //M1_LOG_E(TAG, "SPI recv seq error, %x, %x\r\n", recv_opt.seq_num, (current_recv_seq + 1));
            if (recv_opt.seq_num == 1) {
                //M1_LOG_E(TAG, "Maybe SLAVE restart, ignore\r\n");
            }
        }
        current_recv_seq = recv_opt.seq_num;
        at_spi_rddma_done();
    }

    spi_mutex_unlock();
} // static void init_master_hd(spi_device_handle_t* spi)


bool get_esp32_main_init_status(void)
{
	return esp32_main_init_done;
} // bool get_esp32_main_init_status(void)


/**
  * @brief Delay without context switch
  * @param  x in ms approximately
  * @retval None
  */
void hard_delay(uint32_t x)
{
    volatile uint32_t idx;

    for (idx=0; idx<6000*x; idx++) // 100
    {
    	;
    }
}


/**
  * @brief  Reset slave to initialize
  * @param  None
  * @retval None
  */
static void reset_slave(void)
{
	esp32_disable();
	hard_delay(1); // 50
	esp32_enable();
	/* stop spi transactions short time to avoid slave sync issues */
	hard_delay(200); // 50000
}



void esp32_main_init(void)
{
	BaseType_t ret;
	size_t free_heap;

	if ( esp32_main_init_done )
		return;

	reset_slave();

	init_master_hd(&spi_dev_handle);
    ret = xTaskCreate(spi_trans_control_task, "spi_trans_control_task", M1_TASK_STACK_SIZE_2048, NULL, TASK_PRIORITY_ESP32_TASKS, NULL);
	assert(ret==pdPASS);
	free_heap = xPortGetFreeHeapSize(); // xPortGetMinimumEverFreeHeapSize()
	assert(free_heap >= M1_LOW_FREE_HEAP_WARNING_SIZE);

	esp32_main_init_done = true;
} // void app_main(void)


static void esp_free_mem( char **buf_ptr)
{
	if ( *buf_ptr != NULL )
	{
		free (*buf_ptr);
		*buf_ptr = NULL;
	}
} // static void esp_free_mem( char **buf_ptr)


uint8_t wifi_ap_scan_list(ctrl_cmd_t *app_req)
{
	char *rx_buf = NULL;
	char *ok_buf = NULL;
	char *resp_buf = NULL;
	int rx_buf_len = 0;
	uint32_t rx_uid;
	uint8_t ret;
	uint32_t tick_t0, tick_pass;

	tick_t0 = HAL_GetTick();
	esp_queue_reset(ctrl_msg_Q);
	app_req->at_cmd = strdup(CONCAT_CMD_PARAM(ESP32C6_AT_REQ_WIFI_MODE, ESP32C6_WIFI_MODE_STA));
	app_req->cmd_resp = strdup(ESP32C6_AT_RES_OK);
	app_req->cmd_len = strlen(app_req->at_cmd);
	ret = spi_AT_app_send_command(app_req);
	if ( ret==SUCCESS )
	{
		ret = ERROR;
		while (true)
		{
			tick_pass = HAL_GetTick() - tick_t0;
			tick_pass /= MILLISEC_TO_SEC;
			if ( tick_pass ) // at least one second has passed?
			{
				tick_t0 += MILLISEC_TO_SEC; // Update tick_t0
				if ( app_req->cmd_timeout_sec > tick_pass )
				{
					app_req->cmd_timeout_sec -= tick_pass;
				}
				else
					break; // Timeout
			} // if ( tick_pass )
			esp_free_mem(&resp_buf);
			rx_buf = spi_AT_app_get_response(&rx_buf_len, &rx_uid, app_req->cmd_timeout_sec);
			resp_buf = rx_buf;
			rx_buf = m1_resp_string_strip(rx_buf, CR_LF);
			if ( !rx_buf )
				continue;
			if ( rx_uid != current_uid ) // Not the expected response?
				continue;
			if ( strcmp(rx_buf, app_req->cmd_resp) ) // Not the expected response?
				continue;
			ret = SUCCESS;
			break;
		} // while ( true )
		if ( ret==SUCCESS )
		{
			esp_free_mem(&app_req->at_cmd);
			esp_free_mem(&app_req->cmd_resp);
			app_req->at_cmd = strdup(CONCAT_CMD_PARAM(ESP32C6_AT_REQ_LIST_AP, ""));
			app_req->cmd_len = strlen(app_req->at_cmd);
			app_req->cmd_resp = NULL;
			ret = spi_AT_app_send_command(app_req);
			while ( ret==SUCCESS )
			{
				esp_free_mem(&resp_buf);
				rx_buf = spi_AT_app_get_response(&rx_buf_len, &rx_uid, app_req->cmd_timeout_sec);
				resp_buf = rx_buf;
				if ( rx_buf && rx_buf_len)
				{
					if ( rx_uid != current_uid ) // Not the expected response?
						continue;
					m1_parse_spi_at_resp(rx_buf, ESP32C6_AT_RES_LIST_AP_KEY, app_req);
					ok_buf = strstr(rx_buf, "OK");
					if ( ok_buf!=NULL ) // If "OK" found in the response, it's the last response to receive from the slave
						break; // Complete and exit
					tick_pass = HAL_GetTick() - tick_t0;
					tick_pass /= MILLISEC_TO_SEC;
					if ( tick_pass ) // at least one second has passed?
					{
						tick_t0 += MILLISEC_TO_SEC; // Update tick_t0
						if ( app_req->cmd_timeout_sec > tick_pass )
						{
							app_req->cmd_timeout_sec -= tick_pass;
						}
						else
							break; // Timeout
					} // if ( tick_pass )
				} // if ( rx_buf && rx_buf_len)
				else
					ret = ERROR;
			} // while ( ret==SUCCESS )
		} // if ( ret==SUCCESS )
	} // if ( ret==SUCCESS )
	esp_free_mem(&resp_buf);
	esp_free_mem(&app_req->at_cmd);
	esp_free_mem(&app_req->cmd_resp);
	if ( ret==SUCCESS )
	{
		app_req->msg_type = CTRL_RESP;
		app_req->resp_event_status = SUCCESS;
	} // if ( ret==SUCCESS )
	else
	{
		//M1_LOG_E(TAG, "Response not received\r\n");
	}

	return ret;
} // uint8_t wifi_ap_scan_list(ctrl_cmd_t *app_req)



/* Station join handler (PR-11, issue #42): CTRL_REQ_CONNECT_AP.
 *
 * Request shape (no in-tree requester exists yet -- this defines it for
 * the paired firmware): app_req->msg_id == CTRL_REQ_CONNECT_AP,
 * cmd_timeout_sec == DEFAULT_CTRL_RESP_CONNECT_AP_TIMEOUT (45 s), and
 * app_req->u.wifi_ap_config.ssid/.pwd carry NUL-terminated SSID/PSK
 * strings (<=32 / <=64 chars, no '"'/CR/LF -- the same grammar the
 * STM32 join flow enforces before it ever builds a command).
 *
 * Sequence (same shape as wifi_ap_scan_list above): put the radio in
 * station mode and wait OK, then send AT+CWJAP="<ssid>","<pwd>" and
 * wait for the association markers. Response codes carried in
 * resp_event_status as CTRL_RESP_CONNECT_AP:
 *   SUCCESS                      associated ("WIFI GOT IP"/"OK")
 *   CTRL_ERR_INVALID_ARGUMENT    ssid/psk failed the grammar checks
 *   CTRL_ERR_INVALID_PASSWORD    AT +CWJAP:<2> (wrong password)
 *   CTRL_ERR_NO_AP_FOUND         AT +CWJAP:<3> (AP not found)
 *   CTRL_ERR_NOT_CONNECTED       +CWJAP:<1|4|other> or FAIL
 *   CTRL_ERR_REQUEST_TIMEOUT     no marker before the timeout
 * The plaintext PSK exists in FOUR holders: the caller's request
 * struct (u.wifi_ap_config.ssid/.pwd -- the source this handler
 * reads), the join_cmd buffer, the SPI TX stream-buffer storage, and
 * the DMA staging region in the control task. Per exit path of this
 * handler (re-verified with the conditional TX scrub): the
 * request-struct fields are wiped (esp_wipe_ap_config_credentials())
 * on EVERY path that received them -- the three grammar-refusal early
 * returns, the malloc-failure return, and the common cleanup tail
 * (covering mode-set failure, send refusal, and all post-send exits);
 * the join_cmd buffer is wiped (volatile stores) on EVERY path that
 * built it -- send refusal, AT failure, success, and timeout
 * alike; the DMA staging region in the control task is wiped right
 * after EVERY actual transmit (the slave answering proves the transmit
 * happened); the SPI TX stream-buffer storage is scrubbed
 * CONDITIONALLY (esp_scrub_spi_tx_ring_buf(): only when the driver's
 * single-flight semaphore is free within its bound AND the ring is
 * genuinely empty -- both hold on the normal success/AT-failure paths
 * and on a timeout after the command was drained). Documented
 * residual: on a response timeout against a wedged slave that never
 * pulled the write -- or when another request holds the semaphore --
 * the scrub is skipped and the plaintext can remain in the ring
 * storage until later traffic overwrites it. Grammar-refusal,
 * mode-set-failure, and malloc-failure exits build no join command
 * at all, so only the request-struct copy ever exists on them (and
 * it is wiped). */

/* Wipe the caller's request-struct copy of the credentials -- the
 * fourth plaintext holder above. Volatile stores over the full field
 * width, so the plaintext cannot survive the handler on any exit path
 * that received it. */
static void esp_wipe_ap_config_credentials(ctrl_cmd_t *app_req)
{
	volatile uint8_t *wipe;
	size_t wi;

	wipe = app_req->u.wifi_ap_config.pwd;
	for ( wi=0; wi<sizeof(app_req->u.wifi_ap_config.pwd); wi++ )
		wipe[wi] = 0;
	wipe = app_req->u.wifi_ap_config.ssid;
	for ( wi=0; wi<sizeof(app_req->u.wifi_ap_config.ssid); wi++ )
		wipe[wi] = 0;
} // static void esp_wipe_ap_config_credentials(ctrl_cmd_t *app_req)

uint8_t wifi_connect_ap(ctrl_cmd_t *app_req)
{
	char *rx_buf = NULL;
	char *resp_buf = NULL;
	char *join_cmd = NULL;
	char *fail_buf = NULL;
	int rx_buf_len = 0;
	uint32_t rx_uid;
	uint8_t ret;
	uint8_t join_status = CTRL_ERR_REQUEST_TIMEOUT;
	uint32_t tick_t0, tick_pass;
	size_t i;

	tick_t0 = HAL_GetTick();
	esp_queue_reset(ctrl_msg_Q);

	/* Grammar checks BEFORE anything is sent: the AT command quotes both
	 * fields, so a '"' would terminate one early and CR/LF would split
	 * the line. */
	{
		const char *ssid = (const char *)app_req->u.wifi_ap_config.ssid;
		const char *pwd = (const char *)app_req->u.wifi_ap_config.pwd;
		size_t ssid_len = strnlen(ssid, SSID_LENGTH);
		size_t pwd_len = strnlen(pwd, PASSWORD_LENGTH);

		if ( (ssid_len == SSID_LENGTH) || (pwd_len == PASSWORD_LENGTH) )
		{
			esp_wipe_ap_config_credentials(app_req); /* the plaintext stays only in the struct */
			app_req->resp_event_status = CTRL_ERR_INVALID_ARGUMENT;
			return CTRL_ERR_INCORRECT_ARG;
		}
		for ( i=0; i<ssid_len; i++ )
			if ( (ssid[i]=='"') || (ssid[i]=='\r') || (ssid[i]=='\n') )
			{
				esp_wipe_ap_config_credentials(app_req);
				app_req->resp_event_status = CTRL_ERR_INVALID_ARGUMENT;
				return CTRL_ERR_INCORRECT_ARG;
			}
		for ( i=0; i<pwd_len; i++ )
			if ( (pwd[i]=='"') || (pwd[i]=='\r') || (pwd[i]=='\n') )
			{
				esp_wipe_ap_config_credentials(app_req);
				app_req->resp_event_status = CTRL_ERR_INVALID_ARGUMENT;
				return CTRL_ERR_INCORRECT_ARG;
			}
	}

	app_req->at_cmd = strdup(CONCAT_CMD_PARAM(ESP32C6_AT_REQ_WIFI_MODE, ESP32C6_WIFI_MODE_STA));
	app_req->cmd_resp = strdup(ESP32C6_AT_RES_OK);
	app_req->cmd_len = strlen(app_req->at_cmd);
	ret = spi_AT_app_send_command(app_req);
	if ( ret==SUCCESS )
	{
		ret = ERROR;
		while (true)
		{
			tick_pass = HAL_GetTick() - tick_t0;
			tick_pass /= MILLISEC_TO_SEC;
			if ( tick_pass ) // at least one second has passed?
			{
				tick_t0 += MILLISEC_TO_SEC; // Update tick_t0
				if ( app_req->cmd_timeout_sec > tick_pass )
				{
					app_req->cmd_timeout_sec -= tick_pass;
				}
				else
					break; // Timeout
			} // if ( tick_pass )
			esp_free_mem(&resp_buf);
			rx_buf = spi_AT_app_get_response(&rx_buf_len, &rx_uid, app_req->cmd_timeout_sec);
			resp_buf = rx_buf;
			rx_buf = m1_resp_string_strip(rx_buf, CR_LF);
			if ( !rx_buf )
				continue;
			if ( rx_uid != current_uid ) // Not the expected response?
				continue;
			if ( strcmp(rx_buf, app_req->cmd_resp) ) // Not the expected response?
				continue;
			ret = SUCCESS;
			break;
		} // while ( true )
		if ( ret==SUCCESS )
		{
			esp_free_mem(&app_req->at_cmd);
			esp_free_mem(&app_req->cmd_resp);
			/* AT+CWJAP="<ssid>","<pwd>" -- carries the plaintext PSK */
			join_cmd = malloc(SSID_LENGTH + PASSWORD_LENGTH + 24);
			if ( !join_cmd )
			{
				esp_free_mem(&resp_buf);
				esp_wipe_ap_config_credentials(app_req);
				app_req->resp_event_status = CTRL_ERR_MEMORY_FAILURE;
				return ERROR;
			}
			snprintf(join_cmd, SSID_LENGTH + PASSWORD_LENGTH + 24, "%s\"%s\",\"%s\"" ESP32C6_AT_REQ_CRLF,
					 ESP32C6_AT_REQ_CONNECT_AP,
					 (const char *)app_req->u.wifi_ap_config.ssid,
					 (const char *)app_req->u.wifi_ap_config.pwd);
			app_req->at_cmd = join_cmd;
			app_req->cmd_len = strlen(join_cmd);
			app_req->cmd_resp = NULL;
			ret = spi_AT_app_send_command(app_req);
			while ( ret==SUCCESS )
			{
				esp_free_mem(&resp_buf);
				rx_buf = spi_AT_app_get_response(&rx_buf_len, &rx_uid, app_req->cmd_timeout_sec);
				resp_buf = rx_buf;
				if ( rx_buf && rx_buf_len)
				{
					if ( rx_uid != current_uid ) // Not the expected response?
						continue;
					fail_buf = strstr(rx_buf, ESP32C6_AT_RES_CONN_AP_FAIL);
					if ( fail_buf!=NULL ) // "+CWJAP:<code>": the join was refused
					{
						const char *cp = fail_buf + strlen(ESP32C6_AT_RES_CONN_AP_FAIL);
						int code = 0;
						while ( (*cp>='0') && (*cp<='9') )
							code = (code*10) + (*(cp++) - '0');
						join_status = (code==2) ? CTRL_ERR_INVALID_PASSWORD :
						              (code==3) ? CTRL_ERR_NO_AP_FOUND :
						                          CTRL_ERR_NOT_CONNECTED;
						break;
					}
					if ( strstr(rx_buf, ESP32C6_AT_RES_CONN_GOT_IP)!=NULL ||
						 strstr(rx_buf, ESP32C6_AT_RES_OK)!=NULL )
					{
						join_status = SUCCESS; // Complete and exit
						break;
					}
					tick_pass = HAL_GetTick() - tick_t0;
					tick_pass /= MILLISEC_TO_SEC;
					if ( tick_pass ) // at least one second has passed?
					{
						tick_t0 += MILLISEC_TO_SEC; // Update tick_t0
						if ( app_req->cmd_timeout_sec > tick_pass )
						{
							app_req->cmd_timeout_sec -= tick_pass;
						}
						else
							break; // Timeout
					} // if ( tick_pass )
				} // if ( rx_buf && rx_buf_len)
				else
					ret = ERROR;
			} // while ( ret==SUCCESS )
			/* PSK hygiene: wipe EVERY copy that carried the plaintext --
			 * the join command buffer itself (volatile stores, so it
			 * cannot be elided), the SPI TX stream-buffer storage
			 * (esp_scrub_spi_tx_ring_buf), and the DMA staging region
			 * (scrubbed by the control task right after the transmit). */
			if ( join_cmd )
			{
				volatile char *wipe = join_cmd;
				while ( *wipe ) *(wipe++) = 0;
				esp_scrub_spi_tx_ring_buf();
			}
			app_req->at_cmd = NULL; /* join_cmd freed below with the others */
		} // if ( ret==SUCCESS )
	} // if ( ret==SUCCESS )
	/* Common cleanup tail -- every non-early-return exit passes here
	 * (mode-set failure, join send refusal, and all post-send exits):
	 * wipe the caller's request-struct copy of the credentials (the
	 * join_cmd buffer and the TX ring were already handled above). */
	esp_wipe_ap_config_credentials(app_req);
	esp_free_mem(&resp_buf);
	if ( join_cmd )
		free(join_cmd);
	esp_free_mem(&app_req->at_cmd);
	esp_free_mem(&app_req->cmd_resp);
	if ( join_status==SUCCESS )
	{
		ret = SUCCESS;
		app_req->msg_type = CTRL_RESP;
		app_req->msg_id = CTRL_RESP_CONNECT_AP;
		app_req->resp_event_status = SUCCESS;
	} // if ( join_status==SUCCESS )
	else
	{
		ret = ERROR;
		app_req->msg_id = CTRL_RESP_CONNECT_AP;
		app_req->resp_event_status = join_status;
	} // else

	return ret;
} // uint8_t wifi_connect_ap(ctrl_cmd_t *app_req)



uint8_t ble_scan_list(ctrl_cmd_t *app_req)
{
	char *rx_buf = NULL;
	char *ok_buf = NULL;
	char *resp_buf = NULL;
	int rx_buf_len = 0;
	uint32_t rx_uid;
	uint8_t ret;
	uint32_t tick_t0, tick_pass;

	tick_t0 = HAL_GetTick();
	esp_queue_reset(ctrl_msg_Q);
	app_req->at_cmd = strdup(CONCAT_CMD_PARAM(ESP32C6_AT_REQ_BLE_MODE, ESP32C6_BLE_MODE_CLI));
	app_req->cmd_resp = strdup(ESP32C6_AT_RES_OK);
	app_req->cmd_len = strlen(app_req->at_cmd);
	ret = spi_AT_app_send_command(app_req);
	if ( ret==SUCCESS )
	{
		ret = ERROR;
		while (true)
		{
			tick_pass = HAL_GetTick() - tick_t0;
			tick_pass /= MILLISEC_TO_SEC;
			if ( tick_pass ) // at least one second has passed?
			{
				tick_t0 += MILLISEC_TO_SEC; // Update tick_t0
				if ( app_req->cmd_timeout_sec > tick_pass )
				{
					app_req->cmd_timeout_sec -= tick_pass;
				}
				else
					break; // Timeout
			} // if ( tick_pass )
			esp_free_mem(&resp_buf);
			rx_buf = spi_AT_app_get_response(&rx_buf_len, &rx_uid, app_req->cmd_timeout_sec);
			resp_buf = rx_buf;
			rx_buf = m1_resp_string_strip(rx_buf, CR_LF);
			if ( !rx_buf )
				continue;
			if ( rx_uid != current_uid ) // Not the expected response?
				continue;
			if ( strcmp(rx_buf, app_req->cmd_resp) ) // Not the expected response?
				continue;
			ret = SUCCESS;
			break;
		} // while ( true )
		if ( ret==SUCCESS )
		{
			esp_free_mem(&app_req->at_cmd);
			esp_free_mem(&app_req->cmd_resp);
			app_req->at_cmd = strdup(CONCAT_CMD_PARAM(ESP32C6_AT_REQ_BLE_SCAN, "1")); // Scan for 3 seconds, hard coded
			app_req->cmd_len = strlen(app_req->at_cmd);
			app_req->cmd_resp = NULL;
			ret = spi_AT_app_send_command(app_req);
			while ( ret==SUCCESS )
			{
				esp_free_mem(&resp_buf);
				rx_buf = spi_AT_app_get_response(&rx_buf_len, &rx_uid, app_req->cmd_timeout_sec);
				resp_buf = rx_buf;
				if ( rx_buf && rx_buf_len)
				{
					if ( rx_uid != current_uid ) // Not the expected response?
						continue;
					m1_parse_spi_at_resp(rx_buf, ESP32C6_AT_RES_BLE_SCAN_KEY, app_req);
					ok_buf = strstr(rx_buf, "+BLESCANDONE");
					if ( ok_buf!=NULL ) // If "+BLESCANDONE" found in the response, it's the last response to receive from the slave
					{
						break; // Complete and exit
					}
					tick_pass = HAL_GetTick() - tick_t0;
					tick_pass /= MILLISEC_TO_SEC;
					if ( tick_pass ) // at least one second has passed?
					{
						tick_t0 += MILLISEC_TO_SEC; // Update tick_t0
						if ( app_req->cmd_timeout_sec > tick_pass )
						{
							app_req->cmd_timeout_sec -= tick_pass;
						}
						else
							break; // Timeout
					} // if ( tick_pass )
				} // if ( rx_buf && rx_buf_len)
				else
				{
					ret = ERROR;
					break;
				} // else
			} // while ( ret==SUCCESS )
		} // if ( ret==SUCCESS )
	} // if ( ret==SUCCESS )
	esp_free_mem(&resp_buf);
	esp_free_mem(&app_req->at_cmd);
	esp_free_mem(&app_req->cmd_resp);
	if ( ret==SUCCESS )
	{
		app_req->msg_type = CTRL_RESP;
		app_req->resp_event_status = SUCCESS;
	} // if ( ret==SUCCESS )
	else
	{
		M1_LOG_E(TAG, "Response not received\r\n");
	}

	return ret;
} // uint8_t ble_scan_list(ctrl_cmd_t *app_req)



uint8_t ble_advertise(ctrl_cmd_t *app_req)
{
	char *rx_buf = NULL;
	char *ok_buf = NULL;
	char *resp_buf = NULL;
	int rx_buf_len = 0;
	uint32_t rx_uid;
	uint8_t ret;
	uint32_t tick_t0, tick_pass;

	tick_t0 = HAL_GetTick();
	esp_queue_reset(ctrl_msg_Q);

	app_req->at_cmd = strdup(CONCAT_CMD_PARAM(ESP32C6_AT_REQ_BLE_MODE, ESP32C6_BLE_MODE_SER));
	app_req->cmd_resp = strdup(ESP32C6_AT_RES_OK);
	app_req->cmd_len = strlen(app_req->at_cmd);
	ret = spi_AT_app_send_command(app_req);
	if ( ret==SUCCESS )
	{
		ret = ERROR;
		while (true)
		{
			tick_pass = HAL_GetTick() - tick_t0;
			tick_pass /= MILLISEC_TO_SEC;
			if ( tick_pass ) // at least one second has passed?
			{
				tick_t0 += MILLISEC_TO_SEC; // Update tick_t0
				if ( app_req->cmd_timeout_sec > tick_pass )
				{
					app_req->cmd_timeout_sec -= tick_pass;
				}
				else
					break; // Timeout
			} // if ( tick_pass )
			esp_free_mem(&resp_buf);
			vTaskDelay(100); // Give the system some time to avoid possible crash for unknown reason! B.N 2025
			rx_buf = spi_AT_app_get_response(&rx_buf_len, &rx_uid, app_req->cmd_timeout_sec);
			resp_buf = rx_buf;
			rx_buf = m1_resp_string_strip(rx_buf, CR_LF);
			if ( !rx_buf )
				continue;
			if ( rx_uid != current_uid ) // Not the expected response?
				continue;
			if ( strcmp(rx_buf, app_req->cmd_resp) ) // Not the expected response?
				continue;
			ret = SUCCESS;
			break;
		} // while ( true )
		if ( ret==SUCCESS )
		{
			esp_free_mem(&app_req->at_cmd);
			esp_free_mem(&app_req->cmd_resp);
			app_req->at_cmd = strdup(CONCAT_CMD_PARAM(ESP32C6_AT_REQ_ADVERTISE, ESP32C6_AT_REQ_ADV_DATA));
			app_req->cmd_len = strlen(app_req->at_cmd);
			app_req->cmd_resp = NULL;
			ret = spi_AT_app_send_command(app_req);
			while ( true )
			{
				esp_free_mem(&resp_buf);
				vTaskDelay(100); // Give the system some time to avoid possible crash for unknown reason! B.N 2025
				rx_buf = spi_AT_app_get_response(&rx_buf_len, &rx_uid, app_req->cmd_timeout_sec);
				resp_buf = rx_buf;
				if ( rx_buf && rx_buf_len)
				{
					if ( rx_uid != current_uid ) // Not the expected response?
						continue;
					ok_buf = strstr(rx_buf, "OK");
					if ( ok_buf!=NULL ) // If "OK" found in the response, it's the last response to receive from the slave
					{
						break; // Complete and exit
					}
					tick_pass = HAL_GetTick() - tick_t0;
					tick_pass /= MILLISEC_TO_SEC;
					if ( tick_pass ) // at least one second has passed?
					{
						tick_t0 += MILLISEC_TO_SEC; // Update tick_t0
						if ( app_req->cmd_timeout_sec > tick_pass )
						{
							app_req->cmd_timeout_sec -= tick_pass;
						}
						else
							break; // Timeout
					} // if ( tick_pass )
				} // if ( rx_buf && rx_buf_len)
				else
				{
					ret = ERROR;
					break;
				}
			} // while ( true )
		} // if ( ret==SUCCESS )
	} // if ( ret==SUCCESS )
	esp_free_mem(&resp_buf);
	esp_free_mem(&app_req->at_cmd);
	esp_free_mem(&app_req->cmd_resp);
	if ( ret==SUCCESS )
	{
		app_req->msg_type = CTRL_RESP;
		app_req->resp_event_status = SUCCESS;
	} // if ( ret==SUCCESS )
	else
	{
		//M1_LOG_E(TAG, "Response not received\r\n");
	}

	return ret;
} // uint8_t ble_advertise(ctrl_cmd_t *app_req)



uint8_t esp_dev_reset(ctrl_cmd_t *app_req)
{
	char *rx_buf = NULL;
	char *ok_buf = NULL;
	char *resp_buf = NULL;
	int rx_buf_len = 0;
	uint32_t rx_uid;
	uint8_t ret, got_at_reset = 0;
	uint32_t tick_t0, tick_pass;

	tick_t0 = HAL_GetTick();
	esp_queue_reset(ctrl_msg_Q);
	app_req->at_cmd = strdup(CONCAT_CMD_PARAM(ESP32C6_AT_RESET, ""));
	app_req->cmd_resp = strdup(ESP32C6_AT_RES_READY);
	app_req->cmd_len = strlen(app_req->at_cmd);
	ret = spi_AT_app_send_command(app_req);
	if ( ret==SUCCESS )
	{
		ret = ERROR;
		while (true)
		{
			tick_pass = HAL_GetTick() - tick_t0;
			tick_pass /= MILLISEC_TO_SEC;
			if ( tick_pass ) // at least one second has passed?
			{
				tick_t0 += MILLISEC_TO_SEC; // Update tick_t0
				if ( app_req->cmd_timeout_sec > tick_pass )
				{
					app_req->cmd_timeout_sec -= tick_pass;
				}
				else
					break; // Timeout
			} // if ( tick_pass )
			esp_free_mem(&resp_buf);
			rx_buf = spi_AT_app_get_response(&rx_buf_len, &rx_uid, app_req->cmd_timeout_sec);
			resp_buf = rx_buf;
			rx_buf = m1_resp_string_strip(rx_buf, CR_LF);
			if ( !rx_buf )
				continue;
			if ( rx_uid != current_uid ) // Not the expected response?
				continue;
			if ( !got_at_reset )
			{
				if ( strcmp(rx_buf, ESP32C6_AT_RESET) ) // Not the expected response?
					continue;
				got_at_reset = 1;
				continue;
			} // if ( !got_at_reset )
			else
			{
				if ( strcmp(rx_buf, app_req->cmd_resp) ) // Not the expected response?
					continue;
			}
			ret = SUCCESS;
			break;
		} // while ( true )
	} // if ( ret==SUCCESS )
	esp_free_mem(&resp_buf);
	esp_free_mem(&app_req->at_cmd);
	esp_free_mem(&app_req->cmd_resp);
	if ( ret==SUCCESS )
	{
		app_req->msg_type = CTRL_RESP;
		app_req->resp_event_status = SUCCESS;
	} // if ( ret==SUCCESS )
	else
	{
		//M1_LOG_E(TAG, "Response not received\r\n");
	}

	return ret;
} // uint8_t esp_dev_reset(ctrl_cmd_t *app_req)
