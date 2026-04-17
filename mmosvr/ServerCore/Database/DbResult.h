#pragma once


class DbResult
{
public:
	DbResult() = default;
	explicit DbResult(nanodbc::result res);
	explicit DbResult(int32 affectedRows);

	bool Next();

	// 인덱스 기반 접근
	int32 GetInt32(short col) const;
	int64 GetInt64(short col) const;
	float GetFloat(short col) const;
	std::string GetString(short col) const;
	bool IsNull(short col) const;

	// 이름 기반 접근
	int32 GetInt32(const std::string& colName) const;
	int64 GetInt64(const std::string& colName) const;
	float GetFloat(const std::string& colName) const;
	std::string GetString(const std::string& colName) const;
	bool IsNull(const std::string& colName) const;

	bool NextResultSet();  // SP가 반환한 다음 결과셋으로 이동

	bool IsValid() const { return valid_; }
	int32 AffectedRows() const { return affectedRows_; }

private:
	nanodbc::result result_;
	bool valid_ = false;
	int32 affectedRows_ = 0;
};

