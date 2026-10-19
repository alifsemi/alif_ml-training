/*
 * Ethos-U NPU driver initialization for Alif Ensemble.
 * U85 is the shared NPU. U55 is the per-core local NPU.
 */

#include "RTE_Components.h"
#include CMSIS_device_header

#include "ethosu_driver.h"
#include "ethosu_init.h"

/* Ethos-U NPU driver instance */
static struct ethosu_driver npuDriver;

/* Ethos-U NPU interrupt handler - overrides the weak vector table entry */
#ifdef ETHOSU85
void IRQ366_Handler(void)
#else
void NPULOCAL_IRQHandler(void)
#endif
{
    ethosu_irq_handler(&npuDriver);
}

int32_t npu_init(void)
{
#ifdef ETHOSU85
    /* U85 is a shared NPU at a different base address */
    void *const npuBaseAddr = (void *)(NPU_HG_BASE);
#else
    /* U55 is a per-core local NPU */
    void *const npuBaseAddr = (void *)(NPULOCAL_BASE);
#endif

    /* Initialize Arm Ethos-U NPU driver */
    if (ethosu_init(&npuDriver,    /* Ethos-U driver instance    */
                    npuBaseAddr,   /* Ethos-U NPU base address   */
                    0,             /* Cache memory pointer (N/A) */
                    0,             /* Cache memory size          */
                    1,             /* Secure mode                */
                    1))            /* Privileged mode            */
    {
        return -1;
    }

#ifdef ETHOSU85
    NVIC_EnableIRQ((IRQn_Type)366);   /* NPU_HG IRQ */
#else
    NVIC_EnableIRQ(NPULOCAL_IRQ_IRQn);
#endif

    return 0;
}

struct ethosu_driver *npu_get_driver(void)
{
    return &npuDriver;
}
