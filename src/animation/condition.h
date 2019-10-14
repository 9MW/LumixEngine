#pragma once


#include "engine/array.h"
#include "engine/lumix.h"


namespace Lumix
{


class Animation;
struct IAllocator;
class OutputMemoryStream;
template<class Key> struct HashFunc;
template <typename K, typename V, typename H> class HashMap;


namespace Anim
{


struct InputDecl
{
	enum Type : int
	{
		// don't change order
		FLOAT,
		U32,
		BOOL,
		EMPTY
	};

	struct Constant
	{
		Type type = EMPTY;
		union
		{
			float f_value;
			int i_value;
			bool b_value;
		};
		StaticString<32> name;
	};

	struct Input
	{
		Type type = EMPTY;
		int offset;
		StaticString<32> name;
	};

	Input inputs[32];
	u32 inputs_count = 0;
	Constant constants[32];
	u32 constants_count = 0;

	static int getSize(Type type);

	void removeInput(u32 index);
	void removeConstant(int index);
	int addInput();
	int addConstant();
	void recalculateOffsets();
	int getInputIdx(const char* name, int size) const;
	int getConstantIdx(const char* name, int size) const;
};


struct Condition
{
	enum class Error
	{
		NONE,
		UNKNOWN_IDENTIFIER,
		MISSING_LEFT_PARENTHESIS,
		MISSING_RIGHT_PARENTHESIS,
		UNEXPECTED_CHAR,
		OUT_OF_MEMORY,
		MISSING_BINARY_OPERAND,
		NOT_ENOUGH_PARAMETERS,
		INCORRECT_TYPE_ARGS,
		NO_RETURN_VALUE
	};

	static const char* errorToString(Error error);

	explicit Condition(IAllocator& allocator);

	bool eval(const struct RuntimeContext& rc) const;
	Error compile(const char* expression, InputDecl& decl);

	Array<u8> bytecode;
};


} // namespace Anim


} // namespace Lumix