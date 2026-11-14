.syntax unified
.cpu cortex-m4
.thumb
.global Reset_Handler
.section .isr_vector,"a",%progbits
.word _estack
.word Reset_Handler
.rept 13
.word Default_Handler
.endr
.word SysTick_Handler
.rept 38
.word Default_Handler
.endr
.word USART2_IRQHandler
.rept 43
.word Default_Handler
.endr

.section .text.Reset_Handler,"ax",%progbits
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
    bl SystemInit
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:
    ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
3:
    cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:
    bl main
    b Default_Handler
.size Reset_Handler,.-Reset_Handler

.section .text.Default_Handler,"ax",%progbits
.type Default_Handler,%function
.thumb_func
Default_Handler:
    b .
.size Default_Handler,.-Default_Handler
.section .note.GNU-stack,"",%progbits
