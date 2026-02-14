/* See COPYING.txt for license details. */

/*
 * lfrfid_protocol_h10301.h
 */

#ifndef LFRFID_PROTOCOL_H10301_H_
#define LFRFID_PROTOCOL_H10301_H_


/* ───────── FSK half → symbol(0/1) 상태 ───────── */

typedef struct {
    lfrfid_evt_t prev_half;
    bool         has_prev;
} fsk_symbol_state_t;

/* 초기화 */
void fsk_symbol_state_init(fsk_symbol_state_t *st);

/**
 * half 하나를 입력으로 넣는다.
 *
 * - 내부에 half 하나를 저장해 두었다가, 두 개가 모이면
 *   period = half0.t_us + half1.t_us
 *   로 주기를 계산해서:
 *      ~64us → symbol=0
 *      ~80us → symbol=1
 *
 * - 아직 half가 하나만 있을 때는 false 반환 (심볼 없음)
 *
 * @return
 *   true  : out_symbol에 0 또는 1이 채워짐
 *   false : 아직 심볼이 안 만들어짐
 */
bool fsk_symbol_feed(fsk_symbol_state_t *st,
                     const lfrfid_evt_t *half,
                     uint8_t *out_symbol);

/* ───────── symbol 스트림 → data bit 상태 ───────── */

typedef struct {
    uint8_t buf[64];   // 심볼 임시 버퍼 (0/1)
    uint8_t count;     // 현재 심볼 개수
} fsk_bit_state_t;

void fsk_bit_state_init(fsk_bit_state_t *st);

/**
 * symbol(0/1)을 하나 집어넣고,
 *
 *  - 1이 5개 모이면 → 데이터 비트 1
 *  - 0이 6개 모이면 → 데이터 비트 0
 *
 * 가 만들어지면 true 반환 + out_bit에 저장.
 * 비트가 여러 개 나올 수 있어도 **한 번 호출당 최대 1비트만 반환**.
 */
bool fsk_bit_feed(fsk_bit_state_t *st,
                  uint8_t symbol,
                  uint8_t *out_bit);



/* ============================================================
 * H10301 Decoder
 *  - 96bit shift register buffer
 *  - bit_count: 현재까지 채워진 bit 수
 * ============================================================ */
#define H10301_DECODER_BITS   96U
#define H10301_DECODER_WORDS  3U

typedef struct {
    uint32_t word[H10301_DECODER_WORDS];  // [0] = MSB, [2] = LSB
    uint8_t  bit_count;                    // 0 ~ 96
    //uint32_t bit_test;
} h10301_Decoder_t;

/* 디코더 초기화 */
void h10301_decoder_begin(h10301_Decoder_t *dec);

/* RAW 비트 push (1비트 shift) */
void h10301_decoder_push_bit(h10301_Decoder_t *dec, uint8_t bit);

/* 디코더가 96비트 frame을 모두 채웠는지 */
static inline bool h10301_decoder_is_full(const h10301_Decoder_t *dec) {
    return dec && (dec->bit_count >= H10301_DECODER_BITS);
}

/* 96bit frame 데이터를 반환 */
static inline const uint32_t* h10301_decoder_frame(const h10301_Decoder_t *dec) {
    return dec ? dec->word : 0;
}

/* ============================================================
 * RAW → 심볼(0/1) → decoder push
 * ============================================================ */
void h10301_insert_raw_events(const lfrfid_evt_t *events,
                              int event_count,
                              h10301_Decoder_t *dec);

/* ============================================================
 * Decoder frame이 HID H10301인지 판별
 * ============================================================ */
bool h10301_is_valid(const uint32_t *frame96);

/* ============================================================
 * 96bit frame → 26bit raw 추출
 * ============================================================ */
bool h10301_extract_raw26(const uint32_t *frame96, uint32_t *out_raw26);

/* ============================================================
 * 26bit raw → facility/card number 추출
 * ============================================================ */
bool h10301_extract_fields(uint32_t raw26,
                           uint8_t *facility_code,
                           uint16_t *card_number);

extern const LFRFIDProtocolBase protocol_h10301;

#if 0
/* H10301 (HID 26bit) 판별기
 *
 *  - HID half period(timing)를 기준으로
 *    match_ratio, score, total_count, match_count,
 *    ref_half_us, ref_full_us 를 채운다.
 */
void LFRFID_H10301_ScoreTiming(
    const lfrfid_evt_t   *evts,
    uint16_t              evt_count,
    LFRFIDProtoCandidate *out);
#endif
#endif /* LFRFID_PROTOCOL_H10301_H_ */
