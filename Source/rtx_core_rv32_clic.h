/* Copyright (c) 2025 Codasip s.r.o. (port to RISC-V)
 * Copyright (c) 2013-2023 Arm Limited. All rights reserved. (Original rtx_core_cm.h code)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * -----------------------------------------------------------------------------
 *
 * Project:     CMSIS-RTOS RTX
 * Title:       Codasip RISC-V 32-bit with CLIC Core definitions
 *
 * -----------------------------------------------------------------------------
 */

#ifndef RTX_CORE_RV32_CLIC_H_
#define RTX_CORE_RV32_CLIC_H_

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_compiler.h"
#include "core_rv32_clic.h" /* In Codasip's port of CMSIS_6 */

typedef bool bool_t;

#ifndef FALSE
#define FALSE                   ((bool_t)0)
#endif

#ifndef TRUE
#define TRUE                    ((bool_t)1)
#endif

#define EXCLUSIVE_ACCESS        0   /* No atomic instructions with L110, need to disable interrupts instead */

#define OS_TICK_HANDLER         NULL /* Was SysTick_Handler. This is ignored by OS_Tick_Setup() and hard coded in to irq_codasip_l110.S */
#ifndef SOFTWARE_IRQ
#error Please define SOFTWARE_IRQ to a free CLIC Interrupt line, required for RTX Pending Service Call deferred interrupt (OS_PEND_SV_IRQ).
#endif
#define OS_PEND_SV_IRQ          SOFTWARE_IRQ

#define RTX_CONTEXT_REGS        (32U)
#define RTX_CONTEXT_SIZE        (RTX_CONTEXT_REGS * sizeof(uint32_t))
#define RTX_CONTEXT_A0_IDX      (8U)
#define RTX_CONTEXT_A0_OFS      (RTX_CONTEXT_A0_IDX * sizeof(uint32_t))

/* This is a RISC-V port of an ARM register read */
/**************************************************************************************************/
/** \brief  Read the PRIMASK register bit
    \details
    The function reads the Priority Mask register (PRIMASK) value using the instruction \b MRS.
    \n\n
    PRIMASK is a 1-bit-wide interrupt mask register. When set,
    it blocks all interrupts apart from the non-maskable interrupt (NMI) and the hard fault exception.
    The PRIMASK prevents activation of all exceptions with configurable priority.

    \returns    PRIMASK register value
                - =0 no effect
                - =1 prevents the activation of all exceptions with configurable priority

    \sa
        - \ref __set_PRIMASK; __get_BASEPRI; __get_FAULTMASK
        - \ref ref_man_sec "Cortex-M Generic User Guides"
 */
__STATIC_INLINE uint32_t __get_PRIMASK(void)
{
    uint32_t mie;

    /* Read the mstatus register to get the Machine Interrupt Enable (MIE) bit */
    __asm volatile ( "csrr %0, mstatus" : "=r"(mie) );

    mie &= 8;

    /* When PRIMASK is 1 interrupts are disabled, but when MIE is 0 interrupts are disabled,
     * so negate mie to match PRIMASK */
    return !mie;
}

#if 0
/// xPSR_Initialization Value
/// \param[in]  privileged      true=privileged, false=unprivileged
/// \param[in]  thumb           true=Thumb, false=ARM
/// \return                     xPSR Init Value
__STATIC_INLINE uint32_t xPSR_InitVal (bool_t privileged, bool_t thumb) {
  (void)privileged;
  (void)thumb;
  return (0);
}
#endif

// Stack Frame:
// ARM:
//  - Extended: S16-S31, R4-R11, R0-R3, R12, LR, PC, xPSR, S0-S15, FPSCR
//  - Basic:             R4-R11, R0-R3, R12, LR, PC, xPSR
//
// RISC-V:
//  - rv32i:     x1, x3-x31, mcause, mepc   (Base integer extension)
//
// ToDo RISC-V Future Stack Frames:
//  - rv32e:    (Reduced base integer extension)
//  - rv32if:   (Single-precision floating-point extension)

/// Stack Frame Initialization Value (EXC_RETURN[7..0])
/// This is used in svcRtxThreadNew (rtx_thread.c) to initialise:
///     thread->stack_frame   = STACK_FRAME_INIT_VAL;
#if (DOMAIN_NS == 1)    // ToDo What is DOMAIN_NS ???
#define STACK_FRAME_INIT_VAL    0xBCU
#else
#define STACK_FRAME_INIT_VAL    0xFDU
#endif

/// Stack Offset of Register R0
/// \param[in]  stack_frame     Stack Frame (EXC_RETURN[7..0])
/// \return                     R0 Offset
__STATIC_INLINE uint32_t StackOffsetR0 (uint8_t stack_frame) {
#if ((__FPU_USED == 1U) || \
     (defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE > 0)))
#error RISC-V __FPU_USED == 1U not supported yet
  return (((stack_frame & 0x10U) == 0U) ? ((16U+8U)*4U) : (8U*4U));
#else
  (void)stack_frame;
  return (RTX_CONTEXT_A0_OFS); // Register a0 stack offset, a.k.a. register x10, which is the 8th register in the
                               // stack frame (x0 and x2/sp not saved). See irq_codasip_l110.S for the stack frame.
#endif
}

//  ==== Core functions ====

//lint -sem(__get_CONTROL, pure)
//lint -sem(__get_IPSR,    pure)
//lint -sem(__get_PRIMASK, pure)
//lint -sem(__get_BASEPRI, pure)

/// Check if running Privileged
/// \return     true=privileged, false=unprivileged
__STATIC_INLINE bool_t IsPrivileged (void) {
  return 0; // ToDo RISC-V User/Machine mode. ARM: ((__get_CONTROL() & 1U) == 0U);
}

/// Set thread Privileged mode
/// \param[in]  privileged      true=privileged, false=unprivileged
__STATIC_INLINE void SetPrivileged (bool_t privileged) {
  if (privileged) {
    // Privileged Thread mode & PSP
    // ToDo RISC-V User/Machine mode. ARM: __set_CONTROL(0x02U);
  } else {
    // Unprivileged Thread mode & PSP
    // ToDo RISC-V User/Machine mode. ARM: __set_CONTROL(0x03U);
  }
}

/// Check if in (ARM terminology) Exception (e.g. any RISC-V trap [exceptions and IRQs])
/// \return     true=exception, false=thread
/// ARM: return (__get_IPSR() != 0U);
__STATIC_INLINE bool_t IsException (void) {
  extern uint32_t SVCallNestCtr;
  extern uint32_t ExceptionNestCtr;
  return (SVCallNestCtr || ExceptionNestCtr || codasip_clic_irq_active());
}

/// Check if in Fault
/// \return     true, false
__STATIC_INLINE bool_t IsFault (void) {
  // ToDo uint32_t ipsr = __get_IPSR();
  return 0; // ARM: (((int32_t)ipsr < ((int32_t)SVCall_IRQn + 16)) &&
            // ((int32_t)ipsr > ((int32_t)NonMaskableInt_IRQn + 16)));
}

/// Check if in SVCall (eCall)
/// \return     true, false
__STATIC_INLINE bool_t IsSVCallIrq (void) {
  extern uint32_t SVCallNestCtr;
  return SVCallNestCtr != 0;
}

/// Check if in PendSV IRQ
/// \return     true, false
__STATIC_INLINE bool_t IsPendSvIrq (void) {
  extern uint32_t PendSVNestCtr;
  return PendSVNestCtr != 0;
}

/// Check if in Tick Timer IRQ
/// \return     true, false
__STATIC_INLINE bool_t IsTickIrq (int32_t tick_irqn) {
  extern uint32_t SysTickNestCtr;
  (void) tick_irqn;
  return SysTickNestCtr != 0;
}

/// Check if IRQ is Masked
/// \return     true=masked, false=not masked
__STATIC_INLINE bool_t IsIrqMasked (void) {
  return (__get_PRIMASK() != 0U); /* Interrupts are disabled (masked) when PRIMASK == 1 */
}


//  ==== Core Peripherals functions ====

/// Setup SVC and PendSV System Service Calls
__STATIC_INLINE void SVC_Setup (void) {

    /* Setup the SysTick_IRQn with CLIC level=0, priority=minimum 1, +ve edge triggered, make it non-vectored
     * This is the lowest CLIC level/priority.
     *
     * clicintctl[i] will be 0b0000000.1 when CLICINTCTLBITS==7/8 bits & mnlbits==7/8, this is to simulate the ARM
     *      NVIC minimum PRIGROUP (c.f. CLICINTCTLBITS) of 0 which splits NVIC Priority/SubPriority to 7.1 bits.
     * clicintctl[i] will be 0b000.11111 when CLICINTCTLBITS==3 bits & mnlbits==3
     *
     * See: https://developer.arm.com/documentation/ddi0337/e/Nested-Vectored-Interrupt-Controller/NVIC-programmer-s-model/NVIC-register-descriptions
     */
    uint32_t PriorityGroup = NVIC_GetPriorityGrouping(); /* This is the binary point, see core_rv32_clic.h */
    uint32_t PreemptPriority = (1UL << (7 - PriorityGroup)) - 1;
    uint32_t SubPriority     = 1;
    uint32_t EncodedPriority = NVIC_EncodePriority (PriorityGroup, PreemptPriority, SubPriority);
    NVIC_SetPriorityTrig(OS_PEND_SV_IRQ, EncodedPriority, CLIC_TRIG_EDGE_POS);
    NVIC_SetVector(OS_PEND_SV_IRQ, (uint32_t) NULL);

    NVIC_ClearPendingIRQ(OS_PEND_SV_IRQ);
    NVIC_EnableIRQ(OS_PEND_SV_IRQ);
}

/// Get Pending SV (Service Call) Flag
/// \return     Pending SV Flag
__STATIC_INLINE uint8_t GetPendSV (void) {
    return NVIC_GetPendingIRQ(OS_PEND_SV_IRQ);
}

/// Clear Pending SV (Service Call) Flag
__STATIC_INLINE void ClrPendSV (void) {
    NVIC_ClearPendingIRQ(OS_PEND_SV_IRQ);
}

/// Set Pending SV (Service Call) Flag
__STATIC_INLINE void SetPendSV (void) {
    NVIC_SetPendingIRQ(OS_PEND_SV_IRQ);
}


//  ==== Service Calls definitions ====

//lint -save -e9023 -e9024 -e9026 "Function-like macros using '#/##'" [MISRA Note 10]

#if defined(RTX_SVC_PTR_CHECK)
#warning "SVC Function Pointer checking is not supported!"
#endif

//lint -esym(522,__svc*) "Functions '__svc*' are impure (side-effects)"

/* RISC-V Registers */
#define SVC_RegF "t0"   /* RISC-V Temporary register that must be saved by the callee if used
                         * after the call, so we are using it to store the SVC function pointer
                         * for the ecall handler */

/* Function Return register aX naming to a C variable __aX e.g. __a0 */
#define SVC_ArgN(n) \
register uint32_t __a##n __ASM("a"#n)

/* Function Argument(s) registers (a0-a7) and Return register (a0) naming to a C variable(s) __aX */
#define SVC_ArgR(n,a) \
register uint32_t __a##n __ASM("a"#n) = (uint32_t)a

/* svcRtxFunctionName Function Pointer loading to temporary t0 register for the ecall handler */
#define SVC_ArgF(f) \
register uint32_t __rf   __ASM(SVC_RegF) = (uint32_t)svcRtx##f

/* Tell the compiler which input register(s) are being used by the ecall (using the C variable name) */
#define SVC_In0 "r"(__rf)
#define SVC_In1 "r"(__rf),"r"(__a0)
#define SVC_In2 "r"(__rf),"r"(__a0),"r"(__a1)
#define SVC_In3 "r"(__rf),"r"(__a0),"r"(__a1),"r"(__a2)
#define SVC_In4 "r"(__rf),"r"(__a0),"r"(__a1),"r"(__a2),"r"(__a3)

/* Tell the compiler which output register is being used by the ecall (using the C variable name) */
#define SVC_Out0
#define SVC_Out1 "=r"(__a0)

/* Tell the compiler which output register will be clobbered by the ecall (using the register name) */
#define SVC_CL0
#define SVC_CL1 "a0"

#define SVC_Call0(in, out, cl)                                                 \
  __ASM volatile ("ecall" : out : in : cl)

#if    (defined(RTX_SVC_PTR_CHECK) && !defined(_lint))
// ToDo SVC_Jump(f) is not used - remove?
#error SVC_Jump(f) not yet implemented in this port
#define SVC_Jump(f)                                                            \
  __ASM volatile (                                                             \
    ".align 3\n\t"                                                             \
    "ldr r7,1f\n\t"                                                            \
    "bx  r7\n"                                                                 \
    "1: .word %[adr]" : : [adr] "X" (f)                                        \
  )

#define SVC_Veneer_Prototye(f)                                                 \
__STATIC_INLINE void jmpRtx##f (void);
#define SVC_Veneer_Function(f)                                                 \
__attribute__((naked,section(".text.os.svc.veneer."#f)))                       \
__STATIC_INLINE void jmpRtx##f (void) {                                        \
  SVC_Jump(svcRtx##f);                                                         \
}
#else
#define SVC_Veneer_Prototye(f)
#define SVC_Veneer_Function(f)
#endif

#define SVC0_0N(f,t)                                                           \
SVC_Veneer_Prototye(f)                                                         \
__attribute__((always_inline))                                                 \
__STATIC_INLINE t __svc##f (void) {                                            \
  SVC_ArgF(f);                                                                 \
  SVC_Call0(SVC_In0, SVC_Out0, SVC_CL1);                                       \
}                                                                              \
SVC_Veneer_Function(f)

#define SVC0_0(f,t)                                                            \
SVC_Veneer_Prototye(f)                                                         \
__attribute__((always_inline))                                                 \
__STATIC_INLINE t __svc##f (void) {                                            \
  SVC_ArgN(0);                                                                 \
  SVC_ArgF(f);                                                                 \
  SVC_Call0(SVC_In0, SVC_Out1, SVC_CL0);                                       \
  return (t) __a0;                                                             \
}                                                                              \
SVC_Veneer_Function(f)

#define SVC0_1N(f,t,t1)                                                        \
SVC_Veneer_Prototye(f)                                                         \
__attribute__((always_inline))                                                 \
__STATIC_INLINE t __svc##f (t1 a1) {                                           \
  SVC_ArgR(0,a1);                                                              \
  SVC_ArgF(f);                                                                 \
  SVC_Call0(SVC_In1, SVC_Out1, SVC_CL0);                                       \
}                                                                              \
SVC_Veneer_Function(f)

#define SVC0_1(f,t,t1)                                                         \
SVC_Veneer_Prototye(f)                                                         \
__attribute__((always_inline))                                                 \
__STATIC_INLINE t __svc##f (t1 a1) {                                           \
  SVC_ArgR(0,a1);                                                              \
  SVC_ArgF(f);                                                                 \
  SVC_Call0(SVC_In1, SVC_Out1, SVC_CL0);                                       \
  return (t) __a0;                                                             \
}                                                                              \
SVC_Veneer_Function(f)

#define SVC0_2(f,t,t1,t2)                                                      \
SVC_Veneer_Prototye(f)                                                         \
__attribute__((always_inline))                                                 \
__STATIC_INLINE t __svc##f (t1 a1, t2 a2) {                                    \
  SVC_ArgR(0,a1);                                                              \
  SVC_ArgR(1,a2);                                                              \
  SVC_ArgF(f);                                                                 \
  SVC_Call0(SVC_In2, SVC_Out1, SVC_CL0);                                       \
  return (t) __a0;                                                             \
}                                                                              \
SVC_Veneer_Function(f)

#define SVC0_3(f,t,t1,t2,t3)                                                   \
SVC_Veneer_Prototye(f)                                                         \
__attribute__((always_inline))                                                 \
__STATIC_INLINE t __svc##f (t1 a1, t2 a2, t3 a3) {                             \
  SVC_ArgR(0,a1);                                                              \
  SVC_ArgR(1,a2);                                                              \
  SVC_ArgR(2,a3);                                                              \
  SVC_ArgF(f);                                                                 \
  SVC_Call0(SVC_In3, SVC_Out1, SVC_CL0);                                       \
  return (t) __a0;                                                             \
}                                                                              \
SVC_Veneer_Function(f)

#define SVC0_4(f,t,t1,t2,t3,t4)                                                \
SVC_Veneer_Prototye(f)                                                         \
__attribute__((always_inline))                                                 \
__STATIC_INLINE t __svc##f (t1 a1, t2 a2, t3 a3, t4 a4) {                      \
  SVC_ArgR(0,a1);                                                              \
  SVC_ArgR(1,a2);                                                              \
  SVC_ArgR(2,a3);                                                              \
  SVC_ArgR(3,a4);                                                              \
  SVC_ArgF(f);                                                                 \
  SVC_Call0(SVC_In4, SVC_Out1, SVC_CL0);                                       \
  return (t) __a0;                                                             \
}                                                                              \
SVC_Veneer_Function(f)

//lint -restore [MISRA Note 10]


//  ==== Exclusive Access Operation ====

#if (EXCLUSIVE_ACCESS == 1)
#error No exclusive-access/atomic instructions with L110, need to disable interrupts instead (so #define EXCLUSIVE_ACCESS 0).
#endif  // (EXCLUSIVE_ACCESS == 1)

#endif /* !#ifndef RTX_CORE_RV32_CLIC_H_ */
