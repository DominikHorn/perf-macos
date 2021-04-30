all:
	make test
	make run 
test: *.hpp *.cpp
	clang++ -std=c++20 -O2 -fno-tree-vectorize -o test test.cpp -Wall -Wextra
preprocess: *.hpp
	clang++ -std=c++20 -E perf-macos.hpp
run:
	sudo ./test
clean:
	rm -rf test
