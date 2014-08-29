/*
 * Copyright (c) 2014 Jim Tremblay
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#define NOS_PRIVATE
#include "nOS.h"

#if defined(__cplusplus)
extern "C" {
#endif

static nOS_Stack isrStack[NOS_CONFIG_ISR_STACK_SIZE];

void nOS_PortInit(void)
{
    register uint32_t volatile _msp __asm("msp");
    register uint32_t volatile _psp __asm("psp");
    register uint32_t volatile _control __asm("control");

    nOS_CriticalEnter();
    /* Copy msp to psp */
    _psp = _msp;
    /* Set msp to local isr stack */
    _msp = (((uint32_t)&isrStack[NOS_CONFIG_ISR_STACK_SIZE-1]) & 0xfffffff8UL);
    /* Set current stack to psp and priviledge mode */
    _control |= 0x00000002UL;
    /* Set PendSV and SysTick to lowest priority */
    *(volatile uint32_t *)0xe000ed20UL |= 0xffff0000UL;
    nOS_CriticalLeave();
}

void nOS_ContextInit(nOS_Thread *thread, nOS_Stack *stack, size_t ssize, void(*func)(void*), void *arg)
{
    nOS_Stack *tos = (nOS_Stack*)((uint32_t)(stack + (ssize - 1)) & 0xfffffff8UL);

    *(--tos) = 0x01000000UL;    /* xPSR */
    *(--tos) = (nOS_Stack)func; /* PC */
    *(--tos) = 0x00000000UL;    /* LR */
#if (NOS_CONFIG_DEBUG > 0)
    *(--tos) = 0x12121212UL;    /* R12 */
    *(--tos) = 0x03030303UL;    /* R3 */
    *(--tos) = 0x02020202UL;    /* R2 */
    *(--tos) = 0x01010101UL;    /* R1 */
#else
    tos     -= 4;               /* R12, R3, R2 and R1 */
#endif
    *(--tos) = (nOS_Stack)arg;  /* R0 */
#if (NOS_CONFIG_DEBUG > 0)
    *(--tos) = 0x11111111UL;    /* R11 */
    *(--tos) = 0x10101010UL;    /* R10 */
    *(--tos) = 0x09090909UL;    /* R9 */
    *(--tos) = 0x08080808UL;    /* R8 */
    *(--tos) = 0x07070707UL;    /* R7 */
    *(--tos) = 0x06060606UL;    /* R6 */
    *(--tos) = 0x05050505UL;    /* R5 */
    *(--tos) = 0x04040404UL;    /* R4 */
#else
    tos     -= 8;               /* R11, R10, R9, R8, R7, R6, R5 and R4 */
#endif

    thread->stackPtr = tos;
}

void nOS_IsrEnter (void)
{
    nOS_CriticalEnter();
    nOS_isrNestingCounter++;
    nOS_CriticalLeave();
}

void nOS_IsrLeave (void)
{
    nOS_CriticalEnter();
    nOS_isrNestingCounter--;
    if (nOS_isrNestingCounter == 0) {
#if (NOS_CONFIG_SCHED_LOCK_ENABLE > 0)
        if (nOS_lockNestingCounter == 0)
#endif
        {
            nOS_highPrioThread = SchedHighPrio();
            if (nOS_runningThread != nOS_highPrioThread) {
                *(volatile uint32_t *)0xe000ed04UL = 0x10000000UL;
            }
        }
    }
    nOS_CriticalLeave();
}

__asm void PendSV_Handler(void)
{
    extern nOS_runningThread;
    extern nOS_highPrioThread;

    /* Save PSP before doing anything, PendSV_Handler already running on MSP */
    MRS         R0,         PSP
    ISB

    /* Get the location of nOS_runningThread */
    LDR         R3,         =nOS_runningThread
    LDR         R2,         [R3]
    
    /* Make space for the remaining registers */
    SUBS        R0,         R0,             #32
    
    /* Save psp to nOS_Thread object of current running thread */
    STR         R0,         [R2]

    /* Push low registers on thread stack */
    STMIA       R0!,        {R4-R7}
    
    /* Copy high registers to low registers */
    MOV         R4,         R8
    MOV         R5,         R9
    MOV         R6,         R10
    MOV         R7,         R11
    /* Push high registers on thread stack */
    STMIA       R0!,        {R4-R7}

    /* Copy nOS_highPrioThread to nOS_runningThread */
    LDR         R1,         =nOS_highPrioThread
    LDR         R0,         [R1]
    STR         R0,         [R3]

    /* Restore psp from nOS_Thread object of high prio thread */
    LDR         R2,         [R1]
    LDR         R0,         [R2]
    
    /* Move to the high registers */
    ADDS        R0,         R0,             #16

    /* Pop high registers from thread stack */
    LDMIA       R0!,        {R4-R7}
    /* Copy low registers to high registers */
    MOV         R11,        R7
    MOV         R10,        R6
    MOV         R9,         R5
    MOV         R8,         R4

    /* Restore psp to high prio thread stack */
    MSR         PSP,        R0
    ISB
    
    /* Go back for the low registers */
    SUBS        R0,         R0,             #32
    
    /* Pop low registers from thread stack */
    LDMIA      R0!,        {R4-R7}

    /* Return */
    BX          LR
    NOP
}

#if defined(__cplusplus)
}
#endif
