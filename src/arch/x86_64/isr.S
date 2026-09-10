# Copyright (c) 2026 Hunter Shurniak. All rights reserved.

.section .text

.extern isr_handler

#------------------------------------------------------------
# ISR stubs
#
# For exceptions without a CPU-pushed error code:
#     pushq $0
#     pushq $vector
#
# For exceptions with a CPU-pushed error code:
#     pushq $vector
#
# After either variant, the stack is:
#
#     vector
#     error_code
#     rip
#     cs
#     rflags
#     rsp
#     ss
# ------------------------------------------------------------

.macro ISR_NO_ERROR vector
.global isr_stub_\vector
.type isr_stub_\vector, @function
isr_stub_\vector:
    pushq $0
    pushq $\vector
    jmp isr_common
.endm

.macro ISR_ERROR vector
.global isr_stub_\vector
.type isr_stub_\vector, @function
isr_stub_\vector:
    pushq $\vector
    jmp isr_common
.endm

# ------------------------------------------------------------
# Exceptions with CPU-pushed error codes
# ------------------------------------------------------------

ISR_ERROR    8
ISR_ERROR   10
ISR_ERROR   11
ISR_ERROR   12
ISR_ERROR   13
ISR_ERROR   14
ISR_ERROR   17
ISR_ERROR   21
ISR_ERROR   29
ISR_ERROR   30


# ------------------------------------------------------------
# Exceptions without CPU-pushed error codes
# ------------------------------------------------------------

ISR_NO_ERROR  0
ISR_NO_ERROR  1
ISR_NO_ERROR  2
ISR_NO_ERROR  3
ISR_NO_ERROR  4
ISR_NO_ERROR  5
ISR_NO_ERROR  6
ISR_NO_ERROR  7
ISR_NO_ERROR  9
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_NO_ERROR 31


# ------------------------------------------------------------
# Common ISR handler
# ------------------------------------------------------------

.global isr_common
.type isr_common, @function
isr_common:
    cld

    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rbx

    andq $-16, %rsp

    movq %rbx, %rdi
    call isr_handler

    movq %rbx, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp
    iretq


# ------------------------------------------------------------
# ISR stub address table
# ------------------------------------------------------------

.section .rodata

.global isr_stub_table
.type isr_stub_table, @object
isr_stub_table:
    .quad isr_stub_0
    .quad isr_stub_1
    .quad isr_stub_2
    .quad isr_stub_3
    .quad isr_stub_4
    .quad isr_stub_5
    .quad isr_stub_6
    .quad isr_stub_7
    .quad isr_stub_8
    .quad isr_stub_9
    .quad isr_stub_10
    .quad isr_stub_11
    .quad isr_stub_12
    .quad isr_stub_13
    .quad isr_stub_14
    .quad isr_stub_15
    .quad isr_stub_16
    .quad isr_stub_17
    .quad isr_stub_18
    .quad isr_stub_19
    .quad isr_stub_20
    .quad isr_stub_21
    .quad isr_stub_22
    .quad isr_stub_23
    .quad isr_stub_24
    .quad isr_stub_25
    .quad isr_stub_26
    .quad isr_stub_27
    .quad isr_stub_28
    .quad isr_stub_29
    .quad isr_stub_30
    .quad isr_stub_31

.size isr_stub_table, .-isr_stub_table