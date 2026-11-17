/*============================================================================*/
/**
  * @file    m1_sdcard_capacity.h
  * @brief   Pure capacity arithmetic for the CLI `sdcard` command.
  *
  * The capacity figures the FatFs accessor returns are KILOBYTES
  * (m1_sdcard.c: `(sectors / 1024) * sector_size`). Multiplying `used_kb` by
  * 100 in 32-bit arithmetic therefore overflows once a card is more than
  * 2^32 / 100 KB (~41 GiB) full -- e.g. a 64 GB card at 50 GB used reported a
  * single-digit percentage instead of ~78%, while the MB figures printed
  * beside it stayed correct. A 128 GB card reaches ~134,000,000 KB, so the
  * intermediate is 64-bit.
  *
  * Deliberately HAL-, FatFs- and FreeRTOS-free (same pattern as
  * m1_log_capture.h) so the host suite can execute it directly:
  * tools/m1cp_host_test/test_sdcard_usage.c.
  */
/*============================================================================*/
#ifndef M1_SDCARD_CAPACITY_H_
#define M1_SDCARD_CAPACITY_H_

#include <stdint.h>

/* Used percentage (0..100) for a mounted volume with `total_kb` capacity and
 * `free_kb` free, both in KB. Returns 0 when the total is 0 (unmounted or a
 * filesystem that reports no clusters), which also keeps the division safe.
 * Truncates, like the integer expression it replaces. */
static inline uint32_t m1_sdcard_used_percent(uint32_t total_kb, uint32_t free_kb)
{
    uint32_t used_kb;

    if (total_kb == 0U)
    {
        return 0U;
    }

    used_kb = (free_kb < total_kb) ? (total_kb - free_kb) : 0U; /* free > total is not valid; never underflow */
    return (uint32_t)(((uint64_t)used_kb * 100U) / (uint64_t)total_kb);
} // uint32_t m1_sdcard_used_percent(uint32_t total_kb, uint32_t free_kb)

#endif /* M1_SDCARD_CAPACITY_H_ */
