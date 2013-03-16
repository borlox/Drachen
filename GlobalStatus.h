#ifndef GLOBAL_STATUS_H
#define GLOBAL_STATUS_H

struct GlobalStatus
{
	std::string level;

	size_t startLives;
	size_t startMoney;
};

extern GlobalStatus gStatus;

#endif //GLOBAL_STATUS_H
