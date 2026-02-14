/* See COPYING.txt for license details. */

/*
*
* m1_core_config.c
*
* Core functions
*
* M1 Project
*
*/

#include "main.h"
#include "m1_core_config.h"

#ifndef SYS_IS_CALLED_FROM_ISR
#define SYS_IS_CALLED_FROM_ISR() ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0 ? 1 : 0)
#endif /* SYS_IS_CALLED_FROM_ISR */



/*============================================================================*/
/**
 * @brief
 * @param  None
 * @retval None
 */
/*============================================================================*/
void *malloc_critical(size_t size)
{
  void *p;
  if (SYS_IS_CALLED_FROM_ISR())
  {
    uint32_t priority;
    priority = taskENTER_CRITICAL_FROM_ISR();
    p = malloc(size);
    taskEXIT_CRITICAL_FROM_ISR(priority);
  }
  else
  {
    taskENTER_CRITICAL();
    p = malloc(size);
    taskEXIT_CRITICAL();
  }
  return p;
}


/*============================================================================*/
/**
 * @brief
 * @param  None
 * @retval None
 */
/*============================================================================*/
void *calloc_critical(size_t num, size_t size)
{
  void *p;
  if (SYS_IS_CALLED_FROM_ISR())
  {
    uint32_t priority;
    priority = taskENTER_CRITICAL_FROM_ISR();
    p = calloc(num, size);
    taskEXIT_CRITICAL_FROM_ISR(priority);
  }
  else
  {
    taskENTER_CRITICAL();
    p = calloc(num, size);
    taskEXIT_CRITICAL();
  }
  return p;
}


/*============================================================================*/
/**
 * @brief
 * @param  None
 * @retval None
 */
/*============================================================================*/
void free_critical(void *mem)
{
  if (SYS_IS_CALLED_FROM_ISR())
  {
    uint32_t priority;
    priority = taskENTER_CRITICAL_FROM_ISR();
    free(mem);
    taskEXIT_CRITICAL_FROM_ISR(priority);
  }
  else
  {
    taskENTER_CRITICAL();
    free(mem);
    taskEXIT_CRITICAL();
  }
}

/*
* Enable GCC_MALLOC_DEBUG in m1_core_config.h to debug heap
* You can monitor "m" struct in live watch
*/
#if (GCC_MALLOC_DEBUG == 1)
#include "malloc.h"

volatile struct mallinfo m;

void *gcc_malloc_debug(size_t size)
{
  void *p = malloc(size);

  m = mallinfo();
  return p;
}

void *gcc_calloc_debug(size_t num, size_t size)
{
  void *p = calloc(num, size);

  m = mallinfo();
  return p;
}

void gcc_free_debug(void *mem)
{
  free(mem);

  m = mallinfo();
}

#endif /* (GCC_MALLOC_DEBUG == 1) */
