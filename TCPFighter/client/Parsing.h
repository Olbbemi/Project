#ifndef Parsing_Info
#define Parsing_Info

#define UNICODE
#define _UNICODE

#include <iostream>
#include <string.h>
#include <tchar.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <locale>

#define Bulk 50

using namespace std;

class PARSER
{
private:
	wifstream input;
	TCHAR *buf, temp[Bulk];
	int index, length, store;
	vector<int> table;

	TCHAR* Get_Data();
	void Find_Str(const TCHAR *str);
	void Find_Exception(const TCHAR *str);
	void Make_Table(const TCHAR *pivot);
	int KMP(const TCHAR *pivot);

	enum STATUS
	{
		find = 0,
		fail,
		exception
	};

public:
	PARSER();
	~PARSER();

	void Init();
	void Find_Scope(const TCHAR *str);
	void Get_Value(const TCHAR *str, int &data);
	void Get_Value(const TCHAR *str, double &data);
	void Get_Value(const TCHAR *str, TCHAR &data);
	void Get_String(const TCHAR *str, TCHAR *data, int size);
};

class Unicodecvt : public codecvt<wchar_t, char, mbstate_t>
{
protected:
	virtual bool __CLR_OR_THIS_CALL do_always_noconv() const _NOEXCEPT	{	return true;	}
};

#endif
