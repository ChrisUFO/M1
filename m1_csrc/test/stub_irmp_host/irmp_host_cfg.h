/* See COPYING.txt for license details. */
/*
* irmp_host_cfg.h -- configuration under which the golden-decode host test
* compiles the REAL vendored IRMP/IRSND (Infrared/irmp-irsnd/, untouched).
*
* Mechanism: both irmp.h and irsnd.h skip their built-in irmpconfig.h /
* irsndconfig.h when IRMP_USE_AS_LIB / IRSND_USE_AS_LIB are defined (the
* upstream "use as library" hook), so this file supplies every macro they
* would have provided. The vendored sources are compiled VERBATIM.
*
* RX (IRMP_SUPPORT_*): the firmware's four enabled decoders (SIRCS, NEC,
* SAMSUNG, KASEIKYO) plus every additional decoder the IR-Data corpus needs.
* Upstream mutual-exclusion rules (enforced by #undef in irmp.h) force these
* documented corpus skips: FDC/RCCAR/ORTEK/S100 conflict with RC5; RUWIDO
* conflicts with DENON; RCII conflicts with GRUNDIG; FAN conflicts with
* NUBERT; PANASONIC conflicts with KASEIKYO; ROOMBA conflicts with RC6. RC5,
* DENON, GRUNDIG, NUBERT and KASEIKYO win because they are the protocols the
* official vocabulary and the common captures rely on. RF decoders stay off
* (the RF capture files are not infrared).
*
* TX (IRSND_SUPPORT_*): exact mirror of the firmware's irsndconfig.h -- the
* round-trip vectors must exercise the transmit capability the device
* actually ships.
*/
#ifndef IRMP_HOST_CFG_H_
#define IRMP_HOST_CFG_H_

#define IRMP_USE_AS_LIB 1
#define IRSND_USE_AS_LIB 1
#define F_INTERRUPTS 20000

/* --- decoder enables: firmware four + corpus --------------------------- */
#define IRMP_SUPPORT_SIRCS_PROTOCOL             1
#define IRMP_SUPPORT_NEC_PROTOCOL               1
#define IRMP_SUPPORT_SAMSUNG_PROTOCOL           1
#define IRMP_SUPPORT_KASEIKYO_PROTOCOL          1
#define IRMP_SUPPORT_MATSUSHITA_PROTOCOL        1
#define IRMP_SUPPORT_DENON_PROTOCOL             1
#define IRMP_SUPPORT_RC5_PROTOCOL               1
#define IRMP_SUPPORT_RC6_PROTOCOL               1
#define IRMP_SUPPORT_JVC_PROTOCOL               1
#define IRMP_SUPPORT_NEC16_PROTOCOL             1
#define IRMP_SUPPORT_NEC42_PROTOCOL             1
#define IRMP_SUPPORT_IR60_PROTOCOL              1
#define IRMP_SUPPORT_GRUNDIG_PROTOCOL           1
#define IRMP_SUPPORT_SIEMENS_PROTOCOL           1
#define IRMP_SUPPORT_NOKIA_PROTOCOL             1
#define IRMP_SUPPORT_RECS80_PROTOCOL            1
#define IRMP_SUPPORT_RECS80EXT_PROTOCOL         1
#define IRMP_SUPPORT_BOSE_PROTOCOL              0 /* conflicts with RC5 (tightens RC5 tolerance past real captures) */
#define IRMP_SUPPORT_KATHREIN_PROTOCOL          1
#define IRMP_SUPPORT_NUBERT_PROTOCOL            1
#define IRMP_SUPPORT_SPEAKER_PROTOCOL           1
#define IRMP_SUPPORT_BANG_OLUFSEN_PROTOCOL      1
#define IRMP_SUPPORT_THOMSON_PROTOCOL           1
#define IRMP_SUPPORT_NIKON_PROTOCOL             1
#define IRMP_SUPPORT_TELEFUNKEN_PROTOCOL        1
#define IRMP_SUPPORT_RCMM_PROTOCOL              1
#define IRMP_SUPPORT_SAMSUNG48_PROTOCOL         1
#define IRMP_SUPPORT_MERLIN_PROTOCOL            1
#define IRMP_SUPPORT_PENTAX_PROTOCOL            1
#define IRMP_SUPPORT_A1TVBOX_PROTOCOL           1
#define IRMP_SUPPORT_TECHNICS_PROTOCOL          1
#define IRMP_SUPPORT_VINCENT_PROTOCOL           1
#define IRMP_SUPPORT_METZ_PROTOCOL              1
#define IRMP_SUPPORT_MELINERA_PROTOCOL          1

/* --- documented corpus skips (upstream conflicts / no IR decoder) ------ */
#define IRMP_SUPPORT_FDC_PROTOCOL               0 /* conflicts with RC5 */
#define IRMP_SUPPORT_RCCAR_PROTOCOL             0 /* conflicts with RC5 */
#define IRMP_SUPPORT_ORTEK_PROTOCOL             0 /* conflicts with RC5 */
#define IRMP_SUPPORT_S100_PROTOCOL              0 /* conflicts with RC5 */
#define IRMP_SUPPORT_RUWIDO_PROTOCOL            0 /* conflicts with DENON */
#define IRMP_SUPPORT_RCII_PROTOCOL              0 /* conflicts with GRUNDIG */
#define IRMP_SUPPORT_FAN_PROTOCOL               0 /* conflicts with NUBERT */
#define IRMP_SUPPORT_PANASONIC_PROTOCOL         0 /* conflicts with KASEIKYO */
#define IRMP_SUPPORT_ROOMBA_PROTOCOL            0 /* conflicts with RC6 */
#define IRMP_SUPPORT_LGAIR_PROTOCOL             0
#define IRMP_SUPPORT_MITSU_HEAVY_PROTOCOL       0
#define IRMP_SUPPORT_SAMSUNGAH_PROTOCOL         0
#define IRMP_SUPPORT_IRMP16_PROTOCOL            0
#define IRMP_SUPPORT_GREE_PROTOCOL              0
#define IRMP_SUPPORT_ACP24_PROTOCOL             0
#define IRMP_SUPPORT_NETBOX_PROTOCOL            0
#define IRMP_SUPPORT_LEGO_PROTOCOL              0
#define IRMP_SUPPORT_RF_GEN24_PROTOCOL          0 /* RF, not IR */
#define IRMP_SUPPORT_RF_X10_PROTOCOL            0 /* RF, not IR */
#define IRMP_SUPPORT_RF_MEDION_PROTOCOL         0 /* RF, not IR */

/* --- IRMP behavior macros (mirror the firmware defaults) --------------- */
#define IRMP_HIGH_ACTIVE                        0
#define IRMP_ENABLE_RELEASE_DETECTION           0
#define IRMP_LOGGING                            0
#define IRMP_EXT_LOGGING                        0
#define IRMP_PROTOCOL_NAMES                     1
#define IRMP_USE_IDLE_CALL                      0

/* --- encoder enables: exact mirror of irsndconfig.h -------------------- */
#define IRSND_SUPPORT_SIRCS_PROTOCOL            1
#define IRSND_SUPPORT_NEC_PROTOCOL              1
#define IRSND_SUPPORT_SAMSUNG_PROTOCOL          1
#define IRSND_SUPPORT_MATSUSHITA_PROTOCOL       1
#define IRSND_SUPPORT_KASEIKYO_PROTOCOL         1
#define IRSND_SUPPORT_DENON_PROTOCOL            0
#define IRSND_SUPPORT_RC5_PROTOCOL              1
#define IRSND_SUPPORT_RC6_PROTOCOL              1
#define IRSND_SUPPORT_RC6A_PROTOCOL             0
#define IRSND_SUPPORT_JVC_PROTOCOL              0
#define IRSND_SUPPORT_NEC16_PROTOCOL            1
#define IRSND_SUPPORT_NEC42_PROTOCOL            1
#define IRSND_SUPPORT_IR60_PROTOCOL             0
#define IRSND_SUPPORT_GRUNDIG_PROTOCOL          0
#define IRSND_SUPPORT_SIEMENS_PROTOCOL          0
#define IRSND_SUPPORT_NOKIA_PROTOCOL            0
#define IRSND_SUPPORT_BOSE_PROTOCOL             0
#define IRSND_SUPPORT_KATHREIN_PROTOCOL         0
#define IRSND_SUPPORT_NUBERT_PROTOCOL           0
#define IRSND_SUPPORT_FAN_PROTOCOL              0
#define IRSND_SUPPORT_SPEAKER_PROTOCOL          0
#define IRSND_SUPPORT_BANG_OLUFSEN_PROTOCOL     0
#define IRSND_SUPPORT_RECS80_PROTOCOL           0
#define IRSND_SUPPORT_RECS80EXT_PROTOCOL        0
#define IRSND_SUPPORT_THOMSON_PROTOCOL          0
#define IRSND_SUPPORT_NIKON_PROTOCOL            0
#define IRSND_SUPPORT_NETBOX_PROTOCOL           0
#define IRSND_SUPPORT_ORTEK_PROTOCOL            0
#define IRSND_SUPPORT_TELEFUNKEN_PROTOCOL       0
#define IRSND_SUPPORT_FDC_PROTOCOL              0
#define IRSND_SUPPORT_RCCAR_PROTOCOL            0
#define IRSND_SUPPORT_ROOMBA_PROTOCOL           0
#define IRSND_SUPPORT_RUWIDO_PROTOCOL           0
#define IRSND_SUPPORT_A1TVBOX_PROTOCOL          0
#define IRSND_SUPPORT_LEGO_PROTOCOL             0
#define IRSND_SUPPORT_RCMM_PROTOCOL             0
#define IRSND_SUPPORT_LGAIR_PROTOCOL            0
#define IRSND_SUPPORT_SAMSUNG48_PROTOCOL        0
#define IRSND_SUPPORT_PENTAX_PROTOCOL           0
#define IRSND_SUPPORT_S100_PROTOCOL             0
#define IRSND_SUPPORT_ACP24_PROTOCOL            0
#define IRSND_SUPPORT_TECHNICS_PROTOCOL         0
#define IRSND_SUPPORT_PANASONIC_PROTOCOL        0
#define IRSND_SUPPORT_MITSU_HEAVY_PROTOCOL      0
#define IRSND_SUPPORT_IRMP16_PROTOCOL           0
#define IRSND_SUPPORT_MELINERA_PROTOCOL         0

#endif /* IRMP_HOST_CFG_H_ */
