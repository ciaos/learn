#!/bin/bash

# libevent : libevent-devel-2.0.21-4.el7.x86_64
# boost : boost-devel-1.53.0-27.el7.x86_64

g++ server.cpp -std=c++11 -lboost_thread -lboost_chrono -levent
