#include <iostream>
#include "mfi/board_info.h"

int main()
{
	mfi::board b{};

	std::cout << "Board Name: " << b.name() << std::endl;
	std::cout << "Port Count: " << b.sensors().size() << std::endl;

	return 0;
}