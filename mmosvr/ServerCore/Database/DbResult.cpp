#include "pch.h"
#include "Database/DbResult.h"


DbResult::DbResult(nanodbc::result res)
	: result_(std::move(res)), valid_(true)
	, affectedRows_(static_cast<int32>(result_.affected_rows()))
{
}

DbResult::DbResult(int32 affectedRows)
	: affectedRows_(affectedRows)
{
}

bool DbResult::Next()
{
	if (!valid_) return false;
	return result_.next();
}

bool DbResult::NextResultSet()
{
	if (!valid_) return false;
	return result_.next_result();
}

// 인덱스 기반 접근
int32 DbResult::GetInt32(short col) const { return result_.get<int32>(col); }
int64 DbResult::GetInt64(short col) const { return result_.get<int64>(col); }
float DbResult::GetFloat(short col) const { return result_.get<float>(col); }
std::string DbResult::GetString(short col) const { return result_.get<std::string>(col); }
bool DbResult::IsNull(short col) const { return result_.is_null(col); }

// 이름 기반 접근
int32 DbResult::GetInt32(const std::string& colName) const { return result_.get<int32>(colName); }
int64 DbResult::GetInt64(const std::string& colName) const { return result_.get<int64>(colName); }
float DbResult::GetFloat(const std::string& colName) const { return result_.get<float>(colName); }
std::string DbResult::GetString(const std::string& colName) const { return result_.get<std::string>(colName); }
bool DbResult::IsNull(const std::string& colName) const { return result_.is_null(colName); }

