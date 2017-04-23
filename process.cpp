#include <iostream>
#include <sstream>
#include "protocols.h"
#include "communicator.hpp"
#include "lamport.hpp"
#include <string>
#include <unistd.h>
#include "exception.hpp"

using namespace std;

int main(int argc, char const *argv[]) {
	bool master = false;
	if (argc < 2) {
		cout << "Usage: sender <message> [master]" << endl;
		return 1;
	}

	if (argc == 3 && strcmp(argv[2], "master") == 0) {
		master = true;
		cout << "master" << endl;
	}
	Clock lClock;
	try {
		Communicator comm(master, &lClock, true);

		comm.berkleySync();
		cout << endl << endl << "----Multicast total ordered----" << endl << endl;

		sleep(5);
		comm.setAutoMark();

		struct timespec tv;
		tv.tv_sec = 1;
		tv.tv_nsec = 0;
		CommObj mComm;
		mComm.setValues('T', comm.getFPID(), "", comm.getCastAddr(false));
		int i = 1;

		while (comm.recv(comm.getSocket(true), &mComm, false, &tv)) {
			cout << i++ << ": " << mComm.getEncoded() << endl;
		}
		comm.multicastFinish();

	} catch (COMMException& e) {
		cerr << e.what() << endl;
		exit(1);
	}

	cout << "Fin." << endl;

	// cout << endl << "--- MultiCast Test ---" << endl;

	// CommObj *sSomm, *rComm;

	// for (int i = 1; i <= 2; i++) {
	// 	sSomm = new CommObj();
	// 	rComm = new CommObj();

	// 	cout << endl << "Test " << i << endl;

	// 	sSomm->setValues('T', "1111", "Test", comm.getCastAddr(true));
	// 	if (comm.isMaster())
	// 		comm.send(comm.getSocket(true), *sSomm);

	// 	comm.recv(comm.getSocket(true), *rComm);
	// 	cout << rComm->getEncoded() << endl;
	// 	delete sSomm;
	// 	delete rComm;
	// }
	return 0;
}