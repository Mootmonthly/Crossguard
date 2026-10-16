#include "CrossguardRules/Json.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace Crossguard
{
	FJsonNode FJsonNode::MakeBool(bool bValue)
	{
		FJsonNode Node;
		Node.NodeType = EType::Bool;
		Node.bBool = bValue;
		return Node;
	}

	FJsonNode FJsonNode::MakeInt(std::int64_t Value)
	{
		FJsonNode Node;
		Node.NodeType = EType::Integer;
		Node.Int = Value;
		return Node;
	}

	FJsonNode FJsonNode::MakeReal(double Value)
	{
		FJsonNode Node;
		Node.NodeType = EType::Real;
		Node.Real = Value;
		return Node;
	}

	FJsonNode FJsonNode::MakeString(std::string Value)
	{
		FJsonNode Node;
		Node.NodeType = EType::String;
		Node.Text = std::move(Value);
		return Node;
	}

	FJsonNode FJsonNode::MakeArray()
	{
		FJsonNode Node;
		Node.NodeType = EType::Array;
		return Node;
	}

	FJsonNode FJsonNode::MakeObject()
	{
		FJsonNode Node;
		Node.NodeType = EType::Object;
		return Node;
	}

	double FJsonNode::AsReal() const
	{
		if (NodeType == EType::Integer)
		{
			return static_cast<double>(Int);
		}
		return NodeType == EType::Real ? Real : 0.0;
	}

	FJsonNode& FJsonNode::Push(FJsonNode Value)
	{
		if (NodeType == EType::Null)
		{
			NodeType = EType::Array;
		}
		if (NodeType != EType::Array)
		{
			return *this;
		}
		Children.push_back(std::move(Value));
		return Children.back();
	}

	const FJsonNode* FJsonNode::Find(const std::string& Key) const
	{
		if (NodeType != EType::Object)
		{
			return nullptr;
		}
		for (std::size_t Index = 0; Index < MemberKeys.size(); ++Index)
		{
			if (MemberKeys[Index] == Key)
			{
				return &Children[Index];
			}
		}
		return nullptr;
	}

	FJsonNode* FJsonNode::Find(const std::string& Key)
	{
		return const_cast<FJsonNode*>(static_cast<const FJsonNode*>(this)->Find(Key));
	}

	FJsonNode& FJsonNode::Set(const std::string& Key, FJsonNode Value)
	{
		if (NodeType == EType::Null)
		{
			NodeType = EType::Object;
		}
		if (NodeType != EType::Object)
		{
			return *this;
		}
		if (FJsonNode* Existing = Find(Key))
		{
			*Existing = std::move(Value);
			return *Existing;
		}
		MemberKeys.push_back(Key);
		Children.push_back(std::move(Value));
		return Children.back();
	}

	bool FJsonNode::Remove(const std::string& Key)
	{
		if (NodeType != EType::Object)
		{
			return false;
		}
		for (std::size_t Index = 0; Index < MemberKeys.size(); ++Index)
		{
			if (MemberKeys[Index] == Key)
			{
				MemberKeys.erase(MemberKeys.begin() + static_cast<std::ptrdiff_t>(Index));
				Children.erase(Children.begin() + static_cast<std::ptrdiff_t>(Index));
				return true;
			}
		}
		return false;
	}

	// File-local helpers in a named namespace: the Unreal module builds as a unity build.
	namespace JsonDetail
	{
		class FReader
		{
		public:
			FReader(const std::string& InText, int InMaxDepth) : Text(InText), MaxDepth(InMaxDepth) {}

			bool ParseDocument(FJsonNode& Out)
			{
				// UTF-8 byte order mark.
				if (Text.size() >= 3 && static_cast<unsigned char>(Text[0]) == 0xEF && static_cast<unsigned char>(Text[1]) == 0xBB
					&& static_cast<unsigned char>(Text[2]) == 0xBF)
				{
					Pos = 3;
				}
				SkipSpace();
				if (!ParseValue(Out, 0))
				{
					return false;
				}
				SkipSpace();
				return Pos == Text.size() || Fail("unexpected content after the document");
			}

			FJsonParseError Error;

		private:
			bool Fail(const char* Message)
			{
				if (Error.Message.empty())
				{
					Error.Message = Message;
					Error.Offset = Pos;
				}
				return false;
			}

			bool AtEnd() const { return Pos >= Text.size(); }
			char Peek() const { return AtEnd() ? '\0' : Text[Pos]; }

			void SkipSpace()
			{
				while (!AtEnd() && (Text[Pos] == ' ' || Text[Pos] == '\t' || Text[Pos] == '\n' || Text[Pos] == '\r'))
				{
					++Pos;
				}
			}

			bool Literal(const char* Word)
			{
				const std::size_t Length = std::strlen(Word);
				if (Text.compare(Pos, Length, Word) != 0)
				{
					return Fail("invalid literal");
				}
				Pos += Length;
				return true;
			}

			bool ParseValue(FJsonNode& Out, int Depth)
			{
				if (Depth > MaxDepth)
				{
					return Fail("nested too deeply");
				}
				switch (Peek())
				{
				case '{':
					return ParseObject(Out, Depth);
				case '[':
					return ParseArray(Out, Depth);
				case '"':
				{
					std::string Value;
					if (!ParseString(Value))
					{
						return false;
					}
					Out = FJsonNode::MakeString(std::move(Value));
					return true;
				}
				case 't':
					Out = FJsonNode::MakeBool(true);
					return Literal("true");
				case 'f':
					Out = FJsonNode::MakeBool(false);
					return Literal("false");
				case 'n':
					Out = FJsonNode();
					return Literal("null");
				default:
					return ParseNumber(Out);
				}
			}

			bool ParseObject(FJsonNode& Out, int Depth)
			{
				Out = FJsonNode::MakeObject();
				++Pos;
				SkipSpace();
				if (Peek() == '}')
				{
					++Pos;
					return true;
				}
				while (true)
				{
					SkipSpace();
					if (Peek() != '"')
					{
						return Fail("expected a key");
					}
					std::string Key;
					if (!ParseString(Key))
					{
						return false;
					}
					if (Out.Find(Key) != nullptr)
					{
						return Fail("duplicate key");
					}
					SkipSpace();
					if (Peek() != ':')
					{
						return Fail("expected ':'");
					}
					++Pos;
					SkipSpace();
					FJsonNode Value;
					if (!ParseValue(Value, Depth + 1))
					{
						return false;
					}
					Out.Set(Key, std::move(Value));
					SkipSpace();
					if (Peek() == ',')
					{
						++Pos;
						continue;
					}
					if (Peek() == '}')
					{
						++Pos;
						return true;
					}
					return Fail("expected ',' or '}'");
				}
			}

			bool ParseArray(FJsonNode& Out, int Depth)
			{
				Out = FJsonNode::MakeArray();
				++Pos;
				SkipSpace();
				if (Peek() == ']')
				{
					++Pos;
					return true;
				}
				while (true)
				{
					SkipSpace();
					FJsonNode Value;
					if (!ParseValue(Value, Depth + 1))
					{
						return false;
					}
					Out.Push(std::move(Value));
					SkipSpace();
					if (Peek() == ',')
					{
						++Pos;
						continue;
					}
					if (Peek() == ']')
					{
						++Pos;
						return true;
					}
					return Fail("expected ',' or ']'");
				}
			}

			static void AppendUtf8(std::string& Out, std::uint32_t CodePoint)
			{
				if (CodePoint < 0x80)
				{
					Out += static_cast<char>(CodePoint);
				}
				else if (CodePoint < 0x800)
				{
					Out += static_cast<char>(0xC0 | (CodePoint >> 6));
					Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
				}
				else if (CodePoint < 0x10000)
				{
					Out += static_cast<char>(0xE0 | (CodePoint >> 12));
					Out += static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
					Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
				}
				else
				{
					Out += static_cast<char>(0xF0 | (CodePoint >> 18));
					Out += static_cast<char>(0x80 | ((CodePoint >> 12) & 0x3F));
					Out += static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
					Out += static_cast<char>(0x80 | (CodePoint & 0x3F));
				}
			}

			bool ParseHex4(std::uint32_t& Out)
			{
				Out = 0;
				for (int Digit = 0; Digit < 4; ++Digit)
				{
					const char C = Peek();
					std::uint32_t Value = 0;
					if (C >= '0' && C <= '9')
					{
						Value = static_cast<std::uint32_t>(C - '0');
					}
					else if (C >= 'a' && C <= 'f')
					{
						Value = static_cast<std::uint32_t>(C - 'a' + 10);
					}
					else if (C >= 'A' && C <= 'F')
					{
						Value = static_cast<std::uint32_t>(C - 'A' + 10);
					}
					else
					{
						return Fail("invalid \\u escape");
					}
					Out = Out * 16 + Value;
					++Pos;
				}
				return true;
			}

			// One raw (unescaped) UTF-8 sequence starting at Pos; refuses malformed or overlong ones.
			bool CopyUtf8Sequence(std::string& Out)
			{
				const unsigned char Lead = static_cast<unsigned char>(Text[Pos]);
				int Extra = 0;
				std::uint32_t CodePoint = 0;
				if (Lead < 0x80)
				{
					Out += static_cast<char>(Lead);
					++Pos;
					return true;
				}
				if ((Lead & 0xE0) == 0xC0)
				{
					Extra = 1;
					CodePoint = Lead & 0x1F;
				}
				else if ((Lead & 0xF0) == 0xE0)
				{
					Extra = 2;
					CodePoint = Lead & 0x0F;
				}
				else if ((Lead & 0xF8) == 0xF0)
				{
					Extra = 3;
					CodePoint = Lead & 0x07;
				}
				else
				{
					return Fail("invalid UTF-8");
				}
				if (Pos + static_cast<std::size_t>(Extra) >= Text.size())
				{
					return Fail("invalid UTF-8");
				}
				for (int Index = 1; Index <= Extra; ++Index)
				{
					const unsigned char Next = static_cast<unsigned char>(Text[Pos + static_cast<std::size_t>(Index)]);
					if ((Next & 0xC0) != 0x80)
					{
						return Fail("invalid UTF-8");
					}
					CodePoint = (CodePoint << 6) | (Next & 0x3F);
				}
				const std::uint32_t Minimum = Extra == 1 ? 0x80 : Extra == 2 ? 0x800 : 0x10000;
				if (CodePoint < Minimum || CodePoint > 0x10FFFF || (CodePoint >= 0xD800 && CodePoint <= 0xDFFF))
				{
					return Fail("invalid UTF-8");
				}
				Out.append(Text, Pos, static_cast<std::size_t>(Extra) + 1);
				Pos += static_cast<std::size_t>(Extra) + 1;
				return true;
			}

			bool ParseString(std::string& Out)
			{
				// Opening quote.
				++Pos;
				while (true)
				{
					if (AtEnd())
					{
						return Fail("unterminated string");
					}
					const char C = Text[Pos];
					if (C == '"')
					{
						++Pos;
						return true;
					}
					if (static_cast<unsigned char>(C) < 0x20)
					{
						return Fail("control character in a string");
					}
					if (C != '\\')
					{
						if (!CopyUtf8Sequence(Out))
						{
							return false;
						}
						continue;
					}
					++Pos;
					const char Escape = Peek();
					++Pos;
					switch (Escape)
					{
					case '"':
						Out += '"';
						break;
					case '\\':
						Out += '\\';
						break;
					case '/':
						Out += '/';
						break;
					case 'b':
						Out += '\b';
						break;
					case 'f':
						Out += '\f';
						break;
					case 'n':
						Out += '\n';
						break;
					case 'r':
						Out += '\r';
						break;
					case 't':
						Out += '\t';
						break;
					case 'u':
					{
						std::uint32_t CodePoint = 0;
						if (!ParseHex4(CodePoint))
						{
							return false;
						}
						if (CodePoint >= 0xDC00 && CodePoint <= 0xDFFF)
						{
							return Fail("lone low surrogate");
						}
						if (CodePoint >= 0xD800 && CodePoint <= 0xDBFF)
						{
							std::uint32_t Low = 0;
							if (Peek() != '\\' || Pos + 1 >= Text.size() || Text[Pos + 1] != 'u')
							{
								return Fail("lone high surrogate");
							}
							Pos += 2;
							if (!ParseHex4(Low))
							{
								return false;
							}
							if (Low < 0xDC00 || Low > 0xDFFF)
							{
								return Fail("invalid surrogate pair");
							}
							CodePoint = 0x10000 + ((CodePoint - 0xD800) << 10) + (Low - 0xDC00);
						}
						AppendUtf8(Out, CodePoint);
						break;
					}
					default:
						--Pos;
						return Fail("invalid escape");
					}
				}
			}

			static bool IsDigit(char C) { return C >= '0' && C <= '9'; }

			bool ParseNumber(FJsonNode& Out)
			{
				const std::size_t Start = Pos;
				bool bNegative = false;
				if (Peek() == '-')
				{
					bNegative = true;
					++Pos;
				}
				if (!IsDigit(Peek()))
				{
					return Fail("invalid value");
				}

				// Up to 19 significant digits in Mantissa; Exponent10 scales it.
				std::uint64_t Mantissa = 0;
				int Significant = 0;
				long long Exponent10 = 0;
				bool bInteger = true;
				bool bExactInteger = true;

				const auto TakeDigit = [&](char Digit, bool bFraction)
				{
					if (Mantissa == 0 && Digit == '0')
					{
						if (bFraction)
						{
							--Exponent10;
						}
						return;
					}
					if (Significant < 19)
					{
						Mantissa = Mantissa * 10 + static_cast<std::uint64_t>(Digit - '0');
						++Significant;
						if (bFraction)
						{
							--Exponent10;
						}
					}
					else
					{
						bExactInteger = false;
						if (!bFraction)
						{
							++Exponent10;
						}
					}
				};

				if (Peek() == '0')
				{
					++Pos;
					if (IsDigit(Peek()))
					{
						return Fail("leading zero");
					}
				}
				else
				{
					while (IsDigit(Peek()))
					{
						TakeDigit(Text[Pos], false);
						++Pos;
					}
				}
				if (Peek() == '.')
				{
					bInteger = false;
					++Pos;
					if (!IsDigit(Peek()))
					{
						return Fail("invalid number");
					}
					while (IsDigit(Peek()))
					{
						TakeDigit(Text[Pos], true);
						++Pos;
					}
				}
				if (Peek() == 'e' || Peek() == 'E')
				{
					bInteger = false;
					++Pos;
					bool bNegativeExponent = false;
					if (Peek() == '+' || Peek() == '-')
					{
						bNegativeExponent = Peek() == '-';
						++Pos;
					}
					if (!IsDigit(Peek()))
					{
						return Fail("invalid number");
					}
					long long Exponent = 0;
					while (IsDigit(Peek()))
					{
						if (Exponent < 100000)
						{
							Exponent = Exponent * 10 + (Text[Pos] - '0');
						}
						++Pos;
					}
					Exponent10 += bNegativeExponent ? -Exponent : Exponent;
				}

				if (bInteger && bExactInteger && Exponent10 == 0)
				{
					const std::uint64_t Limit = bNegative ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1
														  : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
					if (Mantissa <= Limit)
					{
						const std::int64_t Value = bNegative
							? (Mantissa == Limit ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(Mantissa))
							: static_cast<std::int64_t>(Mantissa);
						Out = FJsonNode::MakeInt(Value);
						return true;
					}
				}

				double Value = 0.0;
				if (Mantissa != 0)
				{
					// Exact powers of ten up to 1e22: one correctly rounded operation when the
					// mantissa is exact too.
					if (Mantissa < (1ull << 53) && Exponent10 >= -22 && Exponent10 <= 22)
					{
						double Power = 1.0;
						for (long long Step = 0; Step < (Exponent10 < 0 ? -Exponent10 : Exponent10); ++Step)
						{
							Power *= 10.0;
						}
						Value = Exponent10 < 0 ? static_cast<double>(Mantissa) / Power : static_cast<double>(Mantissa) * Power;
					}
					else if (Exponent10 < -400)
					{
						Value = 0.0;
					}
					else if (Exponent10 > 400)
					{
						Value = std::numeric_limits<double>::infinity();
					}
					else
					{
						// Two steps keep 10^Exponent10 itself from under- or overflowing.
						const long long Half = Exponent10 / 2;
						Value = static_cast<double>(Mantissa) * std::pow(10.0, static_cast<double>(Half)) * std::pow(10.0, static_cast<double>(Exponent10 - Half));
					}
				}
				if (!std::isfinite(Value))
				{
					Pos = Start;
					return Fail("number out of range");
				}
				Out = FJsonNode::MakeReal(bNegative ? -Value : Value);
				return true;
			}

			const std::string& Text;
			int MaxDepth = 64;
			std::size_t Pos = 0;
		};

		void WriteString(std::string& Out, const std::string& Value)
		{
			Out += '"';
			for (const char C : Value)
			{
				switch (C)
				{
				case '"':
					Out += "\\\"";
					break;
				case '\\':
					Out += "\\\\";
					break;
				case '\n':
					Out += "\\n";
					break;
				case '\r':
					Out += "\\r";
					break;
				case '\t':
					Out += "\\t";
					break;
				default:
					if (static_cast<unsigned char>(C) < 0x20)
					{
						char Buffer[8];
						std::snprintf(Buffer, sizeof(Buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(C)));
						Out += Buffer;
					}
					else
					{
						Out += C;
					}
					break;
				}
			}
			Out += '"';
		}

		std::string FormatReal(double Value, int Precision)
		{
			char Buffer[64];
			std::snprintf(Buffer, sizeof(Buffer), "%.*g", Precision, Value);
			std::string Text(Buffer);
			// A locale with a decimal comma: JSON always uses a point.
			for (char& C : Text)
			{
				if (C == ',')
				{
					C = '.';
				}
			}
			return Text;
		}

		void WriteReal(std::string& Out, double Value)
		{
			if (!std::isfinite(Value))
			{
				Out += '0';
				return;
			}
			// The shortest of 15 and 17 significant digits that reads back as the same double.
			std::string Text = FormatReal(Value, 15);
			FJsonNode Back;
			if (!ParseJson(Text, Back) || Back.AsReal() != Value)
			{
				Text = FormatReal(Value, 17);
			}
			Out += Text;
		}

		void Indent(std::string& Out, int Depth)
		{
			Out += '\n';
			Out.append(static_cast<std::size_t>(Depth) * 2, ' ');
		}

		void WriteNode(std::string& Out, const FJsonNode& Node, bool bPretty, int Depth)
		{
			switch (Node.Type())
			{
			case FJsonNode::EType::Null:
				Out += "null";
				break;
			case FJsonNode::EType::Bool:
				Out += Node.AsBool() ? "true" : "false";
				break;
			case FJsonNode::EType::Integer:
				Out += std::to_string(Node.AsInt());
				break;
			case FJsonNode::EType::Real:
				WriteReal(Out, Node.AsReal());
				break;
			case FJsonNode::EType::String:
				WriteString(Out, Node.AsString());
				break;
			case FJsonNode::EType::Array:
			{
				const std::vector<FJsonNode>& Items = Node.Items();
				if (Items.empty())
				{
					Out += "[]";
					break;
				}
				// Arrays of scalars stay on one line ([1, 3]).
				bool bScalars = true;
				for (const FJsonNode& Item : Items)
				{
					bScalars = bScalars && !Item.IsArray() && !Item.IsObject();
				}
				Out += '[';
				for (std::size_t Index = 0; Index < Items.size(); ++Index)
				{
					if (Index > 0)
					{
						Out += bPretty && bScalars ? ", " : ",";
					}
					if (bPretty && !bScalars)
					{
						Indent(Out, Depth + 1);
					}
					WriteNode(Out, Items[Index], bPretty, Depth + 1);
				}
				if (bPretty && !bScalars)
				{
					Indent(Out, Depth);
				}
				Out += ']';
				break;
			}
			case FJsonNode::EType::Object:
			{
				const std::vector<std::string>& Keys = Node.Keys();
				const std::vector<FJsonNode>& Values = Node.Values();
				if (Keys.empty())
				{
					Out += "{}";
					break;
				}
				Out += '{';
				for (std::size_t Index = 0; Index < Keys.size(); ++Index)
				{
					if (Index > 0)
					{
						Out += ',';
					}
					if (bPretty)
					{
						Indent(Out, Depth + 1);
					}
					WriteString(Out, Keys[Index]);
					Out += bPretty ? ": " : ":";
					WriteNode(Out, Values[Index], bPretty, Depth + 1);
				}
				if (bPretty)
				{
					Indent(Out, Depth);
				}
				Out += '}';
				break;
			}
			}
		}
	}

	bool ParseJson(const std::string& Text, FJsonNode& OutNode, FJsonParseError* OutError, int MaxDepth)
	{
		JsonDetail::FReader Reader(Text, MaxDepth);
		FJsonNode Parsed;
		if (!Reader.ParseDocument(Parsed))
		{
			if (OutError != nullptr)
			{
				*OutError = Reader.Error;
			}
			return false;
		}
		OutNode = std::move(Parsed);
		return true;
	}

	std::string WriteJson(const FJsonNode& Node, bool bPretty)
	{
		std::string Out;
		JsonDetail::WriteNode(Out, Node, bPretty, 0);
		if (bPretty)
		{
			Out += '\n';
		}
		return Out;
	}
}
