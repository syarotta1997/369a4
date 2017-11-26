all : ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore

ext2_mkdir : ext2_mkdir.o helper.o 
	gcc -Wall -g -o ext2_mkdir ext2_mkdir.o helper.o
	
ext2_cp : ext2_cp.o helper.o
	gcc -Wall -g -o ext2_cp ext2_cp.o helper.o
	
ext2_ln : ext2_ln.o helper.o
	gcc -Wall -g -o ext2_ln ext2_ln.o helper.o
	
ext2_rm : ext2_rm.o helper.o
	gcc -Wall -g -o ext2_rm ext2_rm.o helper.o
	
ext2_restore : ext2_restore.o helper.o
	gcc -Wall -g -o ext2_restore ext2_restore.o helper.o
	
#ext2_checker : ext2_checker.o helper.o
#	gcc -Wall -g -o ext2_checker ext2_checker.o helper.o
	
%.o: %.c
	gcc -c -Wall -g -o $@ $<
	
clean:
	rm -f *.o ext2_mkdir ext2_cp ext2_ln ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore
