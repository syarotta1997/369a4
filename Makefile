all : ext2_mkdir

ext2_mkdir : ext2_mkdir.o helper.o 
	gcc -Wall -g -o ext2_mkdir ext2_mkdir.o helper.o
%.o: %.c
	gcc -c -Wall -g -o $@ $<

clean:
	rm -f *.o ext2_mkdir
