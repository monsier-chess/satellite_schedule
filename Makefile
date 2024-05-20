start: build
	
build:
	gcc main.cc -Ior-tools/include -O3 -lm -std=c++17 -lstdc++ or-tools/lib/libortools.so 
run:
	LD_LIBRARY_PATH=or-tools/lib ./a.out
tiny:
	LD_LIBRARY_PATH=or-tools/lib ./a.out --days 12 --slots 10 --a_days 2 --a_views 1 --b_days 3 --b_views 2 --ratio 2 --objects 10 --matrix data/tiny_limits.txt --types data/tiny_objtype.txt
big:
	LD_LIBRARY_PATH=or-tools/lib ./a.out --days 180 --slots 90 --a_days 2 --a_views 2 --b_days 4 --b_views 4 --ratio 4 --objects 361  --matrix data/limits.txt --types data/objtype.txt