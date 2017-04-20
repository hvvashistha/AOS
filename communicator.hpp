#ifndef __COMMUNICATOR__

#include <iostream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include "protocols.h"
#include "lamport.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <unistd.h>
#include <unordered_set>
#include <unordered_map>
#include <random>
#include <algorithm>
#include <pthread.h>
#include <queue>

#define __COMMUNICATOR__
#define HELLO_PORT 12345
#define HELLO_GROUP "225.0.0.37"

using namespace std;
typedef struct sockaddr_in SockAddr;

string itos(long int num) {
	ostringstream oss;
	oss << num;
	return oss.str();
}

string dtos(double num) {
	ostringstream oss;
	oss << num;
	return oss.str();
}

long int randomize() {
	uniform_int_distribution<int> pidDist(0, 99999999);
	random_device rd1;
	return pidDist(rd1);
}

// double stod (string s) {
// 	try {
// 		return stod(s);
// 	} catch (exception& e) {
// 		cerr << "Rogue String (" << s << ")";
// 		throw e;
// 	}
// }

void* distribute(void* arg);

class CommObj {
public:
	char control;
	SockAddr* addr;
	string pid;
	string msg;
	bool sentAck;
	string seqNum;

	CommObj() {
		sentAck = false;
		seqNum = "0";
		addr = NULL;
	}

	CommObj* setValues(char control, string pid, string msg, SockAddr* addr) {
		this->control = control;
		this->pid = pid;
		this->msg = msg;
		this->addr = addr;
		return this;
	}

	CommObj* set(const CommObj other) {
		control = other.control;
		addr = other.addr;
		pid = other.pid;
		msg = other.msg;
		sentAck = other.sentAck;
		seqNum = other.seqNum;
		return this;
	}

	CommObj* decode(const char *msg, SockAddr* addr) {
		istringstream iss(msg);
		char buffer[1024];
		iss >> seqNum >> control >> pid >> std::ws;
		iss.get(buffer, 1024);
		this->msg = buffer;
		this->addr = addr;
		try {
			stod(seqNum);
		} catch (exception& e) {
			cerr << "ROGUE stream" << endl;
			cerr << "Original message : " << msg << endl;
			cerr << "Decoded: <" << seqNum << ", " << control << ", " << pid << ", " << this->msg << ", " << this->addr << endl;
		}

		return this;
	}

	CommObj* markSeqNum(long int seq) {
		ostringstream oss;
		oss << setfill('0') << setw(15) << (itos(seq) + "." + pid);
		seqNum = oss.str();
		return this;
	}

	string getEncoded() const {
		ostringstream oss;
		oss << seqNum << " " << control << " " << pid << " " << msg;
		return oss.str();
	}

	string getStringSeq() const {
		return seqNum;
	}

	bool operator<(const CommObj& other) const {
		return stod(seqNum) > stod(other.seqNum);
	}
};

class Communicator {
private:
	unordered_map<int, SockAddr*> memberList;
	int mSocket, pSocket;
	SockAddr mOutAddr, mInAddr;
	long int master;
	long int pid;
	bool autoMarkSeq = false;
	string fixedPID;
	unordered_map <long int, int> processAlias;
	priority_queue<CommObj> unstableQueue;
	queue<CommObj> stableQueue;
	pthread_t receiver, sender;
	pthread_attr_t rattr, sattr;
	pthread_mutex_t queueLock;
	pthread_cond_t cond_wait;

	Clock* lClock;

	void initMultiCast();
	void initPointToPoint();
	void initiateGroupComm();
	static void* setOrderedRecv(void* arg);
	static void* contSend(void* arg);
	CommObj* recv(int sock, CommObj& comm, bool skipQueue, struct timespec *tv);


public:
	Communicator(bool master, Clock* lClock, bool setupGroup) {
		this->lClock = lClock;
		pthread_mutex_init(&queueLock, NULL);
		pthread_cond_init(&cond_wait, NULL);
		initMultiCast();
		initPointToPoint();

		SockAddr sin;
		u_int len = sizeof(sin);
		if (getsockname(pSocket, (struct sockaddr *)&sin, &len) == -1) {
			perror("getsockname");
		}

		ostringstream* oss = new ostringstream;
		*oss << randomize() << sin.sin_port;
		fixedPID = oss->str();
		delete oss;
		oss = new ostringstream;
		*oss << setfill('0') << setw(15) << fixedPID;
		fixedPID = oss->str();
		delete oss;
		pid = stol(fixedPID);
		cout << "PID: " << pid << endl;

		if (master) {
			this->master = pid;
		}

		if (setupGroup) {
			initiateGroupComm();
		}
	}

	bool isMaster() {
		return master == pid;
	}

	void setAutoMark(bool mark) {

		if (mark && !autoMarkSeq) {
			cout << "Ordered multicast initiated" << endl;
			autoMarkSeq = mark;
			pthread_attr_init(&rattr);
			pthread_attr_init(&sattr);
			pthread_create(&receiver, &rattr, Communicator::setOrderedRecv, this);
			pthread_create(&sender, &sattr, Communicator::contSend, this);
		} else if (!mark && autoMarkSeq) {
			cout << "Terminating Ordered multicast" << endl;
			CommObj mComm;
			autoMarkSeq = mark;
			mComm.setValues('X', getFPID(), "Terminate", this->getCastAddr(true));
			mComm.markSeqNum(lClock->markEvent());
			this->send(this->getSocket(true), mComm);
			this->unstableQueue.push(mComm);
			mComm.control = 'A';
			this->send(this->getSocket(true), mComm);
			pthread_join(sender, NULL);
			pthread_join(receiver, NULL);
		}
	}

	int getMaster() {
		return master;
	}

	int getpid() {
		return pid;
	}

	int getRank() {
		return processAlias.at(pid);
	}

	int getRank(long int pID) {
		return processAlias.at(pID);
	}

	int getGroupSize() {
		return memberList.size();
	}

	CommObj getCommDef() {
		CommObj mComm;
		string temp = "";
		mComm.setValues('D', fixedPID, temp, &mOutAddr);
		return mComm;
	}

	unordered_map<int, SockAddr*> getMemberList() {
		return memberList;
	}

	SockAddr* getPointAddr(int member) {
		return memberList.at(member);
	}

	SockAddr* getCastAddr(bool outBound) {
		return (outBound ? &mOutAddr : &mInAddr);
	}

	int getSocket(bool mCast) {
		return mCast ? mSocket : pSocket;
	}

	void send(int sock, CommObj& comm);

	CommObj* recv(int sock, CommObj& comm);

	CommObj* orderedRecv();

	string getFPID() {
		return fixedPID;
	}

	static CommObj* decodeGroupCast(istringstream& iss, CommObj &mComm) {
		mComm.addr = new SockAddr;
		mComm.addr->sin_family = AF_INET;
		iss >> mComm.pid >> mComm.addr->sin_addr.s_addr >> mComm.addr->sin_port;
		return &mComm;
	}

	static SockAddr* decodeSock(string sock) {
		SockAddr* addr = new SockAddr;
		istringstream iss(sock);
		addr->sin_family = AF_INET;
		iss >> addr->sin_addr.s_addr >> addr->sin_port;
		return addr;
	}

	void berkleySync();
};

void* distribute(void* arg) {
	Communicator *self = (Communicator *) arg;
	CommObj mComm;

	cout << "distributer set" << endl;
	return NULL;
}

void Communicator::send(int sock, CommObj& comm) {
	char buffer[1024];
	string encoded = comm.getEncoded();
	uSend(sock, buffer, mFormat(encoded.c_str(), encoded.length(), buffer), (struct sockaddr *) (comm.addr));
}

CommObj* Communicator::recv(int sock, CommObj& comm, bool skipQueue, struct timespec* tv) {
	SockAddr* addr = NULL;
	char buffer[1024];
	if (sock == mSocket) {
		addr = comm.addr;
	}

	if (!skipQueue && sock == mSocket && (autoMarkSeq || !stableQueue.empty())) {
		cout << "stable queue lock" << endl;
		pthread_mutex_lock(&queueLock);
		if (stableQueue.empty()) {
			pthread_cond_wait(&cond_wait, &queueLock);
		}
		comm.set(stableQueue.front());
		stableQueue.pop();
		pthread_mutex_unlock(&queueLock);
		cout << "stable queue unlock" << endl;
		return &comm;
	} else {
		bool ret = true;
		if (tv) {
			fd_set sockFDs;
			FD_ZERO(&sockFDs);
			FD_SET(sock ,&sockFDs);
			ret = pselect(sock + 1, &sockFDs, NULL, NULL, tv, NULL) > 0 && FD_ISSET(sock, &sockFDs);
		}
		 if (ret) {
		 	if(uRecv(sock, buffer, (struct sockaddr *) addr)) {
				comm.markSeqNum(lClock->getClock());
				return comm.decode(buffer, addr);
			} else {
				return NULL;
			}
		} else {
			return NULL;
		}
	}
}

CommObj* Communicator::recv(int sock, CommObj& comm) {
	return recv(sock, comm, false, NULL);
}

void* Communicator::setOrderedRecv(void* arg) {
	Communicator *self = (Communicator*) arg;
	int requiredAcks = self->getGroupSize();
	struct timespec tv;
	tv.tv_sec = 0;
	tv.tv_nsec = 500000000;
	unordered_map<string, pair<bool, int>> msgs;
	CommObj aComm, *mComm;
	string lastStableInsert = "0000000000000.0";
	pair<string, int> dropRequests = pair<string, int>("", 0);
	const int maxRequestDelay = 10;

	cout << "Receiving thread setup" << endl;

	do {
		mComm = new CommObj;
		mComm -> setValues('M', self->fixedPID, self->fixedPID, self->getCastAddr(false));

		// if (!self->autoMarkSeq) {
		// 	if (!self->unstableQueue.empty()) {
		// 		aComm.set(self->unstableQueue.top());
		// 		string counter = "-,-";

		// 		bool ackSent = false;
		// 		if (msgs.count(aComm.seqNum) > 0) {
		// 			ackSent = msgs.at(aComm.seqNum).first;
		// 			counter = itos(ackSent) + ", " + itos(msgs.at(aComm.seqNum).second);
		// 		} else {
		// 			msgs.insert(pair<string, pair<bool, int>>(aComm.seqNum, pair<bool, int>(false, 0)));
		// 		}
		// 		cout << "Queue Not Empty [" << aComm.seqNum << "], <" << counter << ">" << aComm.msg << endl;
				
		// 		if (!ackSent) {
		// 			aComm.control = 'A';
		// 			self->send(self->getSocket(true), aComm);
		// 			msgs.at(aComm.seqNum).first = true;
		// 			cout << "TOP ACK: [" << aComm.getStringSeq() << "] " << aComm.control << " " << aComm.pid << " " << aComm.msg << " <1, " << itos(msgs.at(aComm.seqNum).second) << ">" << endl;
				
		// 		}

		// 		if (aComm.control == 'X' && msgs.at(aComm.seqNum).second >= requiredAcks) {
		// 			break;
		// 		}	
		// 	}
		// }

		if (self->recv(self->getSocket(true), *mComm, true, &tv)) {
			cout << "REC: [" << mComm->getStringSeq() << "] " << mComm->control << " " << mComm->pid << " " << mComm->msg << " | ";

			if (mComm->control == 'A') {
				if (msgs.count(mComm->seqNum) > 0){
					msgs.at(mComm->seqNum).second = msgs.at(mComm->seqNum).second + 1;
				} else {
					msgs.insert(pair<string, pair<bool, int>>(mComm->seqNum, pair<bool, int>((mComm->pid == self->getFPID()), 1)));
				}
				cout << "ack <" << itos(msgs.at(mComm->seqNum).first) << ", " << itos(msgs.at(mComm->seqNum).second) << ">" << " ";
				delete mComm;
			} else if ((mComm->control == 'M' || mComm->control == 'X') && mComm->pid != self->getFPID()) {
				self->lClock->markEvent();
				self->unstableQueue.push(*mComm);
				msgs.insert(pair<string, pair<bool, int>>(mComm->seqNum, pair<bool, int>(false, 0)));
				if (mComm->control == 'X') {
					self->setAutoMark(false);
				}
			} else if (mComm->control == 'R' && mComm->seqNum <= lastStableInsert) {
				if (!self->unstableQueue.empty()){
					aComm.set(self->unstableQueue.top());
					cout << "TOP: [" << aComm.getStringSeq() << "] " << aComm.control << " " << aComm.pid << " " << aComm.msg << " | ";
				} else {
					cout << "TOP: EMPTY | ";
				}
				aComm.set(*mComm);
				aComm.control = 'A';
				aComm.pid = self->getFPID();
				aComm.addr = self->getCastAddr(true);
				self->send(self->getSocket(true), aComm);
				delete mComm;
			} else if (mComm->pid != self->getFPID()){
				cout << "ROGUE CAPTURE ";
				delete mComm;
			}
		} else {
			delete mComm;
		}

		pthread_mutex_lock(&self->queueLock);
		if (!self->unstableQueue.empty()) {
			aComm.set(self->unstableQueue.top());

			//Create a counter for message if doesn't exist
			if (msgs.count(aComm.seqNum) == 0) {
				msgs.insert(pair<string, pair<bool, int>>(aComm.seqNum, pair<bool, int>(false, 0)));
			}

			//Send Ack if not done so
			if (!msgs.at(aComm.seqNum).first) {
				aComm.control = 'A';
				aComm.pid = self->getFPID();
				aComm.addr = self->getCastAddr(true);
				self->send(self->getSocket(true), aComm);
				msgs.at(aComm.seqNum).first = true;
				cout << "TOP ACK: [" << aComm.getStringSeq() << "] " << aComm.control << " " << aComm.pid << " " << aComm.msg;
				cout << " ack <1, " << itos(msgs.at(aComm.seqNum).second) << ">" << endl;
			}
			
			//Add to stable Queue if all acks are received
			if (msgs.at(aComm.seqNum).second >= requiredAcks) {
				if (aComm.control != 'X') {
					cout << endl << "Marking STABLE: [" << aComm.seqNum << "] " << aComm.msg;
					self->stableQueue.push(self->unstableQueue.top());
					self->unstableQueue.pop();
					lastStableInsert = aComm.seqNum;
					pthread_cond_signal(&self->cond_wait);
				} else {
					while (!self->unstableQueue.empty()) {
						self->unstableQueue.pop();
					}
				}
			} else if(dropRequests.first == aComm.seqNum) {
				if (++dropRequests.second >= maxRequestDelay) {
					aComm.control = 'R';
					aComm.pid = self->getFPID();
					aComm.addr = self->getCastAddr(true);
					msgs.at(aComm.seqNum).first = false;
					msgs.at(aComm.seqNum).second = 0;
					self->lClock->markEvent();
					self->send(self->getSocket(true), aComm);
					dropRequests.second = 0;
					cout << "REQUESTING: [" << aComm.getStringSeq() << "] " << aComm.control << " " << aComm.pid << " " << aComm.msg << endl;
				}
			} else {
				dropRequests.first = aComm.seqNum;
				dropRequests.second = 0;
			}
		}
		pthread_mutex_unlock(&self->queueLock);

		cout << endl << endl;
	} while (self->autoMarkSeq || !self->unstableQueue.empty());

	return NULL;
}

void* Communicator::contSend(void* arg) {
	Communicator *self = (Communicator*) arg;
	string temp;
	CommObj aComm, *mComm;
	u_int counter = 1, timer = 500000;
	cout << "sending thread setup" << endl;
	while (self->autoMarkSeq) {
		temp = "Random Message " + itos(counter++);
		mComm = new CommObj;
		mComm->setValues('M', self->getFPID(), temp, self->getCastAddr(true));
		mComm->markSeqNum(self->lClock->markEvent());
		cout << "Send: [" << mComm->seqNum << "] " << mComm->msg << endl;
		self->send(self->getSocket(true), *mComm);

		self->unstableQueue.push(*mComm);
		//Immediately send ACK
		aComm.set(*mComm);
		aComm.control = 'A';
		self->send(self->getSocket(true), aComm);

		usleep(timer);
	}

	return NULL;
}

void Communicator::initMultiCast() {
	//Multicast socket
	u_int yes = 1;
	struct ip_mreq mreq;

	//Setup outgoing multicast address
	mOutAddr.sin_family = AF_INET;
	mOutAddr.sin_addr.s_addr = inet_addr(HELLO_GROUP);
	mOutAddr.sin_port = htons(HELLO_PORT);

	//Setup incoming multicast address
	mInAddr.sin_family = AF_INET;
	mInAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	mInAddr.sin_port = htons(HELLO_PORT);

	if ((mSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		std::cerr << "socket" << std::endl;
		exit(1);
	}

	if (setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		std::cerr << "Reusing ADDR failed" << std::endl;
		exit(1);
	}

	//Bind for multicast receive
	if (bind(mSocket, (struct sockaddr *) &mInAddr, sizeof(mInAddr)) < 0) {
		std::cerr << "bind";
		exit(1);
	}

	//Add process to multicast group
	mreq.imr_multiaddr.s_addr = inet_addr(HELLO_GROUP);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(mSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
		std::cerr << "setsockopt";
		exit(1);
	}
}

void Communicator::initPointToPoint() {
	u_int yes = 1;
	SockAddr pAddr;
	pAddr.sin_family = AF_INET;
	pAddr.sin_addr.s_addr = INADDR_ANY;
	pAddr.sin_port = INADDR_ANY;

	if ((pSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		std::cerr << "socket" << std::endl;
		exit(1);
	}

	if (setsockopt(pSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		std::cerr << "Reusing ADDR failed" << std::endl;
		exit(1);
	}

	//Bind for point to point receive
	if (bind(pSocket, (struct sockaddr *) &pAddr, sizeof(pAddr)) < 0) {
		std::cerr << "bind";
		exit(1);
	}
}

void Communicator::initiateGroupComm() {
	CommObj mComm;
	SockAddr* sin;
	u_int len = sizeof(sin);

	string temp = "";

	sin = new SockAddr;
	if (getsockname(pSocket, (struct sockaddr *)sin, &len) == -1) {
		perror("getsockname");
	}

	temp += itos(sin->sin_addr.s_addr) + " " + itos(sin->sin_port);
	delete sin;

	mComm.setValues(ENQ, fixedPID, temp, &mOutAddr);
	if (master == pid) {
		send(mSocket, mComm);
	}
	mComm.addr = &mInAddr;

	//Receiving from master
	if (recv(mSocket, mComm) && mComm.control == ENQ) {
		mComm.setValues(ENQ, fixedPID, temp, Communicator::decodeSock(mComm.msg));
		send(this->getSocket(false), mComm);
	}

	struct timeval tin;
	tin.tv_sec = 5;
	tin.tv_usec = 0;

	if (setsockopt(pSocket, SOL_SOCKET, SO_RCVTIMEO, &tin, sizeof(tin)) < 0) {
		std::cerr << "Set Timeout Error";
	}
	cout << "Discovering multicast group members, 5 seconds advertisement window" << endl;
	vector<long int> sortedPIDs;

	if (isMaster()) {
		temp = "";
		while (recv(this->getSocket(false), mComm) && mComm.control == ENQ) {
			temp += mComm.pid + " " + mComm.msg + " ";
		}
		mComm.setValues('L', fixedPID, temp, this->getCastAddr(true));
		send(this->getSocket(true), mComm);
	}
	mComm.addr = this->getCastAddr(false);
	//Receiving from master
	if (recv(this->getSocket(true), mComm) && mComm.control == 'L') {
		istringstream iss(mComm.msg);
		while (Communicator::decodeGroupCast(iss, mComm) && !iss.eof()) {
			long int iPID = stol(mComm.pid);
			if (memberList.count(iPID) > 0) {
				cerr << "Process Id " << iPID << " clashed, exiting!" << endl;
				exit(1);
			}
			memberList.insert(std::pair<int, SockAddr*>(iPID, mComm.addr));
			sortedPIDs.push_back(iPID);
		}
	}

	sort(sortedPIDs.begin(), sortedPIDs.end());
	master = sortedPIDs[0];

	cout << "New Leader: " << master << endl;
	cout << "Members [" << sortedPIDs.size() << "]: ";

	for (int i = 0; i < sortedPIDs.size(); i++) {
		cout << sortedPIDs[i] << "[" << i << "]  ";
		processAlias.insert(pair<long int, int>(sortedPIDs[i], i));
	}

	cout << endl;
}

void Communicator::berkleySync() {
	std::unordered_map <int, int> pClock;
	CommObj mComm = getCommDef();

	mComm.setValues('B', getFPID(), itos(lClock->getClock()), getCastAddr(true));

	cout << "Berkley Sync, clock value: " << lClock->getClock() << endl;

	//Startup Berkley Sync
	if (isMaster()) {
		cout << "Berkley Sync stage 1, Sending master clock" << endl;
		send(getSocket(true), mComm);
	}

	//Inbound cast address
	mComm.addr = getCastAddr(false);
	//Return deltas

	if (recv(getSocket(true), mComm) && mComm.control == 'B') {
		cout << "Berkley Sync stage 2, Sending Deltas back to master" << endl;
		int returnToProcess = stol(mComm.pid);
		int delta = stol(mComm.msg) - lClock->getClock();
		mComm.setValues('B', getFPID(), itos(delta), getPointAddr(returnToProcess));
		send(getSocket(false), mComm);
	}

	if (isMaster()) {
		cout << "Berkley Sync stage 3, Calculating average delta and sending back offsets" << endl;
		float averageDelta = 0.0, receivedDelta = 0;

		// Receive deltas
		// pClock.insert(pair<int, int>(getpid(), receivedDelta));
		cout << "Comm size: " << getGroupSize() << endl;
		for (int i = getGroupSize(); i > 0 && recv(getSocket(false), mComm) && mComm.control == 'B'; i--) {
			receivedDelta = stol(mComm.msg);
			cout << "Received Delta from " << mComm.pid << ": " << mComm.msg << endl;
			pClock.insert(pair<int, int>(stol(mComm.pid), receivedDelta));
			averageDelta += receivedDelta;
		}
		cout << endl;

		averageDelta /= (float)getGroupSize();
		cout << "Average Delta: " << averageDelta << endl;
		for (auto member : getMemberList()) {
			mComm.setValues('U', getFPID(), dtos(pClock.at(member.first) - averageDelta), getPointAddr(member.first));
			send(getSocket(false), mComm);
		}
	}

	//Receive Offset
	if (recv(getSocket(false), mComm) && mComm.control == 'U') {
		cout << "Berkley Sync stage 4, Adjusting offset: " << mComm.msg << endl;
		cout << "Original Clock Sequence: " << lClock->getClock() << endl;
		lClock->setClock((double)lClock->getClock() + stod(mComm.msg));
	}

	cout << "Clock (Synced): " << lClock->getClock() << endl;
}

#endif