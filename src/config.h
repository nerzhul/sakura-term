#pragma once

#include <string>

class Config
{
public:
	Config();

	void write();

	bool read();

	void monitor();

private:
	std::string m_file;
};
