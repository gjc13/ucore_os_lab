.include "defines.h"
.data
hello:
	.string "hello world\n"

.globl	_main
_main:
	movl	$SYS_write,%eax
	movl	$STDOUT,%ebx
	movl	$hello,%ecx
	movl	$12,%edx
	int	$0x80

	ret
