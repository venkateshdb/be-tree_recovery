
ifdef D
   CXXFLAGS=-Wall -std=c++17 -g -pg -DDEBUG
   # CXXFLAGS=-Wall -std=c++17 -g -O3 
else
   CXXFLAGS=-Wall -std=c++17 -g -O3 
   # CXXFLAGS=-Wall -std=c++17 -g -pg -DDEBUG
endif



#CXXFLAGS=-Wall -std=c++11 -g -pg

CC=g++

all: test test_logging_restore generate

test: test.cpp betree.hpp recovery.cpp swap_space.o backing_store.o

test_logging_restore: test_logging_restore.cpp betree.hpp recovery.cpp swap_space.o backing_store.o

generate: generate.cpp

swap_space.o: swap_space.cpp swap_space.hpp backing_store.hpp

backing_store.o: backing_store.hpp backing_store.cpp

clean:
	$(RM) *.o test test_logging_restore generate tmpdir/*
