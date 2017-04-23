#include <iostream>
#include <sstream>
#include "protocols.h"
#include "communicator.hpp"
#include "lamport.hpp"
#include <string>
#include <unistd.h>
#include "exception.hpp"
#include <fstream>

using namespace std;

int main(int argc, char const *argv[]) {
	bool master = false;
	int i=1, algo = atoi(argv[1]);

	if (argc < 2 || ((algo = atoi(argv[1])) == 0 && argc < 3)) {
		cout << "Usage: ./process <0: Total Ordered Receive, 1: mutual exclusion> [Print non ordered receives? 0/1] [master]" << endl;
		return 1;
	}

	if (strcmp(argv[argc - 1], "master") == 0) {
		master = true;
		cout << "master" << endl;
	}

	//Socket timeout
	struct timespec tv = (struct timespec) {.tv_sec = 1, .tv_nsec = 0};
	
	CommObj mComm;

	try {
		//Initiate clock
		Clock lClock;

		//Intitate Communicator
		Communicator comm(master, &lClock, true);
		
		//Synchronize clocks
		comm.berkleySync();

		if (algo == 0) {
			cout << endl << endl << "----Multicast total ordered----" << endl << endl;
			sleep(2);

			//Initiate Multicast, Launches Sender and Receiver threads
			comm.setAutoMark();

			mComm.setValues('T', comm.getFPID(), "", comm.getCastAddr(false));

			//Read Received totally ordered messages
			while (comm.recv(comm.getSocket(true), &mComm, false, &tv)) {
				cout << i++ << ": " << mComm.getEncoded() << endl;
			}
			//Finish off multicast, does thread join
			comm.multicastFinish();

			if (atoi(argv[2]) == 1) {
				cout << endl << endl << "----Multicast Raw receives----" << endl;
				while (!comm.rawQueue.empty()) {
					cout << i++ << ": " << comm.rawQueue.front().getEncoded() << endl;
					comm.rawQueue.pop();
				}
			}
		} else if (algo == 1) {

			cout << endl << endl << "----Mutual Exclusion (Token Ring)----" << endl << endl;
			sleep(2);

			//Master has the initial token
			bool haveToken = comm.isMaster();
			int fCounter, noOfUpdates = 5;
			const string filename = "token.txt";
			vector<long int> sortedPids;
			tv.tv_sec = 5;
			
			//Get sorted process Ids for token ring
			for (auto commPair: comm.getMemberList()) { sortedPids.push_back(commPair.first); }
			sort(sortedPids.begin(), sortedPids.end());
			auto nextPid = find(sortedPids.begin(), sortedPids.end(), comm.getpid());

			//Find out next process
			if (++nextPid == sortedPids.end()) nextPid = sortedPids.begin();
			
			mComm.setValues('T', comm.getFPID(), "TOKEN", comm.getPointAddr(*nextPid));

			do {
				//If process has token, update counter in file or wait fpr the token
				if (haveToken || comm.recv(comm.getSocket(false), mComm, &tv)) {
					haveToken = false;
					
					ifstream inFile(filename, ios::in);
					inFile >> fCounter;
					inFile.close();
					
					cout << "Counter value read: " << fCounter << endl;
					
					ofstream outFile(filename, ios::out | ios::trunc);
					outFile << (++fCounter);
					outFile.close();

					//Pass on the token to next process
					mComm.addr = comm.getPointAddr(*nextPid);
					comm.send(comm.getSocket(false), mComm);
				}
			} while(noOfUpdates--);
		}

	} catch (COMMException& e) {
		cerr << e.what() << endl;
		exit(1);
	}


	cout << endl << "Fin." << endl;

	return 0;
}