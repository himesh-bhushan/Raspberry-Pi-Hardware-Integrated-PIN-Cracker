@ Task: compute the Hamming distance on 2 words
@       Given:  2 integer arrays xs and ys, where len(xs)==len(ys)
@       Return: the number of positions where the two arrays differ
@               i.e. | { i | i <- 0..len(xs), xs[i]!=ys[i] } |
	
@ Follows ARM subroutine calling conventions
	
	@ Entry point (Callable from C):
 .text
.align 2
.global hamming
.type hamming, %function

hamming:
    @ Input: R0 = address of x, R1 = address of y, R2 = length n
    @ save callee-saved registers (save registers that are going to be used again that have value)
    PUSH {r4, r5, lr}

    @ counter for Hamming distance
    MOV r3, #0

loop:
    @ check if length is 0
    CMP r2, #0
    BEQ end

    @ load the current integer from each array (4 byte increase)
    LDR r4, [r0], #4
    LDR r5, [r1], #4

    @ compare the values if the values are not equal
    @ conditional instruction (like ADDNE) to increment R3
    CMP r4, r5
    ADDNE r3, r3, #1

    @ decrease length to loop
    SUBS r2, r2, #1
    BNE loop

end:
    @ move the result into R0 (the return value)
    MOV r0, r3

    @ restore registers and return
    POP {r4, r5, pc}

    
@ Test data	
.data
.equ VAL1, 1
.equ VAL2, 2	

@ Indicate to the linker that the code in this file does not need the stack
@ to be executable. (Recent versions of GNU ld warn if this is not present.)
.section .note.GNU-stack,"",%progbits
	
