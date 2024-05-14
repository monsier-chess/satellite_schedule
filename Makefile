start: build
	
build:
	gcc main.cc -Ior-tools/include -std=c++17 -O3 -lstdc++ or-tools/lib/libortools.so 
run:
	LD_LIBRARY_PATH=or-tools/lib ./a.out
tiny:
	LD_LIBRARY_PATH=or-tools/lib ./a.out --days 6 --slots 10 --a_days 1 --a_views 3 --b_days 2 --b_views 2 --objects 10 --matrix data/tiny_limits.txt --types data/tiny_objtype.txt
big:
	LD_LIBRARY_PATH=or-tools/lib ./a.out --days 180 --slots 90 --a_days 2 --a_views 2 --b_days 4 --b_views 4 --objects 361  --matrix data/limits.txt --types data/objtype.txt