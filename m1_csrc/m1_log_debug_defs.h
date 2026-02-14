/* See COPYING.txt for license details. */

/*
*
* m1_log_debug_defs.h
*
* Defines for logging and debugging functionality
*
* M1 Project
*
*/

#ifndef M1_LOG_DEBUG_DEFS_H_
#define M1_LOG_DEBUG_DEFS_H_

typedef enum {
	LOG_DEBUG_LEVEL_NONE = 0,
	LOG_DEBUG_LEVEL_ERROR,
	LOG_DEBUG_LEVEL_WARN,
	LOG_DEBUG_LEVEL_INFO,
	LOG_DEBUG_LEVEL_DEBUG,
	LOG_DEBUG_LEVEL_TRACE
} S_M1_LogDebugLevel_t;

#define M1_LOGDB_COLOR_SET(color)  	"\0x1B[0;" color "m" // Set text color
#define M1_LOGDB_COLOR_RESET 		"\0x1B[0m" // Reset all modes (styles and colors)

#define M1_LOGDB_COLOR_BLACK  	"30"
#define M1_LOGDB_COLOR_RED    	"31"
#define M1_LOGDB_COLOR_GREEN  	"32"
#define M1_LOGDB_COLOR_YELLOW 	"33"
#define M1_LOGDB_COLOR_BLUE   	"34"
#define M1_LOGDB_COLOR_CYAN   	"36"
#define M1_LOGDB_COLOR_WHITE 	"37"

#define M1_LOGDB_COLOR_ERROR 	M1_LOGDB_COLOR_SET(M1_LOGDB_COLOR_RED)
#define M1_LOGDB_COLOR_WARNING 	M1_LOGDB_COLOR_SET(M1_LOGDB_COLOR_YELLOW)
#define M1_LOGDB_COLOR_INFO 	M1_LOGDB_COLOR_SET(M1_LOGDB_COLOR_WHITE)
#define M1_LOGDB_COLOR_DEBUG 	M1_LOGDB_COLOR_SET(M1_LOGDB_COLOR_BLUE)
#define M1_LOGDB_COLOR_TRACE 	M1_LOGDB_COLOR_SET(M1_LOGDB_COLOR_GREEN)

extern void m1_logdb_printf(S_M1_LogDebugLevel_t level, const char* tag, const char* format, ...)
_ATTRIBUTE((__format__(__printf__, 3, 4)));

#endif /* M1_LOG_DEBUG_DEFS_H_ */
