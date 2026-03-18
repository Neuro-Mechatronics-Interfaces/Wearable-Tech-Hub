#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// We link pico_cyw43_arch_none (BLE only, no WiFi/lwIP), but the CYW43 headers
// still pull in lwipopts.h via indirect includes.  Provide an empty-but-valid
// header so the build doesn't fail.

// NO_SYS=1 means no OS abstraction layer (bare metal).
#define NO_SYS          1
#define LWIP_SOCKET     0
#define LWIP_NETCONN    0
#define LWIP_NETIF_API  0

#endif // LWIPOPTS_H
