	.global .umul
	.text
.umul:

	umul	%o0, %o1, %o0
	retl
	 rd	%y, %o1

