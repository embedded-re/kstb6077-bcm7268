.syntax unified
.arm
.global _start

@ =============================================================================
@ CONSTANTS & CONFIGURATION
@ =============================================================================
.equ UART_BASE, 0xf040c000
.equ REG_THR,   0x00
.equ REG_RBR,   0x00
.equ REG_LSR,   0x14
.equ LSR_DR,    0x01
.equ LSR_THRE,  0x20

.text

@ =============================================================================
@ MAIN PROGRAM ENTRY
@ =============================================================================
_start:
    ldr     sp, =stack_top
    ldr     r4, =banner
    bl      print_string        @ was "print_banner_loop" before -- now reuses
                                  @ the same print_string every other part uses

@ -----------------------------------------------------------------------------
@' main_loop: the top-level flow, kept deliberately tiny.
@ Everything that will ever change (parsing, commands, new features) happens
@ INSIDE read_line/process_line, never here -- this loop itself should almost
@ never need editing again.'
@ -----------------------------------------------------------------------------
main_loop:
    bl      read_line           @ collects one full line into line_buffer
    bl      process_line        @ <-- THIS is your extension point, see below
    b       main_loop

@' -----------------------------------------------------------------------------
@ read_line: collects characters into line_buffer until Enter is pressed.
@ Null-terminates the result. Echoes each character as its typed.
@ No bounds checking yet -- still an open gap, not forgotten, just deferred.
@ Clobbers: r0. Preserves: everything else (saves r5, lr itself).'
@ -----------------------------------------------------------------------------
read_line:
    push    {r5, lr}
    ldr     r5, =line_buffer     @ r5 = "next empty slot in the buffer"

.Lread_char:
    bl      uart_getc            @ r0 = one byte received
    bl      uart_putc            @ echo it back so the user sees what they typed

    cmp     r0, #0x0D             @ was it Enter?
    beq     .Lread_done

    strb    r0, [r5], #1          @ store the character, advance the pointer
    b       .Lread_char

.Lread_done:
    mov     r0, #0
    strb    r0, [r5]              @ null-terminate the collected line

    mov     r0, #0x0A
    bl      uart_putc              @ move to a fresh line before whatever comes next

    pop     {r5, lr}
    bx      lr

@ -----------------------------------------------------------------------------
@ 'process_line: THIS IS WHERE YOU BUILD EVERYTHING NEXT.
@
@ Right now it just proves the line was collected correctly by printing it
@ back. Replace/extend this function -- and only this one -- as you move
@ through the roadmap:
@   Stage 2 (numeric parsing): check if line_buffer starts with hex digits,
@     convert them into a real number in a register.
@   Stage 3 (monitor commands): check the first character/word against
@     known commands ('d', 'e', 'g'...) and branch to a handler for each.
@   Stage 4 (real tokenization): split line_buffer into separate words
@     before deciding what to do with them.
@ None of that touches read_line or main_loop at all -- thats the point
@ of splitting it out like this.'
@ -----------------------------------------------------------------------------
process_line:
    push    {r4, lr}

    ldr     r4, =you_typed_message
    bl      print_string
    ldr     r4, =line_buffer
    bl      print_string
    mov     r0, #0x0A
    bl      uart_putc

    pop     {r4, lr}
    bx      lr

@ =============================================================================
@ SHARED HELPERS -- stable, you likely won't need to touch these again'
@ =============================================================================

@ --- print_string: prints bytes starting at r4 until a 0 byte ---
@ Clobbers: r0, r4.
print_string:
    ldrb    r0, [r4], #1
    cmp     r0, #0
    beq     print_string_done
    bl      uart_putc
    b       print_string
print_string_done:
    bx      lr

@ --- uart_getc: blocking read of one byte. Returns it in r0. ---
uart_getc:
    ldr     r1, =UART_BASE
wait_for_rx_ready:
    ldr     r2, [r1, #REG_LSR]
    tst     r2, #LSR_DR
    beq     wait_for_rx_ready
    ldr     r0, [r1, #REG_RBR]
    bx      lr

@ --- uart_putc: blocking write of one byte from r0. Preserves r0. ---
uart_putc:
    push    {r1, r2, lr}
    ldr     r1, =UART_BASE
wait_for_tx_ready:
    ldr     r2, [r1, #REG_LSR]
    tst     r2, #LSR_THRE
    beq     wait_for_tx_ready
    str     r0, [r1, #REG_THR]
    pop     {r1, r2, lr}
    bx      lr

@ =============================================================================
@ DATA SECTIONS
@ =============================================================================
.section .rodata
banner:
    .asciz "\r\n[TEST....................].\r\n"
you_typed_message:
    .asciz "you typed: "

.section .bss
.align 4
line_buffer:
    .space 64
stack_bottom:
    .space 0x4000
stack_top:
