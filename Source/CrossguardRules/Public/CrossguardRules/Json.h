// A small JSON document model, reader and writer for save files (docs/persistence.md). Plain C++,
// no dependencies, no exceptions.
//
// Integers are kept exact (int64) apart from other numbers. Objects keep their key order. The
// reader refuses what a save never needs and a tampered file might use to confuse it: duplicate
// keys, nesting deeper than MaxDepth, trailing content, invalid escapes.

#pragma once

#include "CrossguardRules/RulesApi.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Crossguard
{
	class CROSSGUARDRULES_API FJsonNode
	{
	public:
		enum class EType
		{
			Null,
			Bool,
			// A number written without a fraction or exponent that fits int64.
			Integer,
			// Any other number.
			Real,
			String,
			Array,
			Object,
		};

		FJsonNode() = default;

		static FJsonNode MakeBool(bool bValue);
		static FJsonNode MakeInt(std::int64_t Value);
		static FJsonNode MakeReal(double Value);
		static FJsonNode MakeString(std::string Value);
		static FJsonNode MakeArray();
		static FJsonNode MakeObject();

		EType Type() const { return NodeType; }
		bool IsNull() const { return NodeType == EType::Null; }
		bool IsBool() const { return NodeType == EType::Bool; }
		bool IsInteger() const { return NodeType == EType::Integer; }
		// Integer or Real.
		bool IsNumber() const { return NodeType == EType::Integer || NodeType == EType::Real; }
		bool IsString() const { return NodeType == EType::String; }
		bool IsArray() const { return NodeType == EType::Array; }
		bool IsObject() const { return NodeType == EType::Object; }

		// Defaults (false, 0, empty) when the type does not match.
		bool AsBool() const { return NodeType == EType::Bool && bBool; }
		std::int64_t AsInt() const { return NodeType == EType::Integer ? Int : 0; }
		// Integers convert.
		double AsReal() const;
		const std::string& AsString() const { return Text; }

		// Arrays. Empty for other types.
		const std::vector<FJsonNode>& Items() const { return Children; }
		// Turns a Null node into an array; does nothing to other types.
		FJsonNode& Push(FJsonNode Value);

		// Objects, in insertion order. Null if absent or not an object.
		const FJsonNode* Find(const std::string& Key) const;
		FJsonNode* Find(const std::string& Key);
		// Replaces an existing key. Turns a Null node into an object; does nothing to other types
		// (returns *this then).
		FJsonNode& Set(const std::string& Key, FJsonNode Value);
		bool Remove(const std::string& Key);
		const std::vector<std::string>& Keys() const { return MemberKeys; }
		// Values in Keys() order.
		const std::vector<FJsonNode>& Values() const { return Children; }

	private:
		EType NodeType = EType::Null;
		bool bBool = false;
		std::int64_t Int = 0;
		double Real = 0.0;
		std::string Text;
		// Array items, or object values (keys in MemberKeys).
		std::vector<FJsonNode> Children;
		std::vector<std::string> MemberKeys;
	};

	struct FJsonParseError
	{
		std::string Message;
		// Byte offset in the input.
		std::size_t Offset = 0;
	};

	// Parses UTF-8 JSON (a leading BOM is skipped). False, with OutError set, on any error.
	CROSSGUARDRULES_API bool ParseJson(const std::string& Text, FJsonNode& OutNode, FJsonParseError* OutError = nullptr, int MaxDepth = 64);

	// Two-space indentation when bPretty. Strings are escaped (quotes, backslash, control
	// characters); other UTF-8 is written as is. Non-finite numbers are written as 0. Reals are
	// written so that ParseJson reads back the same double.
	CROSSGUARDRULES_API std::string WriteJson(const FJsonNode& Node, bool bPretty = true);
}
