#! /bin/bash
command  g++ *.cpp -pthread -o runtest
./runtest 10000  # 5000 is the port num



#192.168.163.128:10000/index.html