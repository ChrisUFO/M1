/* See COPYING.txt for license details. */
/* Host-test stub: logging for the REAL vendored IRMP on POSIX.
* IRMP_HOST_VERBOSE (set via -D) turns the vendor's own trace on; the
* golden-decode wrapper builds non-verbose so passes stay quiet. */
#ifndef STUB_IRMP_HOST_M1_LOG_DEBUG_H_
#define STUB_IRMP_HOST_M1_LOG_DEBUG_H_

#ifdef IRMP_HOST_VERBOSE
#define M1_LOG_D(tag, fmt, ...)	printf("[" tag "] " fmt, ##__VA_ARGS__)
#define M1_LOG_E(tag, fmt, ...)	printf("[" tag "] E " fmt, ##__VA_ARGS__)
#else
#define M1_LOG_D(tag, fmt, ...)	do { } while (0)
#define M1_LOG_E(tag, fmt, ...)	do { } while (0)
#endif
#define M1_LOG_W(tag, fmt, ...)	do { } while (0)
#define M1_LOG_I(...)	do { } while (0)

#endif /* STUB_IRMP_HOST_M1_LOG_DEBUG_H_ */
