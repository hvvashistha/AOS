## To Compile:

    make compile

The program includes 3 cases and does Berkley synchronization in every case.
Following case by case commands will generate log files for 3 simultaneous processes.

## Case 1: Total Ordering (Only print totally ordered messages).
To Execute:

    make run_case_1

## Case 2: Total Ordering (Also prints the original received order after printing totally ordered receive).
To execute:

    make run_case_2

## Case 3: Mutual exclusion file counter increment.
To Execute:

    make run_case_3


## Direct run (Always run master process last, other process will wait for master):
```
./process <Algo? 0:Total Order, 1:Mutual Exclusion> [For total order, Also print original receive order? 0/1] ["master"]
```

