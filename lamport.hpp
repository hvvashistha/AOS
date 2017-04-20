#ifndef __LAMPORT__

#include <unordered_map>
#include <iostream>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <error.h>

#define __LAMPORT__

using namespace std;

class Clock {
private:
	pthread_mutex_t mLock;
	long int eventSequece;

public:
	Clock() {
		srand(time(NULL));
		eventSequece = rand() % 100;
		int ret;
		if((ret = pthread_mutex_init(&mLock, NULL)) != 0) {
			string s = "Mutex Init Error: ";
			s.append(strerror(ret));
			throw s;
		}
	}

	long int getClock() {
		return eventSequece;
	}

	void setClock(long int newClock) {
		eventSequece = newClock;
	}

	long int markEvent() {
		long int seq;
		pthread_mutex_lock(&mLock);
		seq = ++eventSequece;
		pthread_mutex_unlock(&mLock);
		return seq;
	}
};


#endif