start: build
	
build:
	gcc main.cc -Ior-tools/include -std=c++17 -O3 -lstdc++ or-tools/lib/libortools.so 
run:
	LD_LIBRARY_PATH=or-tools/lib ./a.out
