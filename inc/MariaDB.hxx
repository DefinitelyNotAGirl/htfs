#include <mariadb/mysql.h>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <stdint.hxx>

namespace MariaDB {
	typedef std::map<std::string,std::string> DatabaseRow;
	typedef std::vector<DatabaseRow> DatabaseFetchResult;

	enum class ConditionCode {
		equal,
		greater_or_equal,
		greater,
		lesser,
		lesser_or_equal,
		not_equal
	};

	class DatabaseFetchCondition {
	public:
		ConditionCode code;
		std::string column;
		std::string value;

		DatabaseFetchCondition(std::string column,ConditionCode code,std::string value)
			:column(column),code(code),value(value) {
		}

		std::string toString() {
			std::string ConditionExpression = "";
			switch(code) {
				case(ConditionCode::equal):ConditionExpression = "=";break;
				case(ConditionCode::not_equal):ConditionExpression = "!=";break;
				case(ConditionCode::greater):ConditionExpression = ">";break;
				case(ConditionCode::greater_or_equal):ConditionExpression = ">=";break;
				case(ConditionCode::lesser):ConditionExpression = "<";break;
				case(ConditionCode::lesser_or_equal):ConditionExpression = "<=";break;
			}
			return column+ConditionExpression+"'"+value+"'";
		}
	};

	inline std::string ConditionString(std::vector<DatabaseFetchCondition> Conditions) {
		if(Conditions.size() == 1) {
			return "WHERE "+Conditions[0].toString();
		} else {
			std::string Query = "WHERE "+Conditions[0].toString();
			for(u64 i = 1;i<Conditions.size();i++) {
				Query+=" AND "+Conditions[i].toString();
			}
			return Query;
		}
	}

	class Database;
	class QueryError : public std::runtime_error {
	public:
		Database& database;
		QueryError(std::string message,Database& database)
		:database(database),std::runtime_error(message) {
		}
	};

	class Database {
		MYSQL* connection{nullptr};
	public:
		Database(std::string host,std::string user,std::string password,std::string database) {
			connection = mysql_init(nullptr);
        	if (!connection) {
        	    throw std::runtime_error("mysql_init() failed");
        	}
        	if (!mysql_real_connect(connection, host.c_str(), user.c_str(), password.c_str(), database.c_str(), 0, nullptr, 0)) {
        	    throw std::runtime_error(mysql_error(connection));
        	}
		}

		std::vector<DatabaseRow> Query(std::string query) {
			std::cout << "running query: " << query << std::endl;
			if (mysql_query(connection, query.c_str())) {
        	    throw std::runtime_error(mysql_error(connection));
        	}
        	MYSQL_RES* result = mysql_store_result(connection);
        	if(result) {
				int numFields = mysql_num_fields(result);
        		MYSQL_ROW row;
				std::vector<DatabaseRow> Rows;
				std::vector<std::string> fieldNames;
        		MYSQL_FIELD* fields = mysql_fetch_fields(result);
        		for (int i = 0; i < numFields; ++i) {
        		    fieldNames.emplace_back(fields[i].name);
        		}
				while((row = mysql_fetch_row(result))) {
					Rows.push_back(DatabaseRow());
        		    for (int i = 0; i < numFields; i++) {
						Rows.back().insert({
							{fieldNames[i],row[i] ? row[i] : "NULL"}
						});
        		    }
        		}
				return Rows;
        	}
			if(mysql_affected_rows(connection) != (my_ulonglong)-1) {
				return std::vector<DatabaseRow>();
        	}
			throw QueryError(mysql_error(connection),*this);
		};
	};

	class Table_Basic {
		friend class Object;
		Database& database;
		std::string Name;
	public:
		Table_Basic(std::string Name,Database& database)
		:database(database),Name(Name) {
		}
		virtual DatabaseFetchResult FetchAll() {
			return database.Query("SELECT * FROM "+Name);
		}

		virtual DatabaseFetchResult Fetch(std::vector<DatabaseFetchCondition> Conditions) {
			if(Conditions.size() == 0) {
				return FetchAll();
			}
			if(Conditions.size() == 1) {
				return database.Query("SELECT * FROM "+Name+" WHERE "+Conditions[0].toString());
			} else {
				std::string Query = "SELECT * FROM "+Name+" WHERE "+Conditions[0].toString();
				for(u64 i = 1;i<Conditions.size();i++) {
					Query+=" AND "+Conditions[i].toString();
				}
				return database.Query(Query);
			}
		}
	};

	class Object {
	protected:
		Table_Basic& table;
	public:
		std::vector<DatabaseFetchCondition> Selector;
	public:
		Object(Table_Basic& table)
		:table(table) {
		}
		virtual std::string toSQLSet() {
			throw std::runtime_error("MariaDB::Object::toSQLSet function not overwritten");
		}
		virtual std::string toSQLInsert() {
			throw std::runtime_error("MariaDB::Object::toSQLInsert function not overwritten");
		}
		virtual void Insert() {
			std::string Query = "INSERT INTO "+table.Name+" "+toSQLInsert();
			table.database.Query(Query);
		}
		virtual void Update() {
			std::string Query = "UPDATE "+table.Name+" "+toSQLSet()+" "+ConditionString(Selector);
			table.database.Query(Query);
		}
		virtual void persist() {
			if(Selector.size() == 0) {
				Insert();
			} else {
				Update();
			}
		}
		virtual void Delete() {
			std::string Query = "DELETE FROM "+table.Name+" "+ConditionString(Selector);
			table.database.Query(Query);
		}
	};

	template<typename RowType,typename KeyType>
	class Table : public Table_Basic {
	public:
		using Table_Basic::Table_Basic;
		virtual std::vector<RowType> GetAll() {
			DatabaseFetchResult Rows = Table_Basic::FetchAll();
			std::vector<RowType> Items;
			for(DatabaseRow Row : Rows) {
				Items.push_back(RowType(Row));
				for(DatabaseRow::value_type& column : Row) {
					Items.back().Selector.push_back(DatabaseFetchCondition(
						column.first,ConditionCode::equal,column.second
					));
				}
			}
			return Items;
		}

		virtual std::vector<RowType> Get(std::vector<DatabaseFetchCondition> Conditions) {
			DatabaseFetchResult Rows = Table_Basic::Fetch(Conditions);
			std::vector<RowType> Items;
			for(DatabaseRow Row : Rows) {
				Items.push_back(RowType(Row));
				for(DatabaseRow::value_type& column : Row) {
					Items.back().Selector.push_back(DatabaseFetchCondition(
						column.first,ConditionCode::equal,column.second
					));
				}
			}
			return Items;
		}

		/**
		 * @brief inserts or updates data in the table
		 */
		virtual void Write(std::vector<RowType> data) {
			for(RowType& row : data) {
				row.persist();
			}
		}
	};
}
