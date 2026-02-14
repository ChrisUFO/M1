/* See COPYING.txt for license details. */

/*
 * lfrfid_hal.h
 */

#ifndef LFRFID_HAL_H_
#define LFRFID_HAL_H_

void rfid_read_handler(TIM_HandleTypeDef *htim);
void rfid_emul_handler(TIM_HandleTypeDef *htim);

void lfrfid_read_hw_init(void);
void lfrfid_read_hw_deinit(void);

void lfrfid_emul_hw_init(void);
void lfrfid_emul_hw_deinit(void);

void lfrfid_RFIDOut_Init(uint32_t freq);
void lfrfid_RFIDIn_Init(void);

extern TIM_HandleTypeDef   Timerhdl_RfIdTIM5;
extern TIM_HandleTypeDef   Timerhdl_RfIdTIM3;

extern uint8_t rfid_rxtx_is_taking_this_irq;
extern void lfrfid_isr_init(void);

//***************************************************************************
// emulation
//***************************************************************************
#define ENCODED_DATA_MAX 1200

/* 한 스텝: BSRR 값 + 유지시간(us) */
typedef struct
{
    uint32_t bsrr;      /* GPIOx->BSRR 에 쓸 값 (SET/RESET) */
    uint16_t time_us;   /* 이 스텝이 유지될 시간 (마이크로초) */
} Encoded_Data_t;

/* 파형 데이터: 스텝 배열 + 길이 */
typedef struct
{
    Encoded_Data_t *data;  /* Encoded_Data_t 배열 포인터 */
    uint16_t  length; /* 스텝 개수 */
    uint16_t  index;       /* 현재 스텝 인덱스 */
} EncodedTx_Data_t;

#if 0
/* 파형 송신 핸들 */
typedef struct
{
    /* --- 하드웨어 리소스 --- */
    TIM_HandleTypeDef *htim;        /* 사용할 타이머 (예: &htim5) */
    DMA_HandleTypeDef *hdma;        /* 타이머 Update에 연결된 DMA (예: &hdma_tim5_up) */
    GPIO_TypeDef      *gpio_port;   /* 파형을 출력할 GPIO 포트 (예: GPIOA) */

    IRQn_Type          tim_irqn;    /* 타이머 IRQ 번호 (예: TIM5_IRQn) */
    IRQn_Type          dma_irqn;    /* DMA IRQ 번호 (예: GPDMA1_Channel0_IRQn) */

    uint32_t           gpio_bsrr_addr; /* gpio_port->BSRR 주소 캐시 */

    /* --- 파형 데이터 --- */
    EncodedTx_Data_t      data;        /* 현재 사용 중인 파형 데이터 */
    volatile uint16_t  index;       /* 현재 스텝 인덱스 */
    volatile uint8_t   busy;        /* 1이면 전송 중, 0이면 idle */

} WaveTx_HandleTypeDef;
#endif

extern EncodedTx_Data_t lfrfid_encoded_data;

#endif /* LFRFID_HAL_H_ */
