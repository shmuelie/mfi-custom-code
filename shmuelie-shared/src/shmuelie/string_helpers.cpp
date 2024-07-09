#include "shmuelie/string_helpers.h"

using namespace std;

bool shmuelie::is_number(string const& str)
{
	// `std::find_first_not_of` searches the string for the first character
	// that does not match any of the characters specified in its arguments
	return !str.empty() && (str.find_first_not_of("[0123456789]") == string::npos);
}

vector<string> shmuelie::split(string const& str, char delim)
{
	auto i = 0;
	vector<string> list;

	auto pos = str.find(delim);

	while (pos != string::npos)
	{
		list.push_back(str.substr(i, pos - i));
		i = ++pos;
		pos = str.find(delim, pos);
	}

	list.push_back(str.substr(i, str.length()));

	return list;
}