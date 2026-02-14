/* See COPYING.txt for license details. */

/*
 * lfrfid_protocol_em4100.h
 */

#ifndef LFRFID_PROTOCOL_EM4100_H_
#define LFRFID_PROTOCOL_EM4100_H_


#define EM4100_DECODED_DATA_SIZE (5)

#define FRAME_BITS (64)
#define PREAMBLE_BITS (9)
#define FRAME_BUFFER_BYTES (8)

// EM4100 클럭 속도에 따른 하프 비트 시간 (T_h)
#define T_64_US (64)
#define T_128_US (128)
#define T_256_US (256)

#if 0
/**
 * @brief 엣지 이벤트 구조체: 마이크로컨트롤러의 타이머 캡처 결과
 */
typedef struct {
    uint16_t t_us;       // 이전 엣지로부터의 시간 간격 (microseconds)
    uint8_t edge;     // 이 엣지 이후의 신호 레벨 (0 또는 1)
} lfrfid_evt_t;
#endif
/**
 * @brief 디코더 상태 정의
 */
typedef enum {
    DECODER_STATE_IDLE,
    DECODER_STATE_SYNC,
	DECODER_STATE_VERIFY,
    DECODER_STATE_DATA
} DecoderState_t;

/**
 * @brief EM4100 디코더 상태 구조체
 */
typedef struct {
    //DecoderState_t state;
    uint16_t detected_half_bit_us;

    // 프레임 버퍼 및 비트 카운터
    uint8_t frame_buffer[FRAME_BUFFER_BYTES];
    uint8_t bit_count;
   //uint32_t bit_test;

    // 엣지 버퍼
    lfrfid_evt_t edge_buffer[FRAME_CHUNK_SIZE];
    uint8_t edge_count;

    //uint8_t sync_bit_count;
    //uint8_t verify_edge_count;
} EM4100_Decoder_t;

// 외부 API
//void setEm4100_bitrate(int bitrate);
void EM4100_Decoder_Init_Full(EM4100_Decoder_t* dec);
void EM4100_Decoder_Init_Partial(EM4100_Decoder_t* dec);
#if 0
/* EM4100 계열 판별기
 *
 *  - EM4100 /16 /32 /64 세 모드를 모두 검사하여
 *    가장 잘 맞는 모드의 프로토콜을 out->proto에 세팅
 *    (EM4100_16 / EM4100_32 / EM4100 중 하나)
 *  - out에는 match_ratio, score, total_count, match_count,
 *    ref_half_us, ref_full_us 까지 채움
 */
void LFRFID_EM4100_ScoreTiming(
    const lfrfid_evt_t   *evts,
    uint16_t              evt_count,
    LFRFIDProtoCandidate *out);
#endif
extern const LFRFIDProtocolBase protocol_em4100;
extern const LFRFIDProtocolBase protocol_em4100_32;
extern const LFRFIDProtocolBase protocol_em4100_16;

#endif /* LFRFID_PROTOCOL_EM4100_H_ */
