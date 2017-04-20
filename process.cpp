#include <iostream>
#include <sstream>
#include "protocols.h"
#include "communicator.hpp"
#include "lamport.hpp"
#include <string>
#include <unistd.h>

using namespace std;

int main(int argc, char const *argv[])
{	
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
	Communicator comm(master, &lClock, true);
	
	comm.berkleySync();
	cout << endl << endl << "----Multicast total ordered----" << endl << endl;

	comm.setAutoMark(true);

	sleep(10);

	comm.setAutoMark(false);

	struct timespec tv;
	tv.tv_sec = 1;
	tv.tv_nsec = 0;
	CommObj mComm;
	mComm.setValues('T', comm.getFPID(), "", comm.getCastAddr(false));

	while (comm.recv(comm.getSocket(true), mComm, &tv)) {
		cout << mComm.getEncoded() << endl;
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