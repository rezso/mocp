CC = gcc
CXX = g++
CFLAGS = -Wall -march=native --std=c++17 -O3 -fopenmp

ratings-find: 
	$(CXX) $(CFLAGS) ratings_find.cpp -o ratings_find
	strip ratings_find

clean:
	rm *.o ratings_find
