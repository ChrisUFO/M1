/**
  ******************************************************************************
  * @file    st25r3916_aat.h
  * @author  MMY Application Team
  * @brief   ST25R3916 Antenna Tuning
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
 

#ifndef ST25R3916_AAT_H
#define ST25R3916_AAT_H

#include "rfal_platform.h"
#include "rfal_utils.h"

/*
******************************************************************************
* GLOBAL DATATYPES
******************************************************************************
*/

/*!
 * struct representing input parameters for the antenna tuning
 */
struct st25r3916AatTuneParams{
    uint8_t aat_a_min;            /*!< min value of A cap */
    uint8_t aat_a_max;            /*!< max value of A cap */
    uint8_t aat_a_start;          /*!< start value of A cap */
    uint8_t aat_a_stepWidth;      /*!< increment stepWidth for A cap */
    uint8_t aat_b_min;            /*!< min value of B cap */
    uint8_t aat_b_max;            /*!< max value of B cap */
    uint8_t aat_b_start;          /*!< start value of B cap */
    uint8_t aat_b_stepWidth;      /*!< increment stepWidth for B cap */

    uint8_t phaTarget;            /*!< target phase */
    uint8_t phaWeight;            /*!< weight of target phase */
    uint8_t ampTarget;            /*!< target amplitude */
    uint8_t ampWeight;            /*!< weight of target amplitude */

    bool doDynamicSteps;          /*!< dynamically reduce step size in algo */ 
    uint8_t measureLimit;         /*!< max number of allowed steps/measurements */
};


/*!
 * struct representing out parameters for the antenna tuning
 */
struct st25r3916AatTuneResult{

    uint8_t aat_a;                /*!< serial cap after tuning */
    uint8_t aat_b;                /*!< parallel cap after tuning */
    uint8_t pha;                  /*!< phase after tuning */
    uint8_t amp;                  /*!< amplitude after tuning */
    uint16_t measureCnt;          /*!< number of measures performed */
};



/*! 
 *****************************************************************************
 *  \brief  Perform antenna tuning
 *
 *  This function starts an antenna tuning procedure by modifying the serial 
 *  and parallel capacitors of the antenna matching circuit via the AAT_A
 *  and AAT_B registers. 
 *  This function is best run if the field is already turned on.
 *  When used on ST25R3916B with new rgs_am=1 it is necessary to turn on the
 *  field before running this procedure or to set rgs_txonoff=0.
 *   
 *  \param[in] tuningParams : Input parameters for the tuning algorithm. If NULL
 *                            default values will be used.
 *  \param[out] tuningStatus : Result information of performed tuning. If NULL
 *                             no further information is returned, only registers
 *                             ST25R3916 (AAT_A,B) will be adapted.
 *
 *  \return RFAL_ERR_IO    : Error during communication.
 *  \return RFAL_ERR_PARAM : Invalid input parameters
 *  \return RFAL_ERR_NONE  : No error.
 *
 *****************************************************************************
 */
extern ReturnCode st25r3916AatTune(const struct st25r3916AatTuneParams *tuningParams, struct st25r3916AatTuneResult *tuningStatus);

#endif /* ST25R3916_AAT_H */
