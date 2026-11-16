/* See COPYING.txt for license details. */

/*
*
* m1_fw_update_bl.c
*
* Firmware update support functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_watchdog.h"
#include "m1_fw_update.h"
#include "m1_fw_update_bl.h"
#include "m1_dfu_entry.h"
#include "m1_sub_ghz.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG		"FW-BL"

/* The host-buildable header can only carry the numeric IWDG_PR encoding; tie it
 * to the HAL's ground truth here, in a translation unit that has the register
 * definitions (IWDG_PRESCALER_256 == 0b110 on STM32H5). */
_Static_assert(M1_DFU_IWDG_PRESCALER == (uint32_t)IWDG_PRESCALER_256,
               "the DFU watchdog net must use IWDG_PRESCALER_256 (IWDG_PR == 6)");

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

FW_CFG_SECTION S_M1_FW_CONFIG_t m1_fw_config = {
	.magic_number_1 = FW_CONFIG_MAGIC_NUMBER_1,
	.fw_version_rc = FW_VERSION_RC,
	.fw_version_build = FW_VERSION_BUILD,
	.fw_version_minor = FW_VERSION_MINOR,
	.fw_version_major = FW_VERSION_MAJOR,
	.user_option_1 = 0,
	.User_option_2 = 0,
	.ism_band_region = SUBGHZ_ISM_BAND_REGION,
	.magic_number_2 = FW_CONFIG_MAGIC_NUMBER_2
};
// 4 bytes CRC32 will be manually added here with the tool srec_cat
// Example:
// 000FFC00:
// 000FFC10: magic_number_2 CRC32
// 000FFC20:

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

uint8_t bl_crc_check(uint32_t image_size);
uint32_t bl_get_crc_chunk(uint32_t *data_scr, uint32_t len, bool crc_init, bool last_chunk);
void bl_swap_banks(void);
static uint16_t bl_flash_if_init(void);
static uint16_t bl_flash_if_deinit(void);
static uint8_t bl_flash_start(uint32_t image_size);
void fw_gui_progress_update(size_t remainder);
/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint8_t bl_crc_check(uint32_t image_size)
{
    CRC_HandleTypeDef crchdl;
    uint32_t result, crc32;

    __HAL_RCC_CRC_CLK_ENABLE();
    crchdl.Instance                     = CRC;
    crchdl.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_ENABLE;
    crchdl.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_ENABLE;
    crchdl.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_NONE;
    crchdl.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
    crchdl.InputDataFormat              = CRC_INPUTDATA_FORMAT_WORDS;
    if(HAL_CRC_Init(&crchdl) != HAL_OK)
    {
        return BL_CODE_CHK_ERROR;
    }

    result = HAL_CRC_Calculate(&crchdl, (uint32_t*)(FW_START_ADDRESS + M1_FLASH_BANK_SIZE), image_size);

    HAL_CRC_DeInit(&crchdl);

    __HAL_RCC_CRC_FORCE_RESET();
    __HAL_RCC_CRC_RELEASE_RESET();

    crc32 = *(uint32_t*)(FW_CRC_ADDRESS + M1_FLASH_BANK_SIZE);

    if ( crc32==result )
    {
        return BL_CODE_OK;
    }

	 M1_LOG_E(M1_LOGDB_TAG, "crc32: 0x%lX, cal_crc32: 0x%lX, 32-bit image size: %ld\r\n", (unsigned long)crc32, (unsigned long)result, (long)image_size);

    return BL_CODE_CHK_ERROR;
} // uint8_t bl_crc_check(uint32_t image_size)



/*============================================================================*/
/**
  * @brief  Validate the RUNNING bank image against the CRC the build embedded
  *         in it (boot failsafe, issue #41-A).
  *
  * Address reasoning (checked against the linker script, the CRC emitter and
  * the update path -- this is the assumption the whole failsafe rests on):
  *   - The linker places .FW_CONFIG_SECTION at FW_CONFIG_RESERVED_ADDRESS
  *     (0x080FFC00) and the build's CRC emitter cropped 0x0-0xFFC14 and stored
  *     the CRC at 0x080FFC14 == FW_CRC_ADDRESS (build.sh CROP_END_BYTES,
  *     CMakeLists.txt "-crop 0x0 0x000FFC14", m1_fw_update_bl.h static assert).
  *   - Bank 2 is addressed by adding M1_FLASH_BANK_SIZE, and the option-byte
  *     bank swap remaps whichever physical bank is ACTIVE to 0x08000000.
  *     The bank we are executing from therefore always appears at
  *     0x08000000..0x080FFFFF, with its metadata and CRC word at
  *     0x080FFC00..0x080FFC17 -- i.e. exactly FW_CRC_ADDRESS.
  *   - bl_crc_check() adds +M1_FLASH_BANK_SIZE on purpose: it validates the
  *     OTHER bank (the freshly flashed rollback candidate) while running from
  *     this one. Using that offset here would compare the running bank's
  *     computed CRC against the inactive bank's stored CRC and fail (or, worse,
  *     accidentally pass) for the wrong reason.
  *   - The span is derived by address arithmetic only: no magic-number scan
  *     (false positive on image data) and no image-size field (untrusted).
  * @param  None
  * @retval M1_BOOT_FAULT_NONE when the image validates; otherwise the fault
  *         class (magic / header / CRC mismatch) for the boot failsafe.
  */
/*============================================================================*/
S_M1_BOOT_FAULT_t bl_crc_validate_active_bank(void)
{
    uint32_t span_bytes, result;
    uint32_t crc_init_seed = 0;
    uint32_t magic_1, magic_2, crc32;

    span_bytes = m1_boot_crc_span_bytes(FW_START_ADDRESS, FW_CRC_ADDRESS);
    if ( (span_bytes == 0U) || ((span_bytes % 4U) != 0U) )
    {
        return M1_BOOT_FAULT_UNKNOWN; // static-asserted away; fail closed if not
    }

    /* Reuse the streamed CRC path the update verifier itself uses, so the boot
     * check and the update path cannot disagree about polynomial, init value or
     * word order. A CRC peripheral init failure leaves a wrong seed and thus a
     * mismatch, which fails closed into the recovery state. */
    (void)bl_get_crc_chunk(&crc_init_seed, 0, true, false); // CRC init
    result = bl_get_crc_chunk((uint32_t *)(uintptr_t)FW_START_ADDRESS, span_bytes / FW_IMAGE_CRC_SIZE, false, true);

    /* Metadata words live inside the validated span (0x080FFC00/0x080FFC10),
     * so on a failed check they say WHY: erased/foreign metadata vs. intact
     * metadata with corrupted code. */
    magic_1 = *(const volatile uint32_t *)(uintptr_t)FW_CONFIG_RESERVED_ADDRESS;
    magic_2 = *(const volatile uint32_t *)(uintptr_t)(FW_CONFIG_RESERVED_ADDRESS + offsetof(S_M1_FW_CONFIG_t, magic_number_2));
    crc32   = *(const volatile uint32_t *)(uintptr_t)FW_CRC_ADDRESS;

    return m1_boot_classify_fault(magic_1, FW_CONFIG_MAGIC_NUMBER_1,
                                  magic_2, FW_CONFIG_MAGIC_NUMBER_2,
                                  result, crc32);
} // S_M1_BOOT_FAULT_t bl_crc_validate_active_bank(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
uint32_t bl_get_crc_chunk(uint32_t *data_scr, uint32_t len, bool crc_init, bool last_chunk)
{
    static CRC_HandleTypeDef crchdl;
    uint32_t result;

    if (crc_init)
    {
    	__HAL_RCC_CRC_CLK_ENABLE();
    	crchdl.Instance                     = CRC;
    	crchdl.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_ENABLE;
    	crchdl.Init.CRCLength				= CRC_POLYLENGTH_32B;
    	crchdl.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_ENABLE;
    	crchdl.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_NONE;//CRC_INPUTDATA_INVERSION_WORD;
    	crchdl.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;//CRC_OUTPUTDATA_INVERSION_ENABLE;
    	crchdl.InputDataFormat              = CRC_INPUTDATA_FORMAT_WORDS;
    	if(HAL_CRC_Init(&crchdl) != HAL_OK)
    	{
    		return BL_CODE_CHK_ERROR;
    	}
    	result = HAL_CRC_Calculate(&crchdl, data_scr, 0);
    } // if (crc_init)
    else
    {
        result = HAL_CRC_Accumulate(&crchdl, data_scr, len);
        if ( last_chunk )
        {
    	    HAL_CRC_DeInit(&crchdl);
    	    __HAL_RCC_CRC_FORCE_RESET();
    	    __HAL_RCC_CRC_RELEASE_RESET();
        } // if ( last_chunk )
    } // else

    return result;
} // uint32_t bl_get_crc_chunk(uint32_t *data_scr, uint32_t len, bool crc_init, bool last_chunk)



/*============================================================================*/
/**
  * @brief  Get the active bank
  * @param  None
  * @retval The active bank.
  */
/*============================================================================*/
uint16_t bl_get_active_bank(void)
{
	FLASH_OBProgramInitTypeDef OBInit;

	/* Get the boot configuration status */
    HAL_FLASHEx_OBGetConfig(&OBInit);

    /* Check Swap Flash banks  status */
    if ((OBInit.USERConfig & OB_SWAP_BANK_ENABLE)==OB_SWAP_BANK_DISABLE)
    {
    	/*Active Bank is bank 1 */
    	M1_LOG_I(M1_LOGDB_TAG, "Active Bank 1\r\n");
    	return BANK1_ACTIVE;
     }
     else
     {
    	 /*Active Bank is bank 2 */
    	 M1_LOG_I(M1_LOGDB_TAG, "Active Bank 2\r\n");
    	 return BANK2_ACTIVE;
     }
} // uint16_t bl_get_active_bank(void)



/*============================================================================*/
/**
  * @brief  Initializes Memory.
  * @param  None
  * @retval 0 if operation is successful, MAL_FAIL else.
  */
/*============================================================================*/
static uint16_t bl_flash_if_init(void)
{

	/* Disable instruction cache prior to internal cacheable memory update */
	// if (HAL_ICACHE_Disable() != HAL_OK)
	// {
	//   	Error_Handler();
	// }
	/* Unlock the internal flash */
	HAL_FLASH_Unlock();

	return 0;
} // static uint16_t bl_flash_if_init(void)



/*============================================================================*/
/**
  * @brief  De-Initializes Memory.
  * @param  None
  * @retval 0 if operation is successeful, MAL_FAIL else.
  */
/*============================================================================*/
static uint16_t bl_flash_if_deinit(void)
{
	/* Lock the internal flash */
	HAL_FLASH_Lock();

	return 0;
} // static uint16_t bl_flash_if_deinit(void)







/*============================================================================*/
/**
  * @brief  Writes Data into Memory.
  * @param  src: Pointer to the source buffer. Address to be written to.
  * @param  dest: Pointer to the destination buffer.
  * @param  Len: Number of data to be written (in bytes).
  * @retval 0 if operation is successeful, MAL_FAIL else.
  */
/*============================================================================*/
uint8_t bl_flash_if_write(uint8_t *src, uint8_t *dest, uint32_t len)
{
	uint16_t i, j;
	uint32_t *src_chk, *dst_chk;

	src_chk = (uint32_t *)src;
	dst_chk = (uint32_t *)dest;
	for (i=0; i<len; i+=16)
	{
	    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, (uint32_t)dst_chk,
	    		(uint32_t)src_chk)==HAL_OK)
	    {
	    	for (j=0; j<4; j++)
	    	{
	    		/* Check the written value */
	    		if (*src_chk != *dst_chk)
	    		{
	    			/* Flash content doesn't match SRAM content */
	    			return BL_CODE_CHK_ERROR;
	    		}
	    		src_chk++;
	    		dst_chk++;
	    	} // for (j=0; j<4; j++)
	    } // if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
	    else
	    {
	    	/* Error occurred while writing data in Flash memory */
	    	return BL_WRITE_ERROR;
	    }
	} // for (i=0; i<len; i+=16)

	return BL_CODE_OK;
} // uint8_t bl_flash_if_write(uint8_t * src, uint8_t * dest, uint32_t Len)












/*============================================================================*/
/**
  * @brief  Swap the banks
  * @param  None
  * @retval None
  */
/*============================================================================*/
void bl_swap_banks(void)
{
	FLASH_OBProgramInitTypeDef OBInit;

	/* Unlock the User Flash area */
	HAL_FLASH_Unlock();

	HAL_FLASH_OB_Unlock();
    /* Get the boot configuration status */
    HAL_FLASHEx_OBGetConfig(&OBInit);

    /* Check Swap Flash banks status */
    if ( (OBInit.USERConfig & OB_SWAP_BANK_ENABLE)==OB_SWAP_BANK_DISABLE )
    {
    	/*Swap to bank2 */
    	M1_LOG_I(M1_LOGDB_TAG, "Swap to bank2\n\r");
    	/*Set OB SWAP_BANK_OPT to swap Bank2*/
    	OBInit.USERConfig = OB_SWAP_BANK_ENABLE;
     } // if ( (OBInit.USERConfig & OB_SWAP_BANK_ENABLE)==OB_SWAP_BANK_DISABLE )
     else
     {
    	 /* Swap to bank1 */
    	 M1_LOG_I(M1_LOGDB_TAG, "Swap to bank1\n\r");
    	 /*Set OB SWAP_BANK_OPT to swap Bank1*/
    	 OBInit.USERConfig = OB_SWAP_BANK_DISABLE;
     } // else

	 OBInit.OptionType = OPTIONBYTE_USER;
	 OBInit.USERType = OB_USER_SWAP_BANK;
	 HAL_FLASHEx_OBProgram(&OBInit);

	 /* Launch Option bytes loading */
	 HAL_FLASH_OB_Launch();

	 vTaskDelay(pdMS_TO_TICKS(200));
	 /* Reset the MCU */
	 HAL_NVIC_SystemReset();
} // void bl_swap_banks(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
static uint8_t bl_flash_start(uint32_t image_size)
{
	//uint32_t startsector = 0;
	uint32_t sectornb = 0;
	/* Variable contains Flash operation status */
	HAL_StatusTypeDef status;
	FLASH_EraseInitTypeDef EraseInitStruct;
	uint16_t n_sectors;
	uint8_t i;

	/*
	 An STM32H5 crashing when accessing the FLASH_BANK_SIZE register is typically caused by
	 accessing unwritten memory, triggering an Error Correction Code (ECC) fault.
	 Accessing this unwritten memory causes a Double Error Detection (DED), which raises a Non-Maskable Interrupt (NMI).
	 Solution:
	 Read the Flash ECC Data Register (FLASH_ECCDR): Inside the NMI handler, check the FLASH_ECCDR to identify the nature of the ECC error.
	 uint32_t eccdr_value = FLASH->ECCDR; // Read ECCDR to clear the NMI NMI_Handler
	 */
	if ( image_size > M1_FLASH_BANK_SIZE/*FLASH_BANK_SIZE*/ )
		return BL_CODE_SIZE_ERROR;

	n_sectors = (image_size + (FLASH_SECTOR_SIZE - 1))/FLASH_SECTOR_SIZE;
	if (!n_sectors)
		return BL_CODE_SIZE_ERROR;

	EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;

	// we want to erase the other bank than the active one
	if (bl_get_active_bank()==BANK1_ACTIVE)
	{
		M1_LOG_I(M1_LOGDB_TAG, "Erase sectors in Bank 2\r\n");
		EraseInitStruct.Banks = FLASH_BANK_2;
	}
	else
	{
		M1_LOG_I(M1_LOGDB_TAG, "Erase sectors in Bank 1\r\n");
		EraseInitStruct.Banks = FLASH_BANK_1;
	}

	EraseInitStruct.NbSectors = 1;
	for (i=0; i<n_sectors; i++)
	{
		m1_wdt_reset();
		EraseInitStruct.Sector = i;
		status = HAL_FLASHEx_Erase(&EraseInitStruct, &sectornb);
		if (status != HAL_OK)
			break;
	} // for (i=0; i<n_sectors; i++)

	if (status != HAL_OK)
		return BL_CODE_ERASE_ERROR;

	return BL_CODE_OK;
} // static uint8_t bl_flash_start(uint32_t image_size)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
/* File-scope (was: function-local `static`) so bl_flash_stream_reset() can
 * force them back to their pre-BEGIN state -- see that function's own doc
 * comment in m1_fw_update_bl.h for why that matters. */
static uint32_t s_bl_write_acc;
static uint8_t *s_bl_flash_add;
static bool     s_bl_init_done = false;

void bl_flash_stream_reset(void)
{
    s_bl_init_done = false;
    s_bl_write_acc = 0;
    s_bl_flash_add = NULL;
}

static uint8_t bl_flash_binary(uint8_t *payload, size_t size)
{
    uint8_t err;

    if ( !s_bl_init_done )
    {
        M1_LOG_I(M1_LOGDB_TAG, "Start flashing...\r\n");
        s_bl_flash_add = (uint8_t *)FW_START_ADDRESS;
        s_bl_flash_add += M1_FLASH_BANK_SIZE; // It should always write to Bank 2 destination
        s_bl_write_acc = 0;
        s_bl_init_done = true;
    } // if ( !s_bl_init_done )

    if ( size )
    {
        err = bl_flash_if_write(payload, s_bl_flash_add, size);
        if (err != BL_CODE_OK)
        {
            M1_LOG_I(M1_LOGDB_TAG, "Writing flash error at 0x%lX.\r\n", (unsigned long)(uintptr_t)s_bl_flash_add);
            return err;
        }
        s_bl_write_acc += size;
        s_bl_flash_add += size;
        return BL_CODE_OK;
    } // if ( size )

    M1_LOG_I(M1_LOGDB_TAG, "\r\nFlashing completed!\r\n");
    s_bl_init_done = false; // reset

    /* An image smaller than its own trailing CRC can't be valid -- reject it
     * here rather than letting the subtraction below underflow s_bl_write_acc
     * (uint32_t) into a huge word count, which bl_crc_check() would then hand
     * straight to HAL_CRC_Calculate() as a read length far past the flash
     * bank (memory fault / garbage compared against whatever 4 bytes happen
     * to sit at FW_CRC_ADDRESS). Streamed callers can reach this with a tiny
     * client-declared image_size (the M1CP coordinator only rejects
     * image_size == 0, not 1..FW_IMAGE_CRC_SIZE-1). */
    if ( s_bl_write_acc < FW_IMAGE_CRC_SIZE )
    {
        M1_LOG_I(M1_LOGDB_TAG, "Image too small to contain its own CRC (%lu bytes).\r\n",
                 (unsigned long)s_bl_write_acc);
        return BL_CODE_SIZE_ERROR;
    }

    s_bl_write_acc -= FW_IMAGE_CRC_SIZE; // exclude the CRC at the end of the image file
    s_bl_write_acc /= 4; // convert image size from byte to word (32-bit)
    err = bl_crc_check(s_bl_write_acc);

	if (err != BL_CODE_OK)
    {
        M1_LOG_I(M1_LOGDB_TAG, "CRC not matched.\r\n");
        return err;
    }
    M1_LOG_I(M1_LOGDB_TAG, "Flash verified.\r\n");

    return BL_CODE_OK;
} // static uint8_t bl_flash_binary(uint8_t *payload, size_t size)




/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
uint8_t bl_flash_app(FIL *hfile)
{
    uint8_t fw_payload[FW_IMAGE_CHUNK_SIZE];
	uint8_t flash_err;
	uint16_t count;
	size_t write_size;

	f_lseek(hfile, 0); // Move file pointer to the beginning of the file
	write_size = f_size(hfile); // Get image size
    M1_LOG_I(M1_LOGDB_TAG, "Erasing flash...\r\n");
	// Display glass hour here

    bl_flash_if_init();
    flash_err = bl_flash_start(write_size);
    // Clear glass hour
    if ( flash_err != BL_CODE_OK )
    {
    	M1_LOG_I(M1_LOGDB_TAG, "Failed\r\n");
    	write_size = 0; // Set end condition
    }

    while ( write_size )
	{
        m1_wdt_reset();

        flash_err = BL_CODE_CHK_ERROR;
		count = m1_fb_read_from_file(hfile, (char *)fw_payload, FW_IMAGE_CHUNK_SIZE);
		if ( !count || (count % 4 != 0) ) // Read failed?
			break;

		fw_gui_progress_update(write_size);

		write_size -= count;
		if ( count )
		{
			flash_err = bl_flash_binary(fw_payload, count);
			if ( flash_err != BL_CODE_OK )
				break;
		} // if ( count )

		if (!write_size)
		{
			// Last step to verify CRC
			flash_err = bl_flash_binary(NULL, 0);
		} // if (!write_size)
	} // while ( write_size )

    bl_flash_if_deinit();

	if ( write_size || (flash_err != BL_CODE_OK) )
	{
		; // Display error here
	}

	return flash_err;
} // uint8_t bl_flash_app(FIL *hfile)



/******************************************************************************/
/**
  * @brief  Streamed programming wrappers for the M1CP firmware-update path.
  *         These reuse the exact SD-path engine (same calls, same order) but are
  *         driven by streamed chunks instead of a FatFs file, so no on-device SD
  *         staging is needed. Engine logic is unchanged.
  * @retval BL_CODE_OK on success, else an S_M1_BL_CODES_t error.
  */
/******************************************************************************/
uint8_t bl_flash_stream_begin(uint32_t image_size)
{
	/* Same intake contract as the SD path (m1_fw_update.c): only the canonical
	 * image (signed span + trailing CRC word) can ever pass the final
	 * bl_crc_check(), which compares against the CRC word at the FIXED
	 * FW_CRC_ADDRESS in the target bank. Reject anything else before the erase,
	 * with the size-error code, instead of erasing a 1 MiB bank to program an
	 * image that cannot validate and would leave the bank un-bootable-valid. */
	if ( !m1_boot_image_size_is_canonical(image_size) )
	{
		M1_LOG_E(M1_LOGDB_TAG, "Rejected streamed image size %lu: must be exactly %lu bytes (0xFFC14 signed span + 4-byte CRC).\r\n",
				(unsigned long)image_size, (unsigned long)M1_BOOT_IMAGE_FILE_SIZE_CANONICAL);
		return BL_CODE_SIZE_ERROR;
	}

	/* Force the write cursor back to its pre-BEGIN state FIRST, in case the
	 * previous attempt (if any) was aborted or failed before its own
	 * bl_flash_binary(NULL,0) finalize call ran -- otherwise this BEGIN's
	 * fresh erase would be written into starting from that stale,
	 * mid-previous-image cursor instead of the start of the bank. See
	 * bl_flash_stream_reset()'s own doc comment (m1_fw_update_bl.h). */
	bl_flash_stream_reset();
	bl_flash_if_init();
	return bl_flash_start(image_size);   // erase the inactive bank
} // uint8_t bl_flash_stream_begin(uint32_t image_size)

uint8_t bl_flash_stream_write(uint8_t *payload, uint32_t size)
{
	return bl_flash_binary(payload, (size_t)size);   // program one chunk
} // uint8_t bl_flash_stream_write(uint8_t *payload, uint32_t size)

uint8_t bl_flash_stream_finish(void)
{
	uint8_t err = bl_flash_binary(NULL, 0);   // finalize + bl_crc_check
	bl_flash_if_deinit();
	return err;
} // uint8_t bl_flash_stream_finish(void)

void bl_flash_stream_abort(void)
{
	// Release/reset for every exit path that ISN'T a successful
	// bl_flash_stream_finish() (which already re-locks flash itself, above).
	// bl_flash_if_init/deinit are file-local (HAL_FLASH_Unlock/Lock); this is
	// the one exported way a caller outside this file can make sure flash
	// ends up locked again after a begin() that was aborted or that never
	// reached finish(). Safe to call even when nothing was ever unlocked
	// this attempt: bl_flash_stream_reset() is a pure state reset and
	// HAL_FLASH_Lock() is idempotent on already-locked flash.
	bl_flash_stream_reset();
	bl_flash_if_deinit();
} // void bl_flash_stream_abort(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void fw_gui_progress_update(size_t remainder)
{
	char percent_txt[30];
	uint8_t percent;
	size_t percent_cnt;
	static uint32_t image_size = 0;
	static uint8_t progress_percent_count = 0;
	static uint8_t progress_slider_x_post;

	if ( image_size < remainder )
	{
		image_size = remainder; // init
		progress_percent_count = 0;
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
		// Draw solid box to clear existing content
		u8g2_DrawBox(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1 - M1_SUB_MENU_FONT_HEIGHT + 1, 120, M1_SUB_MENU_FONT_HEIGHT + 1);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
		u8g2_DrawXBMP(&m1_u8g2, FW_UPDATE_PROGRESS_SLIDE_STRIP_COL, FW_UPDATE_PROGRESS_SLIDE_STRIP_ROW,
					126, 14, fw_update_slide_strip_126x14); // Progress slide strip
		u8g2_DrawStr(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1, "Updating: 00%");
		progress_slider_x_post = 3;
		M1_LOG_N(M1_LOGDB_TAG, "\r\n");
	}

	percent_cnt = image_size - remainder;
	percent_cnt /= (image_size/FW_UPDATE_FULL_PROGRESS_COUNT);
	if ( percent_cnt )
	{
		percent_cnt -= progress_percent_count;
		if ( percent_cnt )
		{
			progress_percent_count++;
			percent = FW_UPDATE_FULL_PROGRESS_FACTOR*progress_percent_count;
			sprintf(percent_txt, "Updating: %02d%%", percent);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG); // set to background color
			// Draw solid box to clear existing content
			u8g2_DrawBox(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1 - M1_SUB_MENU_FONT_HEIGHT + 1, 120, M1_SUB_MENU_FONT_HEIGHT + 1);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT); // return to text color
			u8g2_DrawStr(&m1_u8g2, 4, INFO_BOX_Y_POS_ROW_1, percent_txt); // Write new content
			u8g2_DrawXBMP(&m1_u8g2, progress_slider_x_post, FW_UPDATE_PROGRESS_SLIDE_STRIP_ROW + 3,
						4, 8, fw_update_slider_5x8); // Progress slider
			progress_slider_x_post += FW_UPDATE_SLIDER_WIDTH - FW_UPDATE_SLIDER_OVERLAP;
			if ( percent==100 ) // complete 100%?
				image_size = 0; // Reset
		    M1_LOG_I(M1_LOGDB_TAG, "\rProgress: %d%%", percent);
		} // if ( percent_cnt )
 	} // if ( percent_cnt )

	m1_u8g2_nextpage(); // Update display RAM
} // void fw_gui_progress_update(size_t remainder)



#ifndef M1CP_HOST_TEST

/*============================================================================*/
/* Loud, recoverable failure exit for the teardown.
 *
 * When a teardown step cannot be completed (the clock tree did not settle, or
 * the IWDG prescaler/reload synchronisation never became visible) the unit must
 * NOT continue into the ROM: it would either run on a clock tree the ROM cannot
 * service or, worse, carry a watchdog net reloaded with the previous PR/RLR
 * values -- a silently disarmed net. Requesting an immediate reset is the only
 * bounded, deterministic and observable option here (no logging is possible
 * with interrupts disabled on a boot path): the reset is recoverable (the unit
 * reboots, the strap/request is honoured again), it leaves a reset-cause flag,
 * and it cannot hang.
 *
 * The trailing spin is never reached in practice -- SYSRESETREQ does not return
 * -- and exists only so the function is honest if a debugger holds the reset
 * request. It is the one loop in this translation unit without an iteration
 * bound, and the host seam allows it for exactly that reason. */
static void bl_dfu_teardown_failed(void) __attribute__((noreturn));
static void bl_dfu_teardown_failed(void)
{
	NVIC_SystemReset();
	for (;;) { __NOP(); } // not reached
} // static void bl_dfu_teardown_failed(void)

/*============================================================================*/
/* Bounded RCC teardown for the ROM handover.
 *
 * This replaces HAL_RCC_DeInit(). The HAL version waits on HAL_GetTick(), and
 * this project's HAL tick comes from TIM6, whose interrupt is already masked
 * when the teardown runs (__disable_irq() at step 2). uwTick therefore stops
 * advancing, every "if ((HAL_GetTick() - tickstart) > RCC_*_TIMEOUT_VALUE)"
 * inside the HAL call is permanently false, and a stuck ready flag -- HSI never
 * coming up, a PLL never stopping, a clock switch never completing -- spins
 * forever with interrupts off and, on the cold-boot strap route, before the
 * IWDG net is armed: a hang only removing power clears.
 *
 * So the same teardown is done here from raw registers, with every flag wait
 * capped at M1_DFU_TEARDOWN_SPIN_LIMIT and falling through, matching the
 * discipline the rest of bl_jump_to_dfu() already follows.
 *
 * Ending state, which is what the ST ROM expects on entry: it is exactly the
 * state a hardware reset leaves behind, so the bootloader's own clock init
 * finds nothing to undo. SYSCLK = HSI (64 MHz, HSIDIV /1), AHB/APB prescalers
 * /1, HSE / CSI / HSI48 and PLL1/2/3 off, the PLL config registers at their
 * reset values, RCC interrupts disabled and their flags cleared. The ROM
 * brings up whatever it needs itself (HSI48 for the USB clock is its own
 * enable, so switching it off here is the reset state, not a loss). LSI is
 * deliberately left running: it clocks the IWDG net armed at step 9, and
 * stopping it would kill the very net that makes an abandoned session
 * recoverable.
 *
 * A flag that never settles is not fatal *inside the waits*: each wait falls
 * through after M1_DFU_TEARDOWN_SPIN_LIMIT, exactly like the cache and IWDG->SR
 * waits. What must not happen is jumping with the tree half torn down, so the
 * caller verifies the post-condition below and resets if it does not hold --
 * see bl_dfu_teardown_failed(). */

/* Bounded poll of one RCC register field. `want` is the masked value meaning
 * "settled"; the loop gives up after M1_DFU_TEARDOWN_SPIN_LIMIT polls. */
static void bl_rcc_wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
	uint32_t i;

	for (i = 0U; (READ_BIT(*reg, mask) != want) && (i < M1_DFU_TEARDOWN_SPIN_LIMIT); i++)
	{
		;
	}
} // void bl_rcc_wait_flag(...)

/* Ends in the clock state a hardware reset leaves; returns false when the
 * essential post-condition does not hold -- SYSCLK is not running from HSI, or
 * PLL1 (the PLL the application used as SYSCLK) is still ready. The caller must
 * not jump in that case: the ROM would inherit a clock tree it cannot service. */
static bool bl_rcc_teardown_bounded(void)
{
	/* HSI on and ready BEFORE anything is switched off: the CPU is running
	 * from PLL1 here, so stopping the active SYSCLK source first would stop
	 * the core mid-sequence. */
	SET_BIT(RCC->CR, RCC_CR_HSION);
	bl_rcc_wait_flag(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);

	/* HSIDIV /1 and the reset trimming: 64 MHz HSI. */
	CLEAR_BIT(RCC->CR, RCC_CR_HSIDIV);
	WRITE_REG(RCC->HSICFGR, RCC_HSICFGR_HSITRIM_6);

	/* CFGR1/CFGR2 reset == SYSCLK switched to HSI, every prescaler /1, no
	 * kernel clock mux or MCO still pointing at a PLL. */
	CLEAR_REG(RCC->CFGR1);
	CLEAR_REG(RCC->CFGR2);
	bl_rcc_wait_flag(&RCC->CFGR1, RCC_CFGR1_SWS, 0U);

	/* The oscillators the application turned on. LSI is not in this list on
	 * purpose -- see the block comment above. */
	CLEAR_BIT(RCC->CR, RCC_CR_CSION | RCC_CR_CSIKERON | RCC_CR_HSECSSON |
	                   RCC_CR_HSIKERON | RCC_CR_HSI48ON | RCC_CR_HSEON);
	CLEAR_BIT(RCC->CR, RCC_CR_HSEEXT | RCC_CR_HSEBYP);

	/* PLLs off, each with its own capped wait on the matching ready flag. */
	CLEAR_BIT(RCC->CR, RCC_CR_PLL1ON);
	bl_rcc_wait_flag(&RCC->CR, RCC_CR_PLL1RDY, 0U);
	CLEAR_BIT(RCC->CR, RCC_CR_PLL2ON);
	bl_rcc_wait_flag(&RCC->CR, RCC_CR_PLL2RDY, 0U);
#if defined(RCC_CR_PLL3ON)
	CLEAR_BIT(RCC->CR, RCC_CR_PLL3ON);
	bl_rcc_wait_flag(&RCC->CR, RCC_CR_PLL3RDY, 0U);
	CLEAR_REG(RCC->PLL3CFGR);
	WRITE_REG(RCC->PLL3DIVR, 0x01010280U);
	CLEAR_REG(RCC->PLL3FRACR);
#endif /* RCC_CR_PLL3ON */

	/* PLL dividers / fractional registers back to their reset values. */
	CLEAR_REG(RCC->PLL1CFGR);
	WRITE_REG(RCC->PLL1DIVR, 0x01010280U);
	CLEAR_REG(RCC->PLL1FRACR);
	CLEAR_REG(RCC->PLL2CFGR);
	WRITE_REG(RCC->PLL2DIVR, 0x01010280U);
	CLEAR_REG(RCC->PLL2FRACR);

	/* No RCC interrupt may reach the ROM. */
	CLEAR_REG(RCC->CIER);
	WRITE_REG(RCC->CICR, 0xFFFFFFFFU);
	SET_BIT(RCC->RSR, RCC_RSR_RMVF);

	/* The HAL keeps the SYSCLK frequency in this variable: nothing after this
	 * point may still believe it is running at the application's 150 MHz. */
	SystemCoreClock = HSI_VALUE;

	/* Verified post-condition, not a discarded return value: SYSCLK must now be
	 * HSI (SWS == 0, which also implies HSIRDY) and PLL1 -- the source the
	 * application ran from -- must be off (PLL1RDY == 0). A stuck flag that the
	 * capped waits above gave up on shows up here instead of being ignored. */
	return (READ_BIT(RCC->CFGR1, RCC_CFGR1_SWS) == 0U) &&
	       (READ_BIT(RCC->CR, RCC_CR_PLL1RDY) == 0U);
} // bool bl_rcc_teardown_bounded(void)

/*============================================================================*/
/* PSP -> MSP handover into the ST ROM, and the jump.
 *
 * FreeRTOS runs every task on PSP: the CM33 port sets CONTROL.SPSEL = 1 in
 * thread mode and fences each task with PSPLIM (see the port's portasm.c).
 * __set_MSP() is a plain MSR MSP, so from a task it only writes a register the
 * executing context does not use -- the ROM reset vector would then run on the
 * abandoned task stack instead of the ROM initial SP that step 1 just
 * sanity-checked, and the first stack growth past the stale PSPLIM faults with
 * no handler in place. Only the pre-scheduler strap route (main(), before
 * osKernelStart) is already on MSP, which is why the legacy fork's plain
 * __set_MSP() was never wrong: it never jumped from a task.
 *
 * Clearing CONTROL.SPSEL makes MSP the active stack INSTANTLY, so from that
 * write onwards every stack access -- a spilled local, a pushed return frame --
 * would touch a frame that no longer belongs to this context: undefined
 * behaviour, and on the way into a bootloader, a corrupt handover. The switch,
 * the MSP load and the branch therefore have to be one register-only sequence,
 * which is why this is a naked tail:
 *
 *   - r0/r1 hold sp_val and the reset vector from function entry (AAPCS), and
 *     nothing here pushes, pops or spills before or after the CONTROL write;
 *   - MSP is loaded BEFORE SPSEL is cleared, so the switch lands directly on
 *     the ROM's own stack rather than on a half-built one (the opposite order
 *     would leave SP on the application's MSP for the width of the write);
 *   - ISB orders the CONTROL write against what follows (the write is
 *     self-synchronising for the next instruction on CM33, but the next
 *     instruction here is a branch, and the barrier removes any doubt);
 *   - PRIMASK is cleared here rather than in the C body: the ROM reset handler
 *     expects the hardware-reset state (interrupts enabled) and enumerates over
 *     USB on interrupts, and doing it here means no interrupt can ever be taken
 *     with SPSEL still 1 and MSP still the application's. Step 6 has disabled
 *     and un-pended every NVIC line and step 3 removed the pended core
 *     exceptions, so there is no enabled source left that could take one in the
 *     single-instruction window before the branch.
 *
 * The shipped ELF was disassembled to confirm there is no load/store between
 * the CONTROL write and the bx; tools/m1cp_host_test pins the seam in source. */
static void bl_rom_handover(uint32_t sp_val, uint32_t reset_vector) __attribute__((naked, noreturn));
static void bl_rom_handover(uint32_t sp_val, uint32_t reset_vector)
{
	(void)sp_val;       /* lives in r0 from here to the bx (see above) */
	(void)reset_vector; /* lives in r1 */

	__asm volatile (
		"msr msp, r0        \n\t" /* ROM initial SP, not yet the active stack */
		"mrs r2, control    \n\t"
		"bic r2, r2, #2     \n\t" /* CONTROL.SPSEL = 0: thread mode uses MSP */
		"msr control, r2    \n\t"
		"isb                \n\t"
		"cpsie i            \n\t" /* the ROM expects the reset state */
		"bx  r1             \n\t" /* ROM reset vector, system memory */
	);
} // static void bl_rom_handover(...)

/*============================================================================*/
/**
  * @brief  Robust, NON-RETURNING jump to the STM32H573 system-memory (ROM) DFU
  *         bootloader (issue #41-B; ported from the fork's bl_jump_to_dfu()).
  *
  * Teardown order, and why each step is where it is:
  *
  *   1. SP sanity check. A blank/foreign system-memory region resets the MCU
  *      instead of branching into garbage. No logging is possible here.
  *   2. __disable_irq(). Nothing may preempt the teardown, and the ROM
  *      bootloader must not receive an application ISR.
  *   3. SysTick teardown (CTRL/LOAD/VAL = 0) plus clearing the pended
  *      SysTick/PendSV bits, so no core exception survives into the ROM's
  *      vector table when interrupts are re-enabled for it later.
  *   4. Application clock tree down to the reset state
  *      (bl_rcc_teardown_bounded()): HSI on and selected, prescalers /1,
  *      HSE/CSI/PLLs off, LSI untouched. The legacy fork did not do this and
  *      relied on the ROM reconfiguring the clock itself; the issue #41
  *      acceptance criterion requires the application clock tree to be dropped
  *      before the jump, and it removes any chance of the ROM inheriting a PLL
  *      it cannot service. It runs AFTER interrupts are off (a tick during
  *      clock teardown would be fatal) and BEFORE the NVIC/VTOR work.
  *      It is a raw-register teardown with capped waits, NOT HAL_RCC_DeInit():
  *      the HAL call polls HAL_GetTick(), and this project's only tick source
  *      (TIM6) is masked by step 2, so its timeouts never advance -- a stuck
  *      RCC flag would then spin forever before the watchdog net exists. The
  *      helper documents what the ROM needs and why every wait is capped.
  *   5. TIM6 (the HAL tick timebase) stopped and its clock gate dropped: with
  *      interrupts off nothing vectors its update, and the ROM never asked for
  *      it. It must come after step 4, which has just reprogrammed the clock
  *      tree that feeds it.
  *   6. NVIC->ICER/ICPR cleared: every interrupt disabled and un-pended.
  *   7. SCB->VTOR = system memory, so any vector taken after this point
  *      resolves in the ROM's vector table.
  *   8. DSB/ISB, then ICACHE/DCACHE disabled with busy-wait. On H5, entering
  *      the BootROM with caches enabled can fail or hang silently.
  *   9. 32 s IWDG safety net: START first, then UNLOCK/PR/RLR, wait for the
  *      register update, feed. 0xCCCC starts a stopped IWDG and is a no-op on a
  *      running one, and the ROM never feeds the counter, so the alternative is
  *      the route's previous period resetting the unit mid-enumeration (~4 s on
  *      the application route). Starting before programming matters on the
  *      cold-boot strap route, which runs before MX_IWDG_Init(): a stopped IWDG
  *      has its LSI off, so PR/RLR update flags need LSI cycles to appear --
  *      see stm32h5xx_hal_iwdg.c. The update wait is LSI-domain (one prescaled
  *      period), so it has its own millisecond-class budget
  *      (M1_DFU_IWDG_SYNC_SPIN_LIMIT); if it still expires the teardown resets
  *      rather than feeding 0xAAAA, which would reload with the previous
  *      PR/RLR and silently disarm the net.
  *  10. Thread-mode stack back to MSP with the ROM's own initial SP, interrupts
  *      re-enabled, jump to the ROM reset vector. That last step is
  *      register-only on purpose: clearing CONTROL.SPSEL changes the stack the
  *      executing context uses, so any C running after it would be reading and
  *      writing a frame that no longer belongs to this context. See
  *      bl_rom_handover().
  *
  * Every hardware-flag spin reachable from this function is bounded: the RCC
  * waits inside bl_rcc_teardown_bounded() and the two cache waits by
  * M1_DFU_TEARDOWN_SPIN_LIMIT, the IWDG->SR update wait by its own
  * M1_DFU_IWDG_SYNC_SPIN_LIMIT. From step 2 to step 9 the unit has interrupts
  * disabled and (on the strap route) no watchdog net, so an unbounded wait on a
  * stuck flag would be a hang only a power cycle clears. The cache and RCC waits
  * fall through to the jump (best effort); the two waits whose failure would
  * leave the unit in a state worse than a reset (the clock tree, the watchdog
  * net) fail loudly through bl_dfu_teardown_failed() instead.
  *
  *
  * Deliberately NOT ported: the legacy boot-time IWDG option-byte rewrite
  * (HAL_FLASHEx_OBProgram + HAL_FLASH_OB_Launch). It was a verified defect --
  * a normal boot must never program option bytes -- and this function programs
  * none: it only arms the already-running watchdog counter. See
  * startup_iwdg_option_byte_check() in m1_system.c.
  *
  * Defined only where CMSIS register definitions exist; host test builds never
  * compile this translation unit (see the header prototype's note).
  */
/*============================================================================*/
void bl_jump_to_dfu(void)
{
	uint32_t sp_val;
	uint32_t jump_address;
	uint32_t i;

	/* 1. Boot-vector sanity: initial SP must look like H5 SRAM. */
	sp_val = *(volatile const uint32_t *)(uintptr_t)M1_SYSTEM_MEMORY_BASE;
	if ( !m1_dfu_sp_plausible(sp_val) )
	{
		bl_dfu_teardown_failed(); // nothing sane to jump to: loud reset
	}

	/* 2. No interrupts from here on until the ROM is ready for them. */
	__disable_irq();

	/* 3. SysTick off, and drop the core exceptions it may already have pended.
	 *    NVIC->ICER/ICPR below cover peripheral lines only, so a SysTick or
	 *    PendSV pended in this window (or by the RTOS just before step 2) would
	 *    otherwise fire into the ROM's vector table the moment the ROM runs. */
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL  = 0U;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	__ISB();

	/* 4. Application clock tree down: back to HSI, PLLs off (bounded, and
	 *    tick-free -- see bl_rcc_teardown_bounded()). Its post-condition is
	 *    checked, not discarded: a tree that did not settle means the ROM would
	 *    inherit a clock it cannot service, so fail loudly instead of jumping. */
	if ( !bl_rcc_teardown_bounded() )
	{
		bl_dfu_teardown_failed();
	}

	/* 5. TIM6 is this project's HAL tick timebase: stop the counter, drop its
	 *    pending update flag and release its clock gate. Its NVIC line is
	 *    masked at step 6 so nothing vectors it, but the ROM must not inherit a
	 *    running peripheral it never asked for. Order matters: the timer has to
	 *    be stopped while its clock is still gated on. */
	TIM6->CR1 &= ~TIM_CR1_CEN;
	TIM6->SR  = 0U;
	CLEAR_BIT(RCC->APB1LENR, RCC_APB1LENR_TIM6EN);

	/* 6. Disable and un-pend every NVIC interrupt. */
	for (i = 0U; i < (uint32_t)(sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); i++)
	{
		NVIC->ICER[i] = 0xFFFFFFFFU;
		NVIC->ICPR[i] = 0xFFFFFFFFU;
	}

	/* 7. Vectors now resolve in system memory. */
	SCB->VTOR = M1_SYSTEM_MEMORY_BASE;

	/* 8. Barriers, then caches off (BootROM entry is unsafe with caches on). */
	jump_address = *(volatile const uint32_t *)(uintptr_t)(M1_SYSTEM_MEMORY_BASE + 4U);

	__DSB();
	__ISB();

	if ( (ICACHE->CR & ICACHE_CR_EN) != 0U )
	{
		ICACHE->CR &= ~ICACHE_CR_EN;
		/* Bounded (see M1_DFU_TEARDOWN_SPIN_LIMIT): a stuck BUSYF must not
		 * hang the unit with interrupts off and no net armed. */
		for (i = 0U; ((ICACHE->SR & ICACHE_SR_BUSYF) != 0U) && (i < M1_DFU_TEARDOWN_SPIN_LIMIT); i++)
		{
			;
		}
	}
	if ( (DCACHE1->CR & DCACHE_CR_EN) != 0U )
	{
		DCACHE1->CR &= ~DCACHE_CR_EN;
		for (i = 0U; ((DCACHE1->SR & DCACHE_SR_BUSYF) != 0U) && (i < M1_DFU_TEARDOWN_SPIN_LIMIT); i++)
		{
			;
		}
	}

	/* 9. 32 s watchdog safety net (LSI / 256 via IWDG_PR == 6, max reload).
	 *    START FIRST, then configure -- the HAL's own order, and the only safe
	 *    one on the cold-boot strap route where the IWDG/LSI is still stopped
	 *    and PR/RLR write flags need LSI cycles to appear. The wait for the
	 *    register update is the one spin in this function that waits on the LSI
	 *    domain (one prescaled LSI period = 8 ms at PR == 6), so it uses its own
	 *    budget (M1_DFU_IWDG_SYNC_SPIN_LIMIT, ~4x ST's HAL_IWDG_DEFAULT_TIMEOUT)
	 *    rather than the tens-of-cycles cache cap. */
	IWDG->KR  = M1_DFU_IWDG_KEY_START;
	IWDG->KR  = M1_DFU_IWDG_KEY_UNLOCK;
	IWDG->PR  = M1_DFU_IWDG_PRESCALER;
	IWDG->RLR = M1_DFU_IWDG_RELOAD;
	for (i = 0U; (IWDG->SR != 0U) && (i < M1_DFU_IWDG_SYNC_SPIN_LIMIT); i++)
	{
		;
	}
	if ( IWDG->SR != 0U )
	{
		/* The PR/RLR writes never became visible. Feeding 0xAAAA now would
		 * reload with the PREVIOUS PR/RLR -- ~0.5 s on the strap route, where
		 * the counter was just started at its reset defaults -- i.e. it would
		 * silently disarm the 32 s net in exactly the corner case the net
		 * exists for. Fail loudly instead (reset, no feed). */
		bl_dfu_teardown_failed();
	}
	IWDG->KR = M1_DFU_IWDG_KEY_RELOAD;

	/* 10. Take the ROM's stack pointer, switch the thread-mode stack back to
	 *     MSP, re-enable interrupts (the ROM enumerates over USB on them) and
	 *     branch to its reset vector. The three must be one register-only
	 *     sequence -- see bl_rom_handover() for the PSP->MSP trap. It does not
	 *     return, so there is deliberately no fallback loop after it. */
	bl_rom_handover(sp_val, jump_address);
} // void bl_jump_to_dfu(void)
#endif /* M1CP_HOST_TEST */
