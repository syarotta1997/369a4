all : ext2_mkdir

ext2_mkdir : ext2_mkdir.c ext2.h
	gcc -Wall -g -o ext2_mkdir ext2_mkdir.c
