/* See COPYING.txt for license details. */

/*
*
* m1_core_config.h
*
* Header for core functions
*
* M1 Project
*
*/

#ifndef M1_CORE_CONFIG_H_
#define M1_CORE_CONFIG_H_


#ifdef __cplusplus
extern "C" {
#endif

#define M1_MIN(a, b)             (((a) < (b)) ? (a) : (b))

/* Memory management macros */
#ifdef __ICCARM__
/* Use malloc debug functions (IAR only) */
#define IAR_MALLOC_DEBUG                          0
#else
#define GCC_MALLOC_DEBUG                          0
#endif /* __ICCARM__ */

#define MALLOC_CRITICAL_SECTION                     1

extern void *calloc_critical(size_t num, size_t size);
extern void *malloc_critical(size_t size);
extern void free_critical(void *mem);

#if (IAR_MALLOC_DEBUG == 1)
#define m1_malloc                                 malloc_debug
#define m1_calloc                                 calloc_debug
#define m1_free                                   free_debug
#elif (GCC_MALLOC_DEBUG == 1)
#define m1_malloc                                 gcc_malloc_debug
#define m1_calloc                                 gcc_calloc_debug
#define m1_free                                   gcc_free_debug
#elif (MALLOC_CRITICAL_SECTION == 1)
#define m1_malloc                                 malloc_critical
#define m1_calloc                                 calloc_critical
#define m1_free                                   free_critical
#else
#define m1_malloc                                 malloc
#define m1_calloc                                 calloc
#define m1_free                                   free
#endif /* MALLOC_DEBUG */

#ifndef m1_memset
#define m1_memset                                  memset
#endif /* m1_memset */

#ifndef m1_memcpy
#define m1_memcpy                                  memcpy
#endif /* m1_memcpy */

//#define GCC_MALLOC_DEBUG	1

#ifdef __cplusplus
}
#endif

#endif /* M1_CORE_CONFIG_H_ */
