/**
  ******************************************************************************
  * @file    rfal_dpoTbl.h
  * @author  MMY Application Team
  * @brief   RF Dynamic Power Table default values
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
  


#ifndef ST25R3916_DPO_H
#define ST25R3916_DPO_H

/*
 ******************************************************************************
 * INCLUDES
 ******************************************************************************
 */
#include "rfal_dpo.h"


/*
 ******************************************************************************
 * GLOBAL DATA TYPES
 ******************************************************************************
 */
 
#if defined(ST25R3916)

    /*! ST25R3916 Default DPO table */
const uint8_t rfalDpoDefaultSettings [] = {
                0x00, 255, 200,
                0x01, 210, 150,
                0x02, 160, 100,
                0x03, 110, 50,
};

#elif defined(ST25R3916B)  /* ST25R3916B has an increased resolution on the driver resistance (d_res) */
    
    /*! ST25R3916B Default DPO table */
    const uint8_t rfalDpoDefaultSettings [] = {
                    0x00, 255, 200,
                    0x05, 210, 150,
                    0x09, 160, 100,
                    0x0B, 110, 50,
    };
    
#endif /* ST25R3916 */
 


#endif /* ST25R3916_DPO_H */
