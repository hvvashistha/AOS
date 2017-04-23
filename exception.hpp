#ifndef __COMMException__

#define __COMMException__
#include <string>
#include <exception>

class COMMException: public std::exception {
private:
	std::string exec;
public:
	COMMException() {
		exec = "!?";
	}

	COMMException(std::string exec) {
		this->exec = exec;
	}

	void setInfo(std::string info) throw() {
		exec = info;
	}

	void appendInfo(std::string info) throw() {
		exec += '\n' + info;
	}

	virtual const char* what() const throw()
	{
		return exec.c_str();
	}
};

#endif