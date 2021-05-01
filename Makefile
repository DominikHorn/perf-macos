all:
	make compile run 
compile: *.hpp *.cpp
	clang++ -std=c++20 -O2 -fno-tree-vectorize -o test test.cpp -Wall -Wextra
debug: *.hpp *.cpp
	clang++ -std=c++20 -O0 -g -fno-tree-vectorize -o test-debug test.cpp -Wall -Wextra
	sudo lldb ./test-debug
run:
	sudo ./test
clean:
	rm -rf test
