/* Copyright (c) 2025 Codasip s.r.o. (port to RISC-V)
 * Copyright (c) 2013-2023 Arm Limited. All rights reserved. (RTX names and definitions from irq_armv6m.S)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* RISC-V rv32i Calling Convention, see https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf
 *
 * Register ABI Name Description                        Saver   Save only if task swap
 * -------- -------- -----------                        -----   ----------------------
 * x0       zero     Hard-wired zero                    —
 * x1       ra       Return address                     Caller
 * x2       sp       Stack pointer                      Callee  Yes
 * x3       gp       Global pointer                     —
 * x4       tp       Thread pointer                     —
 * x5–7     t0–2     Temporaries                        Caller
 * x8       s0/fp    Saved register/frame pointer       Callee  Yes
 * x9       s1       Saved register                     Callee  Yes
 * x10–11   a0–1     Function arguments/return values   Caller
 * x12–17   a2–7     Function arguments                 Caller
 * x18–27   s2–11    Saved registers                    Callee  Yes
 * x28–31   t3–6     Temporaries                        Caller
 *
 * ToDo: Floating Point context save/restore
 * f0–7     ft0–7    FP temporaries                     Caller
 * f8–9     fs0–1    FP saved registers                 Callee  Yes
 * f10–11   fa0–1    FP arguments/return values         Caller
 * f12–17   fa2–7    FP arguments                       Caller
 * f18–27   fs2–11   FP saved registers                 Callee  Yes
 * f28–31   ft8–11   FP temporaries                     Caller
 */

// ToDo RISC-V Future Stack Frames:
//  - rv32e:    (Reduced base integer extension)
//  - rv32if:   (Single-precision floating-point extension)

#if( __riscv_xlen != 32 )
    #error RISC-V Core must be 32-bits for RTX
#endif

#include "rtx_def.h"
#include "platform.h"   /* For CLICINT_ADDR definition */

    .equ     I_T_RUN_OFS,       20  // osRtxInfo.thread.run offset
    .equ     TCB_SP_OFS,        56  // TCB.SP offset
//  .equ     TCB_ZONE_OFS,      68  // TCB.zone offset NOT USED

#if 0
    // Not used yet
    .equ     osRtxErrorStackOverflow, 1 // Stack overflow
    .equ     osRtxErrorSVC,           6 // Invalid SVC function called
#endif

    .section ".rodata"
    .global  irqRtxLib              // Non weak library reference
irqRtxLib:
    .byte    0

/******************************************************************************/
/* Define SVC_FAST to 1 to only stack out registers that need saving at that time,
 * so making SVC calls faster when no context switch is required */
#ifndef SVC_FAST
    #define SVC_FAST (1)
#endif

#define RTX_CONTEXT_SIZE (32 * 4)

#ifdef RTX_EXECUTION_ZONE
    #warning RTX_EXECUTION_ZONE not supported in RISC-V
#endif

/******************************************************************************/
.global risc_v_trap_handler
.global osRtxPendSV_Handler
.global osRtxTick_Handler
.global codasip_clic_irq_clear_pending_call

.weak rtx_risc_v_exception_handler
.weak rtx_risc_v_interrupt_handler

/******************************************************************************/
    .section ".data"
    .global  ExceptionNestCtr
ExceptionNestCtr:
    .word   0

    .global  SysTickNestCtr
SysTickNestCtr:
    .word   0

    .global  ISRNestCtr
ISRNestCtr:
    .word   0

    .global  SVCallNestCtr
SVCallNestCtr:
    .word   0

    .global  PendSVNestCtr
PendSVNestCtr:
    .word   0

/******************************************************************************/
/* RISC-V Trap Handler */
    .section .text.risc_v_trap_handler
    .balign 64  /* Trap handler needs to be aligned to 64 byte boundary for the CLIC mtvec register */

risc_v_trap_handler:
    /* Save context to current stack */
    addi    sp, sp, -RTX_CONTEXT_SIZE

    /* Save all or, if SVC_FAST, Caller Save registers from stack (except x0 & sp) */
    /*      x0 is hard-wired to zero */
    sw      x1,  0(sp)
    /*      x2 is the sp */
    sw      x3,  4(sp)
    sw      x4,  8(sp)
    sw      x5,  12(sp)
    sw      x6,  16(sp)
    sw      x7,  20(sp)
#if !SVC_FAST
    sw      x8,  24(sp)
    sw      x9,  28(sp)
#endif
    sw      x10, 32(sp) /* a0 SVC return value */
    sw      x11, 36(sp)
    sw      x12, 40(sp)
    sw      x13, 44(sp)
    sw      x14, 48(sp)
    sw      x15, 52(sp)
    sw      x16, 56(sp)
    sw      x17, 60(sp)
#if !SVC_FAST
    sw      x18, 64(sp)
    sw      x19, 68(sp)
    sw      x20, 72(sp)
    sw      x21, 76(sp)
    sw      x22, 80(sp)
    sw      x23, 84(sp)
    sw      x24, 88(sp)
    sw      x25, 92(sp)
    sw      x26, 96(sp)
    sw      x27, 100(sp)
#endif
    sw      x28, 104(sp)
    sw      x29, 108(sp)
    sw      x30, 112(sp)
    sw      x31, 116(sp)

    /* Get the exception cause and pc values, save them */
    csrr    t1, mcause
    csrr    t2, mepc
    sw      t1, 120(sp)
/*  sw      t2, 124(sp) Done later in the code. This is the return address for an interrupt, but for an ecall - this needs 4 added to it */
    /* ToDo needs to save mstatus for non-CLIC port */

    /* Check for (async) interrupt or (sync) exception */
    blt     t1, x0, asynchronous_interrupt   /* If the top bit of mcause is set (i.e. a negative number) then this is an interrupt */


SVC_Handler:
    /* ecall SVC Handler, SVC Function address in t0 */
    addi    t2, t2, 4   /* Increment return address to the next instruction after the ecall instruction */
    sw      t2, 124(sp) /* Save (mepc + 4) to the stack */


    /* ecall numbering (SVC_Number and SVC_User) not implemented in this RISC-V port */
#ifdef RTX_SVC_PTR_CHECK
    #warning RTX_SVC_PTR_CHECK not yet supported in this RISC-V port
#endif

    /* Check for ecall, branch if another exception */
    li      t3, 0xfff       /* Get Exception Code from mcause (CLIC Mode uses lower 12-bits only) */
    and     t3, t3, t1
    li      t4, 11          /* 11 = Environment call from M-mode */
    bne     t3, t4, SVC_ExceptionHandler

    /* Increment Service Call Nesting Counter */
    la      t4, SVCallNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, 1
    sw      t5, 0(t4)

    /* ecall function parameters are already in the correct registers a0-a4 (see rtx_core_l110.h),
     * so just call the SVC function (address in t0) */
    jalr    ra, t0, 0   /* Call the SVC function, return value in a0, if applicable */
    sw      a0, 32(sp)  /* a0 (x10) contains the SVC return value, save to context on the stack */

    /* Deccrement Service Call Nesting Counter */
    la      t4, SVCallNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, -1
    sw      t5, 0(t4)

    j       SVC_Context


SVC_ExceptionHandler:
    /* Increment Exception Nesting Counter */
    la      t4, ExceptionNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, 1
    sw      t5, 0(t4)

    mv      a0, t3  /* Put the exception number as the first argument when calling rtx_risc_v_exception_handler(uint32_t exception_number) */
    call    rtx_risc_v_exception_handler

    /* Decrement Exception Nesting Counter */
    la      t4, ExceptionNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, -1
    sw      t5, 0(t4)

    j       SVC_Context


asynchronous_interrupt:
    /* Check for PendSV_Handler and SysTick_Handler interrupts, otherwise call rtx_risc_v_interrupt_handler */
    /* If PendSV_Handler and/or SysTick_Handler interrupts, then check for a context switch */

    sw      t2, 124(sp) /* Save mepc to the stack */

    /* ToDo MAYBE Swap to interrupt stack */

PendSV_Handler:
    li      t3, 0xfff       /* Get Interrupt Number from mcause (CLIC Mode uses lower 12-bit only) */
    and     t3, t3, t1
    li      t4, SOFTWARE_IRQ
    bne     t3, t4, SysTick_Handler

    /* Increment PendSV Nesting Counter */
    la      t4, PendSVNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, 1
    sw      t5, 0(t4)

    /* Call the OS Pending Handler */
    call    osRtxPendSV_Handler

    /* Decrement PendSV Nesting Counter */
    la      t4, PendSVNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, -1
    sw      t5, 0(t4)
    j       SVC_Interrupt_Exit


SysTick_Handler:
    li      t4, CLINT_MTIMER_IRQ
    bne     t3, t4, SVC_Interrupt_Unrecognised

    /* Increment SysTick Nesting Counter */
    la      t4, SysTickNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, 1
    sw      t5, 0(t4)

    /* Call the OS Tick Handler (this then calls OS_Tick_AcknowledgeIRQ()) */
    call    osRtxTick_Handler

    /* Decrement SysTick Nesting Counter */
    la      t4, SysTickNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, -1
    sw      t5, 0(t4)

    j       SVC_Interrupt_Exit_Acked


SVC_Interrupt_Unrecognised:
    /* Increment ISR Nesting Counter */
    la      t4, ISRNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, 1
    sw      t5, 0(t4)

    mv      a0, t3  /* Put the interrupt number as the first argument when calling rtx_risc_v_interrupt_handler(uint32_t interrupt_number) */
    call    rtx_risc_v_interrupt_handler

    /* Decrement ISR Nesting Counter */
    la      t4, ISRNestCtr
    lw      t5, 0(t4)
    addi    t5, t5, -1
    sw      t5, 0(t4)


SVC_Interrupt_Exit:
    /* Acknowledge the non-vectored interrupt */
    lw      t1, 120(sp)     /* Load the saved mcause register value (as it may have been clobbered
                             * by a call to osRtxPendSV_Handler() or rtx_risc_v_interrupt_handler()) */
    li      a0, 0xfff       /* Get Interrupt Number from mcause (CLIC Mode uses lower 12-bit only) */
    and     a0, a0, t1

    /* Clear the Pending Interrupt in CLIC for non-vectored interrupts */
#if 0
    call    codasip_clic_irq_clear_pending

#else
    /* Inlined call to codasip_clic_irq_clear_pending() */
    slli    a0, a0, 0x2
    li      a5, CLICINT_ADDR
    add     a5, a5, a0
    lbu     a4, 0(a5)
    andi    a4, a4, -2
    sb      a4, 0(a5)
#endif


SVC_Interrupt_Exit_Acked:
    /* Non-Vectored Interrupt has already been Acknowledged */


    /* ToDo MAYBE Swap back from interrupt stack */


    /* Check for context switch */
SVC_Context:
    la      a3, osRtxInfo+I_T_RUN_OFS   // Load address of osRtxInfo.thread.run
    lw      a1, 0(a3)                   // Load osRtxInfo.thread.run: curr & next
    lw      a2, 4(a3)
    beq     a1, a2, SVC_Exit_Fast       // Check if thread switch is required, if not branch when threads are the same

    sw      a2, 0(a3)                   // osRtxInfo.thread.run: curr = next
    beq     a1, x0, SVC_ContextRestore  // Branch if running thread is deleted


    /* Context switch required */
SVC_ContextSave:
#if SVC_FAST
    /* Save the remaining Callee Save registers ready for context switch */
    sw      x8,  24(sp)
    sw      x9,  28(sp)

    sw      x18, 64(sp)
    sw      x19, 68(sp)
    sw      x20, 72(sp)
    sw      x21, 76(sp)
    sw      x22, 80(sp)
    sw      x23, 84(sp)
    sw      x24, 88(sp)
    sw      x25, 92(sp)
    sw      x26, 96(sp)
    sw      x27, 100(sp)
#endif

    sw      sp, TCB_SP_OFS(a1)          // Save SP to osRtxInfo.thread.run.curr


#ifdef RTX_STACK_CHECK
    // ToDo RTX_STACK_CHECK
    #warning RTX_STACK_CHECK  not yet supported in this RISC-V port
#endif


SVC_ContextRestore:
    /* Context switch restore */
    lw      sp, TCB_SP_OFS(a2)          // Load SP from osRtxInfo.thread.run.next

#if SVC_FAST
    /* Restore Callee Save registers for context switch */
    lw      x8,  24(sp)
    lw      x9,  28(sp)

    lw      x18, 64(sp)
    lw      x19, 68(sp)
    lw      x20, 72(sp)
    lw      x21, 76(sp)
    lw      x22, 80(sp)
    lw      x23, 84(sp)
    lw      x24, 88(sp)
    lw      x25, 92(sp)
    lw      x26, 96(sp)
    lw      x27, 100(sp)
#endif


SVC_Exit_Fast:
    /* Restore all or, if SVC_FAST, Caller Save registers from stack (except x0 & sp) */
    lw      t1, 120(sp)
    lw      t2, 124(sp)
    csrw    mcause, t1
    csrw    mepc, t2
    /* ToDo needs to restore mstatus for non-CLIC port */

    /*      x0 is hard-wired to zero */
    lw      x1,  0(sp)
    /*      x2 is the sp */
    lw      x3,  4(sp)
    lw      x4,  8(sp)
    lw      x5,  12(sp)
    lw      x6,  16(sp)
    lw      x7,  20(sp)
#if !SVC_FAST
    lw      x8,  24(sp)
    lw      x9,  28(sp)
#endif
    lw      x10, 32(sp) /* a0 ecall return register */
    lw      x11, 36(sp)
    lw      x12, 40(sp)
    lw      x13, 44(sp)
    lw      x14, 48(sp)
    lw      x15, 52(sp)
    lw      x16, 56(sp)
    lw      x17, 60(sp)
#if !SVC_FAST
    lw      x18, 64(sp)
    lw      x19, 68(sp)
    lw      x20, 72(sp)
    lw      x21, 76(sp)
    lw      x22, 80(sp)
    lw      x23, 84(sp)
    lw      x24, 88(sp)
    lw      x25, 92(sp)
    lw      x26, 96(sp)
    lw      x27, 100(sp)
#endif
    lw      x28, 104(sp)
    lw      x29, 108(sp)
    lw      x30, 112(sp)
    lw      x31, 116(sp)

    /* Update stack pointer */
    addi    sp, sp, RTX_CONTEXT_SIZE

    /* Return from Machine Mode */
    mret


/******************************************************************************/
.section .text.rtx_risc_v_exception_handler
rtx_risc_v_exception_handler:
    csrr    t0, mcause     /* For viewing in the debugger */
    csrr    t1, mepc       /* For viewing in the debugger */
    csrr    t2, mstatus    /* For viewing in the debugger */
    j       .

/******************************************************************************/
.section .text.rtx_risc_v_interrupt_handler
rtx_risc_v_interrupt_handler:
    csrr    t0, mcause     /* For viewing in the debugger */
    csrr    t1, mepc       /* For viewing in the debugger */
    csrr    t2, mstatus    /* For viewing in the debugger */
    j       .
