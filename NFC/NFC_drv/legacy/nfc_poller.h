/* See COPYING.txt for license details. */

#include <stdbool.h>

/**
 * @brief ReadIni - Initialize NFC poller (READ-ONLY mode)
 * 
 * @retval true Initialization successful
 * @retval false Initialization failed
 */
bool ReadIni(void);

/**
 * @brief ReadCycle - Main NFC poller processing loop
 * 
 * @retval None
 */
void ReadCycle(void);