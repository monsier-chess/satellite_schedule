start: build
	
build:
	gcc main.cc -Ior-tools/include -lstdc++ or-tools/lib/libortools.so 
run:
	LD_LIBRARY_PATH=or-tools/lib ./a.out
