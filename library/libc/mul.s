	.global .mul
	.text
.mul:

	smul	%o0, %o1, %o0
	retl
	 rd	%y, %o1

