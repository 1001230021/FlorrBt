#pragma once
#include <iostream>
#include <map>
#include <string>
#include <functional>
#include "logger.h"

class CConsole {
private:
	using CallBack = std::function<void(const std::vector<std::string>&)>;
	std::unordered_map<std::string, CallBack> m_Cmds;

public:
	void RegisterCommand(std::string name, CallBack cb);
	void ExecuteLine(std::string line);

	CConsole() = default;
	~CConsole() = default;
};