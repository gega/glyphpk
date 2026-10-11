#

glyphpk:	glyphpk.c glyphpk.h
		gcc glyphpk.c -o glyphpk

glyphpk_test:	glyphpk.h glyphpk_test.c
		gcc -o glyphpk_test glyphpk_test.c
