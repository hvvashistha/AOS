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
#include <algorithm>
#include "exception.hpp"

#define __COMMUNICATOR__
#define HELLO_PORT 12345
#define HELLO_GROUP "225.0.0.37"
#define DEBUG false

using namespace std;
typedef struct sockaddr_in SockAddr;

string itos(long int num) {
	ostringstream oss;
	oss << num;
	if (!oss.good()) throw COMMException("itos(): stream error");
	return oss.str();
}

string dtos(double num) {
	ostringstream oss;
	oss << num;
	if (!oss.good()) throw COMMException("dtos(): stream error");
	return oss.str();
}

long int randomize() {
	uniform_int_distribution<int> pidDist(0, 99999999);
	random_device rd1;
	return pidDist(rd1);
}

class CommObj {
private:
	string seqNum;
public:
	char control;
	SockAddr* addr;
	string pid;
	string msg;


	CommObj() {
		ostringstream oss;
		oss << setfill('0') << setw(15) << (itos(-1) + "." + itos(0));
		seqNum = oss.str();
		addr = NULL;
	}

	CommObj(const CommObj& other) {
		set(other);
	}

	CommObj* setValues(char control, string pid, string msg, SockAddr* addr) {
		this->control = control;
		this->pid = pid;
		this->msg = msg;
		this->addr = addr;
		return this;
	}

	CommObj* set(const CommObj &other) {
		control = other.control;
		addr = other.addr;
		pid = other.pid;
		msg = other.msg;
		seqNum = other.getStringSeq();
		return this;
	}

	CommObj* decode(const char *msg, SockAddr* addr) {
		istringstream iss(msg);
		char buffer[1024];
		iss >> seqNum >> control >> pid >> std::ws;
		if (!iss.good()) throw COMMException("CommObj::decode(): stream error (" + string(msg) + ")");
		iss.get(buffer, 1024);
		this->msg = buffer;
		this->addr = addr;
		return this;
	}

	CommObj* markSeqNum(long int seq) {
		ostringstream oss;
		string S = (itos(seq) + "." + pid);
		oss << setfill('0') << setw(15) << S;
		if (!oss.good()) throw COMMException("markSeqNum(): stream error");
		seqNum = oss.str();
		return this;
	}

	string getEncoded() const {
		ostringstream oss;
		oss << getStringSeq() << " " << control << " " << pid << " " << msg;
		if (!oss.good()) throw COMMException("getEncoded(): stream error");
		return oss.str();
	}

	string getStringSeq() const {
		return seqNum;
	}

	bool operator<(const CommObj& other) const {
		bool test;
		try {
			test = stod(getStringSeq()) > stod(other.getStringSeq());
		} catch (exception &e) {
			cerr << getStringSeq() << " | " << other.getStringSeq() << endl;
			cerr << "Caused by: " << e.what() << endl;
			cerr << "Identifier: " << msg << " | " << other.msg << endl;
			throw e;
		}
		return test;
	}
};

class Msg {
public:
	CommObj* mComm;
	int delay;
	unordered_set<string> acks;

	~Msg() { delete mComm; }

	Msg(CommObj &msg) {
		this->mComm = new CommObj;
		this->mComm->set(msg);
		delay = 0;
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
	string fixedPID, selfAlias;
	unordered_map <long int, int> processAlias;
	unordered_map<double, Msg*> acks;
	priority_queue<double, vector<double>, greater<double>> unstableQueue;
	queue<CommObj> stableQueue;
	pthread_t receiver, sender;
	pthread_attr_t rattr, sattr;
	pthread_mutex_t stableQueueLock, unstableQueueLock;
	pthread_cond_t cond_wait;

	Clock* lClock;

	void initMultiCast();
	void initPointToPoint();
	void initiateGroupComm();
	static void* setOrderedRecv(void* arg);
	static void* contSend(void* arg);



public:
	Communicator(bool master, Clock* lClock, bool setupGroup) {
		this->lClock = lClock;
		pthread_mutex_init(&stableQueueLock, NULL);
		pthread_mutex_init(&unstableQueueLock, NULL);
		pthread_cond_init(&cond_wait, NULL);
		receiver = -1;
		sender = -1;
		initMultiCast();
		initPointToPoint();

		SockAddr sin;
		u_int len = sizeof(sin);
		if (getsockname(pSocket, (struct sockaddr *)&sin, &len) == -1) {
			perror("getsockname");
		}

		ostringstream oss;
		oss << setfill('0') << setw(15) << itos(randomize()) + itos(sin.sin_port);
		fixedPID = oss.str();
		selfAlias = fixedPID;
		pid = stol(fixedPID);
		cout << "PID: " << pid << endl;

		if (master) {
			this->master = pid;
		}

		if (setupGroup) {
			initiateGroupComm();
		}
		selfAlias = itos(getRank());
	}

	~Communicator () {
		while (!unstableQueue.empty()) unstableQueue.pop();
		while (!stableQueue.empty()) stableQueue.pop();
		for(auto ackPair: acks) {
			delete ackPair.second;
		}
		acks.clear();
	}

	bool isMaster() {
		return master == pid;
	}

	void setAutoMark() {
		try {
			if (!autoMarkSeq) {
				autoMarkSeq = true;
				cout << "Ordered multicast initiated" << endl;
				pthread_attr_init(&rattr);
				pthread_attr_init(&sattr);
				pthread_create(&receiver, &rattr, Communicator::setOrderedRecv, this);
				pthread_create(&sender, &sattr, Communicator::contSend, this);
			}
		} catch (exception& e) {
			COMMException ex("Communicator::setAutoMark()");
			ex.appendInfo(e.what());
			throw ex;
		}
	}

	void multicastFinish() {
		if (sender != -1){
			pthread_join(sender, NULL);
			sender = -1;
		}

		if (receiver != -1) {
			pthread_join(receiver, NULL);
			receiver = -1;
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
		mComm.setValues('D', getAliasPid(), temp, &mOutAddr);
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

	CommObj* recv(int sock, CommObj& comm, struct timespec *tv);
	CommObj* recv(int sock, CommObj *comm, bool skipQueue, struct timespec *tv);

	CommObj* orderedRecv();

	string getFPID() {
		return fixedPID;
	}

	string getAliasPid() {
		return selfAlias;
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

void Communicator::send(int sock, CommObj& comm) {
	char buffer[1024];
	string encoded = comm.getEncoded();
	uSend(sock, buffer, mFormat(encoded.c_str(), encoded.length(), buffer), (struct sockaddr *) (comm.addr));
}

CommObj* Communicator::recv(int sock, CommObj *comm, bool skipQueue, struct timespec* tv) {
	try {

		if (!skipQueue) {
			CommObj *c = NULL;
			if (sock == mSocket && (autoMarkSeq || !stableQueue.empty())) {
				pthread_mutex_lock(&stableQueueLock);
				if (stableQueue.empty()) {
					pthread_cond_wait(&cond_wait, &stableQueueLock);
				}
				if (!stableQueue.empty()) {
					comm->set(stableQueue.front());
					stableQueue.pop();
					c = comm;
				}
				pthread_mutex_unlock(&stableQueueLock);
				return  c;
			}
			return NULL;
		} else {
			bool ret = true;
			SockAddr* addr = NULL;
			char buffer[1024];
			if (sock == mSocket) {
				addr = comm->addr;
			}
			if (tv) {
				fd_set sockFDs;
				FD_ZERO(&sockFDs);
				FD_SET(sock , &sockFDs);
				ret = pselect(sock + 1, &sockFDs, NULL, NULL, tv, NULL) > 0 && FD_ISSET(sock, &sockFDs);
			}

			if (ret && uRecv(sock, buffer, (struct sockaddr *) addr)) {
				// cout << "Receiving ." << buffer << endl;
				return comm->decode(buffer, addr);
			}
		}
	} catch (COMMException& e) {
		e.appendInfo("Communicator::recv(int, CommObj&, bool, struct timespec*)");
		throw;
	}
	return NULL;
}

CommObj* Communicator::recv(int sock, CommObj& comm, struct timespec *tv) {
	try {
		return recv(sock, &comm, true, tv);
	} catch (COMMException& e) {
		e.appendInfo("Communicator::recv(int, CommObj&, struct timespec*)");
		throw;
	}
}

void* Communicator::setOrderedRecv(void* arg) {
	Communicator& self = *((Communicator*) arg);
	const int 	sameProcDelay = 5,
				globalDelay = 3 * sameProcDelay,
				requiredAcks = self.getGroupSize();
	int emptyRecvCount;
	double lastStable = -1.0, recvSeq, topSeq;
	
	struct timespec tv = (struct timespec){.tv_sec = 0, .tv_nsec = 100000000};
	CommObj aComm;
	bool emptyRecv, senderRecv;
	
	sleep(3);

	do {
		aComm.setValues('T', self.getAliasPid(), "NOOP", NULL);

		pthread_mutex_lock(&self.unstableQueueLock);

		topSeq = self.unstableQueue.empty() ? -1 : self.unstableQueue.top();
		if (DEBUG) cout << endl << self.getAliasPid() << "| Last: " << lastStable << ", Top: " << topSeq << endl;
		emptyRecv = true;
		senderRecv = false;

		if (self.recv(self.getSocket(true), &aComm, true, &tv)) {
			if (DEBUG) cout << "REC: [" << aComm.pid << "] " << aComm.getEncoded() << endl;
			senderRecv = aComm.pid == self.getAliasPid();
			emptyRecv = false;

			recvSeq = stod(aComm.getStringSeq());

			//Case 1: M, R: Send Ack, A Drop
			if (recvSeq <= lastStable) {
				if (DEBUG) cout << "Case 1";
				
				if (aComm.pid != self.getAliasPid() && (aComm.control == 'M' || aComm.control == 'R')) {
					aComm.setValues('A', self.getAliasPid(), aComm.msg, self.getCastAddr(true));
					if (DEBUG) cout << ", Sending: " << aComm.getEncoded() << endl; 
					self.send(self.getSocket(true), aComm);
				} 

				//Drop
				else if (DEBUG){
					cout << " DROPPED" << endl;
				}
			}

			// Case 2, M, R, A: Insert, R, A: Mark
			else if (recvSeq > lastStable && recvSeq < topSeq){
				if (DEBUG) cout << "Case 2";

				//M, R, A: Insert
				if (DEBUG) cout << ", Inserting";

				if (self.acks.count(recvSeq) == 0) {
					CommObj temp(aComm);
					if (aComm.control == 'A')
						temp.control = 'R';
					self.acks.insert(pair<double, Msg*>(recvSeq, new Msg(temp)));
				}

				//A, R: Mark
				if (aComm.control == 'A' || aComm.control == 'R') {
					if (DEBUG) cout << ", Marking";
					self.acks.at(recvSeq)->acks.insert(aComm.pid);
				}

				self.unstableQueue.push(recvSeq);

				if (DEBUG) cout << endl;
			}

			//Case 3, M: Drop, A, R: Mark, R: Send Ack
			else if (recvSeq == topSeq) {
				if (DEBUG) cout << "Case 3";
				//A, R: Mark
				if (aComm.control == 'A' || aComm.control == 'R') {
					if (DEBUG) cout << ", Marking"; 
					self.acks.at(recvSeq)->acks.insert(aComm.pid);
				}

				//R: Send Ack
				if (aComm.control == 'R' && aComm.pid != self.getAliasPid()) {
					aComm.setValues('A', self.getAliasPid(), aComm.msg, self.getCastAddr(true));
					if (DEBUG) cout << ", Sending: " << aComm.getEncoded() << endl; 
					self.send(self.getSocket(true), aComm);
				}

				//Drop
				if (DEBUG && aComm.control == 'M') {
					cout << " DROPPED";
				}

				if (DEBUG) cout << endl;
			}

			//Case 4, M: Drop, R:Mark, A: Mark
			else if (recvSeq > topSeq && self.acks.count(recvSeq) > 0) {
				if (DEBUG) cout << "Case 4, ";

				if (aComm.control == 'A' || aComm.control == 'R') {
					if (DEBUG) cout << "Marking" << endl;
					self.acks.at(recvSeq)->acks.insert(aComm.pid);
				}

				else if(DEBUG) {
					cout << "DROPPED" << endl;
				}
			}

			//Case 5, M, R, A: Insert, R,A: Mark
			else if(recvSeq > topSeq && self.acks.count(recvSeq) == 0) {
				if (DEBUG) cout << "Case 5, Inserting";

				CommObj temp(aComm);
				if (aComm.control == 'A')
					temp.control = 'R';

				self.acks.insert(pair<double, Msg*>(recvSeq, new Msg(temp)));
				
				if (aComm.control == 'A' || aComm.control == 'R')
					self.acks.at(recvSeq)->acks.insert(aComm.pid);

				self.unstableQueue.push(recvSeq);

				if (DEBUG) cout << endl;
			}
		}

		//Process your own queue
		topSeq = self.unstableQueue.empty() ? -1 : self.unstableQueue.top();
		if (topSeq > -1.0) {
			//By this time all acks should be present.
			aComm.set(*self.acks.at(topSeq)->mComm);

			if (DEBUG){
				string s = " Acks <";
				for (auto ackPid: self.acks.at(topSeq)->acks) s += ackPid + ", ";
				s += ">";
				cout << "TOP: " << aComm.getEncoded() << s << endl;
			} 

			try {
				//Send Ack if not done so & Mark
				if (self.acks.at(topSeq)->acks.count(self.getAliasPid()) == 0) {
					aComm.setValues('A', self.getAliasPid(), aComm.msg, self.getCastAddr(true));
					if (DEBUG) cout << "TOP ACK: " << aComm.getEncoded() << endl;
					self.acks.at(topSeq)->acks.insert(aComm.pid);
					self.send(self.getSocket(true), aComm);
					aComm.set(*self.acks.at(topSeq)->mComm);
				}

				//If Acks complete, Push to stable
				if (self.acks.at(topSeq)->acks.size() == requiredAcks) {
					if (DEBUG) cout << "Stable: " << aComm.getEncoded() << endl;
					pthread_mutex_lock(&self.stableQueueLock);
					delete self.acks.at(topSeq);
					self.acks.erase(topSeq);
					lastStable = stod(aComm.getStringSeq());
					self.stableQueue.push(aComm);
					self.unstableQueue.pop();
					pthread_cond_signal(&self.cond_wait);
					pthread_mutex_unlock(&self.stableQueueLock);
				} else {
					if (emptyRecv) 
						self.acks.at(topSeq)->delay += 1; 

					//Issue Re if delay counter breached
					if (self.acks.at(topSeq)->delay >= sameProcDelay) {
						aComm.setValues('R', self.getAliasPid(), aComm.msg, self.getCastAddr(true));
						if (DEBUG) cout << "RE: " << aComm.getEncoded() << endl;
						self.send(self.getSocket(true), aComm);
						self.acks.at(topSeq)->delay = 0;
					}
				}
					
			} catch (exception& e) {
				COMMException ex("Communicator::setOrderedRecv() processing own queue");
				ex.setInfo(e.what());
				throw ex;
			}
		}

		pthread_mutex_unlock(&self.unstableQueueLock);

		if (!emptyRecv && !senderRecv) {
			emptyRecvCount = globalDelay;
		}

	} while (--emptyRecvCount);
	self.autoMarkSeq = false;

	pthread_cond_signal(&self.cond_wait);
	return NULL;
}

void* Communicator::contSend(void* arg) {
	int numberOfMessages = 50;
	try {
		Communicator *self = (Communicator*)arg;
		string temp;
		CommObj aComm;
		u_int counter = 1, timer = 40000;
		double seq;
		if (DEBUG) cout << endl << "Sending" << endl;
		while (numberOfMessages--) {
			if (DEBUG) cout << "Sending[" << counter << "] from " << self->getAliasPid() << endl; 
			pthread_mutex_lock(&self->unstableQueueLock);
			temp = "Message " + itos(counter++) + " S:" + self->getAliasPid();
			aComm.setValues('M', self->getAliasPid(), temp, self->getCastAddr(true));
			aComm.markSeqNum(self->lClock->markEvent());
			seq = stod(aComm.getStringSeq());
			//Case 5
			self->acks.insert(pair<double, Msg*>(seq, new Msg(aComm)));
			self->unstableQueue.push(seq);
			self->send(self->getSocket(true), aComm);
			pthread_mutex_unlock(&self->unstableQueueLock);
			usleep(timer);
		}
	} catch (COMMException& e) {
		e.appendInfo("Communicator::contSend(void*)");
		throw;
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

	mComm.setValues(ENQ, getAliasPid(), temp, &mOutAddr);
	if (master == pid) {
		send(mSocket, mComm);
	}
	mComm.addr = &mInAddr;

	//Receiving from master
	if (recv(mSocket, mComm, NULL) && mComm.control == ENQ) {
		mComm.setValues(ENQ, getAliasPid(), temp, Communicator::decodeSock(mComm.msg));
		send(this->getSocket(false), mComm);
	}

	struct timeval tin;
	tin.tv_sec = 2;
	tin.tv_usec = 0;

	if (setsockopt(pSocket, SOL_SOCKET, SO_RCVTIMEO, &tin, sizeof(tin)) < 0) {
		std::cerr << "Set Timeout Error";
	}
	cout << "Discovering multicast group members, 2 seconds advertisement window" << endl;
	vector<long int> sortedPIDs;

	if (isMaster()) {
		temp = "";
		while (recv(this->getSocket(false), mComm, NULL) && mComm.control == ENQ) {
			temp += mComm.pid + " " + mComm.msg + " ";
		}
		mComm.setValues('L', getAliasPid(), temp, this->getCastAddr(true));
		send(this->getSocket(true), mComm);
	}
	mComm.addr = this->getCastAddr(false);
	//Receiving from master
	if (recv(this->getSocket(true), mComm, NULL) && mComm.control == 'L') {
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

	cout << endl << "New Leader: " << master << endl;
	cout << "Members [" << sortedPIDs.size() << "]: ";

	for (int i = 0; i < sortedPIDs.size(); i++) {
		cout << sortedPIDs[i] << "[" << i << "]  ";
		processAlias.insert(pair<long int, int>(sortedPIDs[i], i));
	}

	cout << endl << endl;
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

	if (recv(getSocket(true), mComm, NULL) && mComm.control == 'B') {
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
		for (int i = getGroupSize(); i > 0 && recv(getSocket(false), mComm, NULL) && mComm.control == 'B'; i--) {
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
	if (recv(getSocket(false), mComm, NULL) && mComm.control == 'U') {
		cout << "Berkley Sync stage 4, Adjusting offset: " << mComm.msg << endl;
		cout << "Original Clock Sequence: " << lClock->getClock() << endl;
		lClock->setClock((double)lClock->getClock() + stod(mComm.msg));
	}

	cout << "Clock (Synced): " << lClock->getClock() << endl;
}

#endif