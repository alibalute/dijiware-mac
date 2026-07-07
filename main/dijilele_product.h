#ifndef DIJILELE_PRODUCT_H
#define DIJILELE_PRODUCT_H

/**
 * Product profile IDs telemetered to DijiApp over BLE (byte 0x5D, value, 0).
 * Override at build time: DIJILELE_PRODUCT_ID=2 ./build-16mb.sh build
 *

 *  1 = Dijilele M
 *  2 = Dijilele S
 */
#ifndef DIJILELE_PRODUCT_ID
#if defined(INST_DIJILELE_M)
#define DIJILELE_PRODUCT_ID 1
#elif defined(INST_DIJILELE_S)
#define DIJILELE_PRODUCT_ID 2
#else
#define DIJILELE_PRODUCT_ID 0
#endif
#endif

#define DIJILELE_TELEMETRY_PRODUCT 0x5D

#endif
