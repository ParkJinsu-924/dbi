#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <fstream>


namespace Csv
{
	// ── Row ─────────────────────────────────────────────────────────
	// One parsed row: header-name -> string-value.
	// GetOr<T>() converts to the requested type; returns defaultValue
	// on missing key, empty cell, or conversion failure.

	struct Row
	{
		std::unordered_map<std::string, std::string> fields;

		template<typename T>
		T GetOr(const std::string& key, const T& defaultValue) const
		{
			auto it = fields.find(key);
			if (it == fields.end() || it->second.empty())
				return defaultValue;

			try
			{
				if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int32_t>)
					return static_cast<T>(std::stoi(it->second));
				else if constexpr (std::is_same_v<T, long long> || std::is_same_v<T, int64_t>)
					return static_cast<T>(std::stoll(it->second));
				else if constexpr (std::is_same_v<T, float>)
					return std::stof(it->second);
				else if constexpr (std::is_same_v<T, double>)
					return std::stod(it->second);
				else if constexpr (std::is_same_v<T, bool>)
				{
					const auto& v = it->second;
					return v == "1" || v == "true" || v == "TRUE" || v == "True";
				}
				else if constexpr (std::is_same_v<T, std::string>)
					return it->second;
				else
					return defaultValue;
			}
			catch (...)
			{
				return defaultValue;
			}
		}
	};

	// ── Table ───────────────────────────────────────────────────────

	struct Table
	{
		std::vector<std::string> headers;
		std::vector<Row> rows;

		bool Empty() const { return rows.empty(); }
		size_t Size() const { return rows.size(); }
	};

	// ── Parsing helpers ─────────────────────────────────────────────

	namespace detail
	{
		// Split a CSV line respecting quoted fields (RFC 4180).
		inline std::vector<std::string> SplitLine(const std::string& line)
		{
			std::vector<std::string> result;
			std::string current;
			bool inQuotes = false;

			for (size_t i = 0; i < line.size(); ++i)
			{
				const char c = line[i];
				if (inQuotes)
				{
					if (c == '"')
					{
						// Escaped quote ""
						if (i + 1 < line.size() && line[i + 1] == '"')
						{
							current += '"';
							++i;
						}
						else
						{
							inQuotes = false;
						}
					}
					else
					{
						current += c;
					}
				}
				else
				{
					if (c == '"')
						inQuotes = true;
					else if (c == ',')
					{
						result.push_back(current);
						current.clear();
					}
					else
					{
						current += c;
					}
				}
			}
			result.push_back(current);
			return result;
		}

		// Trim whitespace from both ends.
		inline std::string Trim(const std::string& s)
		{
			const auto begin = s.find_first_not_of(" \t\r\n");
			if (begin == std::string::npos)
				return {};
			const auto end = s.find_last_not_of(" \t\r\n");
			return s.substr(begin, end - begin + 1);
		}
	}

	// ── FOR_EACH macro engine ───────────────────────────────────────
	// Applies a macro to each variadic argument (up to 20 fields).
}

#define CSV_FIELD_(obj, row, field) obj.field = row.GetOr(#field, obj.field);

#define CSV_FE_1(obj,row,a)                                CSV_FIELD_(obj,row,a)
#define CSV_FE_2(obj,row,a,...)  CSV_FIELD_(obj,row,a)     CSV_FE_1(obj,row,__VA_ARGS__)
#define CSV_FE_3(obj,row,a,...)  CSV_FIELD_(obj,row,a)     CSV_FE_2(obj,row,__VA_ARGS__)
#define CSV_FE_4(obj,row,a,...)  CSV_FIELD_(obj,row,a)     CSV_FE_3(obj,row,__VA_ARGS__)
#define CSV_FE_5(obj,row,a,...)  CSV_FIELD_(obj,row,a)     CSV_FE_4(obj,row,__VA_ARGS__)
#define CSV_FE_6(obj,row,a,...)  CSV_FIELD_(obj,row,a)     CSV_FE_5(obj,row,__VA_ARGS__)
#define CSV_FE_7(obj,row,a,...)  CSV_FIELD_(obj,row,a)     CSV_FE_6(obj,row,__VA_ARGS__)
#define CSV_FE_8(obj,row,a,...)  CSV_FIELD_(obj,row,a)     CSV_FE_7(obj,row,__VA_ARGS__)
#define CSV_FE_9(obj,row,a,...)  CSV_FIELD_(obj,row,a)     CSV_FE_8(obj,row,__VA_ARGS__)
#define CSV_FE_10(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_9(obj,row,__VA_ARGS__)
#define CSV_FE_11(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_10(obj,row,__VA_ARGS__)
#define CSV_FE_12(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_11(obj,row,__VA_ARGS__)
#define CSV_FE_13(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_12(obj,row,__VA_ARGS__)
#define CSV_FE_14(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_13(obj,row,__VA_ARGS__)
#define CSV_FE_15(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_14(obj,row,__VA_ARGS__)
#define CSV_FE_16(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_15(obj,row,__VA_ARGS__)
#define CSV_FE_17(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_16(obj,row,__VA_ARGS__)
#define CSV_FE_18(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_17(obj,row,__VA_ARGS__)
#define CSV_FE_19(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_18(obj,row,__VA_ARGS__)
#define CSV_FE_20(obj,row,a,...) CSV_FIELD_(obj,row,a)     CSV_FE_19(obj,row,__VA_ARGS__)

#define CSV_GET_MACRO_(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,_17,_18,_19,_20,NAME,...) NAME
#define CSV_FOR_EACH_(obj,row,...) CSV_GET_MACRO_(__VA_ARGS__,\
	CSV_FE_20,CSV_FE_19,CSV_FE_18,CSV_FE_17,CSV_FE_16,CSV_FE_15,CSV_FE_14,CSV_FE_13,\
	CSV_FE_12,CSV_FE_11,CSV_FE_10,CSV_FE_9,CSV_FE_8,CSV_FE_7,CSV_FE_6,CSV_FE_5,\
	CSV_FE_4,CSV_FE_3,CSV_FE_2,CSV_FE_1)(obj,row,__VA_ARGS__)

// ── CSV_DEFINE_TYPE ─────────────────────────────────────────────
// Usage (inside struct): CSV_DEFINE_TYPE(Type, field1, field2, ...)
// Generates: static Type FromCsv(const Csv::Row& row)
// Each field is read by its name (stringified) with the struct's
// default value as fallback — missing/empty CSV columns are safe.

#define CSV_DEFINE_TYPE(Type, ...) \
	static Type FromCsv(const Csv::Row& row) { \
		Type t; \
		CSV_FOR_EACH_(t, row, __VA_ARGS__) \
		return t; \
	}

namespace Csv
{
	// ── LoadFile ────────────────────────────────────────────────────
	// Load a CSV file with header row.  Returns empty Table on failure.
	// Lines starting with '#' are treated as comments and skipped.

	inline Table LoadFile(const std::string& filePath)
	{
		Table table;

		std::ifstream file(filePath);
		if (!file.is_open())
			return table;

		std::string line;

		// Skip comment/empty lines, find header
		while (std::getline(file, line))
		{
			line = detail::Trim(line);
			if (line.empty() || line[0] == '#')
				continue;
			break;
		}

		if (line.empty())
			return table;

		// Parse header
		table.headers = detail::SplitLine(line);
		for (auto& h : table.headers)
			h = detail::Trim(h);

		// Parse data rows
		while (std::getline(file, line))
		{
			line = detail::Trim(line);
			if (line.empty() || line[0] == '#')
				continue;

			auto values = detail::SplitLine(line);
			Row row;
			for (size_t i = 0; i < table.headers.size(); ++i)
			{
				std::string val = (i < values.size()) ? detail::Trim(values[i]) : "";
				row.fields[table.headers[i]] = std::move(val);
			}
			table.rows.push_back(std::move(row));
		}

		return table;
	}
}
