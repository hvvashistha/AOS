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
		oss << setfill('0') << setw(15) << (itos(0) + "." + pid);
		seqNum = oss.str();
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
	priority_queue<CommObj> unstableQueue;
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

	bool isMaster() {
		return master == pid;
	}

	void setAutoMark(bool mark) {
		CommObj mComm;
		mComm.setValues('X', getAliasPid(), "Terminate", this->getCastAddr(true));
		mComm.markSeqNum(lClock->markEvent());
		setAutoMark(mark, mComm);
	}

	void setAutoMark(bool mark, CommObj mComm) {
		try {
			if (mark && !autoMarkSeq) {
				cout << "Ordered multicast initiated" << endl;
				autoMarkSeq = mark;
				pthread_attr_init(&rattr);
				pthread_attr_init(&sattr);
				pthread_create(&receiver, &rattr, Communicator::setOrderedRecv, this);
				pthread_create(&sender, &sattr, Communicator::contSend, this);
			} else if (!mark && autoMarkSeq) {
				if (DEBUG)
					cout << "Terminating Ordered multicast" << endl;
				mComm.setValues('X', getAliasPid(), "Terminate", this->getCastAddr(true));
				this->unstableQueue.push(mComm);
				autoMarkSeq = mark;
				this->send(this->getSocket(true), mComm);
				mComm.control = 'A';
				this->send(this->getSocket(true), mComm);
			}
		} catch (exception& e) {
			COMMException ex("Communicator::setAutoMark(bool, CommObj)");
			ex.appendInfo(e.what());
			throw ex;
		}
	}

	void multicastFinish() {
		if (!autoMarkSeq) {
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
	CommObj* recv(int sock, CommObj& comm, bool skipQueue, struct timespec *tv);

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

CommObj* Communicator::recv(int sock, CommObj& comm, bool skipQueue, struct timespec* tv) {
	try {

		if (!skipQueue) {
			CommObj *c = NULL;
			if (sock == mSocket && (autoMarkSeq || !stableQueue.empty())) {
				pthread_mutex_lock(&stableQueueLock);
				if (stableQueue.empty()) {
					pthread_cond_wait(&cond_wait, &stableQueueLock);
				}
				if (!stableQueue.empty()) {
					comm.set(stableQueue.front());
					stableQueue.pop();
					c = &comm;
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
				addr = comm.addr;
			}
			if (tv) {
				fd_set sockFDs;
				FD_ZERO(&sockFDs);
				FD_SET(sock , &sockFDs);
				ret = pselect(sock + 1, &sockFDs, NULL, NULL, tv, NULL) > 0 && FD_ISSET(sock, &sockFDs);
			}

			if (ret && uRecv(sock, buffer, (struct sockaddr *) addr)) {
				// cout << "Receiving ." << buffer << endl;
				return comm.decode(buffer, addr);
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
		return recv(sock, comm, true, tv);
	} catch (COMMException& e) {
		e.appendInfo("Communicator::recv(int, CommObj&, struct timespec*)");
		throw;
	}
}

void* Communicator::setOrderedRecv(void* arg) {
	Communicator *self = (Communicator*) arg;
	int counter = 0;
	bool drop = true;
	struct timespec tv;
	tv.tv_sec = 0;
	tv.tv_nsec = 100000000;
	unordered_map<string, pair<bool, int>> msgs;
	CommObj aComm;
	string lastStableInsert = "0000000000000.0";
	pair<string, int> dropRequests = pair<string, int>("", 0);
	const int maxRequestDelay = max(5, (int)(3 * self->getGroupSize())), requiredAcks = self->getGroupSize();
	try {
		do {
			pthread_mutex_lock(&self->unstableQueueLock);
			if (DEBUG)
				cout << "-----------------------" << lastStableInsert << "--------------------------------" << endl;
			aComm.setValues('M', self->getAliasPid(), "MINED PACKET", self->getCastAddr(false));

			if (self->recv(self->getSocket(true), aComm, true, &tv)) {
				// if (DEBUG)
					cout << "REC: " << aComm.getEncoded() << " ";

				if (aComm.control == 'A' && stod(aComm.getStringSeq()) > stod(lastStableInsert)) {
					if (DEBUG)
						cout << " ACK";
					if (msgs.count(aComm.getStringSeq()) > 0) {
						msgs.at(aComm.getStringSeq()).second = msgs.at(aComm.getStringSeq()).second + 1;
						counter = 0;
					} else if (aComm.pid == self->getAliasPid()) {
						msgs.insert(pair<string, pair<bool, int>>(aComm.getStringSeq(), pair<bool, int>(true, 1)));
					}

				} else if ((aComm.control == 'M' || aComm.control == 'X') && aComm.pid != self->getAliasPid()) {
					
					if (DEBUG)
						cout << " PUSH";
					self->lClock->markEvent((long int)stod(aComm.getStringSeq()));

					self->unstableQueue.push(aComm);
					msgs.insert(pair<string, pair<bool, int>>(aComm.getStringSeq(), pair<bool, int>(false, 0)));
					
					if (aComm.control == 'X') {
						self->setAutoMark(false, aComm);
					}
					counter = 0;

				} else if (aComm.control == 'R') {

					if (DEBUG)
						cout << endl << "RE: " << " R" << stod(aComm.getStringSeq()) << ", L" << stod(lastStableInsert) << " ";

					if (aComm.getStringSeq() <= lastStableInsert) {
						if (DEBUG)
							cout << "Case 1 ";
						aComm.control = 'A';
						aComm.pid = self->getAliasPid();
						aComm.addr = self->getCastAddr(true);
						self->send(self->getSocket(true), aComm);
						if (DEBUG)
							cout << "RE ACK ";
					} else if (self->getAliasPid() != aComm.pid) {

						if (DEBUG)
							cout << "Case 2 ";
						pair<bool, int> acks = pair<bool, int>(false, 0);

						if (msgs.count(aComm.getStringSeq()) == 0) {
							self->unstableQueue.push(aComm);
							msgs.insert(pair<string, pair<bool, int>>(aComm.getStringSeq(), acks));
						}

						msgs.at(aComm.getStringSeq()) = acks;

					}
				} else if (DEBUG && aComm.pid != self->getAliasPid()) {
					if (DEBUG)
						cout << " ROGUE";
					if (!self->unstableQueue.empty()) {
						CommObj mComm = self->unstableQueue.top();

					}
				}

				// if (aComm.control != 'R') {
				// 	counter = 0;
				// }

				if (DEBUG) {
					if (msgs.count(aComm.getStringSeq()) > 0)
						cout << "<" << (int)msgs.at(aComm.getStringSeq()).first << "," << msgs.at(aComm.getStringSeq()).second << "> ";
					else if (aComm.pid == self->getAliasPid()) {
						cout << " <<1,0>>";
					} else {
						cout << " <<0,0, " << lastStableInsert << " >>";
					}
					cout << " From " << aComm.pid << endl;
				}
			}

			if (!self->unstableQueue.empty()) {
				aComm.set(self->unstableQueue.top());
				if (DEBUG) {
					cout << "TOP: " << aComm.getEncoded() << " ";
					if (aComm.pid == self->getAliasPid() && msgs.count(aComm.getStringSeq()) == 0) {
						cout << " <<1, 0>>";
					} else {
						cout << "<" << (int)msgs.at(aComm.getStringSeq()).first << "," << msgs.at(aComm.getStringSeq()).second << "> ";
					}
				}

				//Create a counter for message (It is from the same process, the only use case remaining here) if doesn't exist
				if (msgs.count(aComm.getStringSeq()) == 0 && aComm.pid == self->getAliasPid()) {
					msgs.insert(pair<string, pair<bool, int>>(aComm.getStringSeq(), pair<bool, int>(true, 0)));
				}

				if (msgs.count(aComm.getStringSeq()) == 0) {
					cout << "[" << self->getAliasPid() << "] ** FATAL ** :" << aComm.getEncoded();
				}

				bool weakAck = msgs.count(aComm.getStringSeq()) > 0 && (msgs.at(aComm.getStringSeq()).second >= requiredAcks ||
				               (msgs.at(aComm.getStringSeq()).first == false && msgs.at(aComm.getStringSeq()).second == (requiredAcks - 1)));

				//Send Ack if not done so
				if (!msgs.at(aComm.getStringSeq()).first) {
					aComm.control = 'A';
					aComm.pid = self->getAliasPid();
					aComm.addr = self->getCastAddr(true);
					if (DEBUG)
						cout << "TOP ACK ";
					self->send(self->getSocket(true), aComm);
					msgs.at(aComm.getStringSeq()).first = true;
				}

				//Add to stable Queue if all acks are received
				if (weakAck) {
					if (aComm.control != 'X') {
						// cout << endl << "Marking STABLE: [" << aComm.getStringSeq() << "] " << aComm.msg;
						pthread_mutex_lock(&self->stableQueueLock);
						self->stableQueue.push(self->unstableQueue.top());
						self->unstableQueue.pop();
						lastStableInsert = aComm.getStringSeq();
						msgs.erase(lastStableInsert);
						if (DEBUG)
							cout << "<Marking Stable>";
						pthread_cond_signal(&self->cond_wait);
						pthread_mutex_unlock(&self->stableQueueLock);
					} else {
						while (!self->unstableQueue.empty()) {
							self->unstableQueue.pop();
						}
					}
				} else if (dropRequests.first == aComm.getStringSeq()) {
					if (++dropRequests.second >= maxRequestDelay) {
						if (DEBUG)
							cout << " Re-Requesting";
						aComm.control = 'R';
						aComm.pid = self->getAliasPid();
						aComm.addr = self->getCastAddr(true);
						msgs.at(aComm.getStringSeq()).first = false;
						msgs.at(aComm.getStringSeq()).second = 0;
						// self->lClock->markEvent();
						self->send(self->getSocket(true), aComm);
						dropRequests.second = 0;
						// cout << "REQUESTING: [" << aComm.getStringSeq() << "] " << aComm.control << " " << aComm.pid << " " << aComm.msg << endl;
					}
				} else {
					dropRequests.first = aComm.getStringSeq();
					dropRequests.second = 0;
				}
			} else if (DEBUG) {
				cout << "TOP: EMPTY";
			}
			
			// if (DEBUG)
				cout << endl;

			if (!self->autoMarkSeq) {
				++counter;
				// if (DEBUG)
					cout << "Counter: " << counter << endl;
			}
			pthread_mutex_unlock(&self->unstableQueueLock);
		} while (self->autoMarkSeq || counter < (maxRequestDelay * 1.5));

		if (counter > maxRequestDelay + 10) {
			cout << "Failed to synchronize!!!" << endl;
		}
		if (DEBUG)
			cout << "Finish" << endl;

		pthread_mutex_init(&self->unstableQueueLock, NULL);

		aComm.addr = self->getCastAddr(false);
		while (self->recv(self->getSocket(true), aComm, true, &tv));
		pthread_cond_signal(&self->cond_wait);
	} catch (COMMException& e) {
		e.appendInfo("Communicator::setOrderedRecv(void*) Catch(1)");
		throw;
	} catch (exception &e) {
		COMMException ex("Communicator::setOrderedRecv(void*) Catch(1)");
		ex.appendInfo(e.what());
		throw ex;
	}

	return NULL;
}

void* Communicator::contSend(void* arg) {
	int numberOfMessages = 15;
	try {
		Communicator *self = (Communicator*) arg;
		string temp;
		CommObj aComm;
		u_int counter = 1, timer = 40000;
		// cout << "sending thread setup" << endl;
		while (self->autoMarkSeq && counter <= numberOfMessages) {
			pthread_mutex_lock(&self->unstableQueueLock);
			temp = "Random Message " + itos(counter++);
			aComm.setValues('M', self->getAliasPid(), temp, self->getCastAddr(true));
			aComm.markSeqNum(self->lClock->markEvent());
			// cout << "Sending: " << aComm.getEncoded() << endl;
			self->unstableQueue.push(aComm);
			self->send(self->getSocket(true), aComm);
			//Immediately send ACK
			aComm.control = 'A';
			self->send(self->getSocket(true), aComm);
			pthread_mutex_unlock(&self->unstableQueueLock);
			usleep(timer);
		}
		pthread_mutex_lock(&self->unstableQueueLock);
		self->setAutoMark(false);
		pthread_mutex_unlock(&self->unstableQueueLock);
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