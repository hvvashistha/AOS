compile:
	g++ -std=c++11 -pthread process.cpp -o process

run_case_1:
	./process 0 0 > berkley_total_order_1.log &
	./process 0 0 > berkley_total_order_2.log &
	./process 0 0 master > berkley_total_order_3.log

run_case_2:
	./process 0 1 > berkley_total_order_1.log &
	./process 0 1 > berkley_total_order_2.log &
	./process 0 1 master > berkley_total_order_3.log

run_case_3:
	./process 1 > berkley_mututal_exclusion_1.log &
	./process 1 > berkley_mututal_exclusion_2.log &
	./process 1 master > berkley_mututal_exclusion_3.log

clean:
	rm -rf *.o
	rm -rf *.log