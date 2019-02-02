#include <iostream>
#include <experimental/filesystem> // but recursive_directory_iterator, create_symlink, remove_all etc don't work in gcc6.3.0
#include "sqlite-amalgamation-3170000/sqlite3.h"
using namespace std; // can be a bad practice in bigger projects
using namespace experimental::filesystem; // can be a bad practice in bigger projects

string pepper("pepper");
string generate_salt();
string hash_password(string password, string salt); // salt as parameter and also uses pepper inside

static int callback(void* NotUsed, int argc, char** argv, char** azColName) {
	for (int i = 0; i < argc; ++i)
		printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
	printf("\n");
	return 0;
}

string root;
sqlite3* groupwareDB;
char* errMsg = 0;
int rc;
int lastAccountID;

void iterateDirectory(directory_iterator iter) {
	for (auto& p : iter) {
		cout << p << '\n';
		if (is_directory(p)) {
			if (!p.path().parent_path().compare(root)) {
				static int id(1); // TODO remove when not needed
				cout << p.path().filename() << '\n';
				string sqlQuery(string(
					"INSERT INTO CONTACT (CONTACT_ID, FULL_NAME, COMPANY)"
					"VALUES (") + to_string(id) + ", 'Lukas Salich', '<company>');" // not specified, what data to INSERT

					"INSERT INTO EMAIL_ADDRESS (EMAIL_ADDRESS_ID, ALIAS, DOMAIN, CONTACT)"
					"VALUES (" + to_string(id) + ", 'lazzo', 'seznam.cz', " + to_string(id) + ");" // not specified, what data to INSERT

					"INSERT INTO ACCOUNT (ACCOUNT_ID, USERNAME, PASSWORD_HASH, SALT, EMAIL_ADDRESS, CONTACT)"
					"VALUES (" + to_string(id) + ", '" + p.path().filename().string() + "', 'password_hash', 'salt', " + to_string(id) + ", " + to_string(id) + ");"

					"UPDATE CONTACT SET ACCOUNT = " + to_string(id) + " WHERE CONTACT_ID = " + to_string(id) + ";"
				);
				rc = sqlite3_exec(groupwareDB, sqlQuery.c_str(), callback, 0, &errMsg);
				if (rc != SQLITE_OK) {
					fprintf(stderr, "SQL error: %s\n", errMsg);
					sqlite3_free(errMsg);
				}
				lastAccountID = id; // TODO change value to last inserted ACCOUNT_ID instead of id
				++id; // TODO remove when not needed
			}
			else {
				string sqlQuery(string(
					"INSERT INTO EMAIL_FOLDER (NAME, EMAIL_FOLDER, ACCOUNT)"
					"VALUES ('") + p.path().filename().string() + "', '" + (p.path().parent_path().compare(root) ? p.path().parent_path().string() : "NULL") + "', " + to_string(lastAccountID) + ");"
				);
				rc = sqlite3_exec(groupwareDB, sqlQuery.c_str(), callback, 0, &errMsg);
				if (rc != SQLITE_OK) {
					fprintf(stderr, "SQL error: %s\n", errMsg);
					sqlite3_free(errMsg);
				}
			}
			iterateDirectory(directory_iterator(p)); // recursion => can be optimized
		}
		else if (is_regular_file(p) && p.path().extension() == ".imap")
			/* TODO read .imap file into db (parse header, store body) and if root then EMAIL_FOLDER = NULL */;
	}
}

int main(int argc, char * argv[]) { // TODO make more OOP and maybe multithreaded for faster crawling
	if (argc != 2) {
		fprintf(stdout, "Usage is \"groupwareConverter.exe <root>\".\n");
		return 1;
	}
	root = argv[1];
	if (!is_directory(root)) {
		fprintf(stdout, "%s is not a directory.\n", root.c_str());
		return 2;
	}

	remove("groupwareDB");

	rc = sqlite3_open("groupwareDB", &groupwareDB);
	if (rc) {
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(groupwareDB));
		sqlite3_close(groupwareDB);
		return 3;
	}
	string sqlQuery( // can be further improved
		"CREATE TABLE ACCOUNT("
		"ACCOUNT_ID INTEGER PRIMARY KEY,"
		"USERNAME TEXT UNIQUE NOT NULL,"
		"PASSWORD_HASH TEXT NOT NULL," // generated and compared with output from hash_password function 
		"SALT TEXT NOT NULL," // generate from generate_salt function, should be different for every user, but not needed to be unique
		"EMAIL_ADDRESS INTEGER UNIQUE NOT NULL REFERENCES EMAIL_ADDRESS(EMAIL_ADDRESS_ID) ON UPDATE CASCADE ON DELETE RESTRICT,"
		"CONTACT INTEGER UNIQUE NOT NULL REFERENCES CONTACT(CONTACT_ID) ON UPDATE CASCADE ON DELETE RESTRICT);"

		"CREATE TABLE EMAIL_FOLDER("
		"EMAIL_FOLDER_ID INTEGER PRIMARY KEY,"
		"NAME TEXT NOT NULL,"
		"EMAIL_FOLDER INTEGER REFERENCES EMAIL_FOLDER(EMAIL_FOLDER_ID) ON UPDATE CASCADE ON DELETE CASCADE,"
		"ACCOUNT INTEGER REFERENCES ACCOUNT(ACCOUNT_ID) ON UPDATE CASCADE ON DELETE SET NULL);"

		"CREATE TABLE EMAIL(" // can also reference ACCOUNT, depends
		"EMAIL_ID INTEGER PRIMARY KEY,"
		"META TEXT," // can be NOT NULL, depends
		"DATA TEXT," // can be NOT NULL, depends
		"SENDER TEXT,"
		"RECEIVER TEXT,"
		"CC TEXT,"
		"BCC TEXT,"
		"SUBJECT TEXT,"
		"EMAIL_FOLDER INTEGER REFERENCES EMAIL_FOLDER(EMAIL_FOLDER_ID) ON UPDATE CASCADE ON DELETE SET NULL);"

		"CREATE TABLE CALENDAR_ENTRY("
		"CALENDAR_ID INTEGER PRIMARY KEY,"
		"TITLE TEXT NOT NULL,"
		"START INTEGER NOT NULL," // SQLite doesn't have datetime type, but stores seconds since 1970-01-01 00:00:00 UTC
		"END INTEGER NOT NULL," // SQLite doesn't have datetime type, but stores seconds since 1970-01-01 00:00:00 UTC
		"DESCRIPTION TEXT,"
		"ACCOUNT INTEGER REFERENCES ACCOUNT(ACCOUNT_ID) ON UPDATE CASCADE ON DELETE SET NULL);" // can be NOT NULL, depends

		"CREATE TABLE CONTACT("
		"CONTACT_ID INTEGER PRIMARY KEY,"
		"FULL_NAME TEXT," // can be NOT NULL, depends
		"COMPANY TEXT," // can be NOT NULL, depends
		"HOME_ADDRESS INTEGER REFERENCES PHYSICAL_ADDRESS(ADDRESS_ID) ON UPDATE CASCADE ON DELETE SET NULL,"
		"BUSINESS_ADDRESS INTEGER REFERENCES PHYSICAL_ADDRESS(ADDRESS_ID) ON UPDATE CASCADE ON DELETE SET NULL,"
		"ACCOUNT INTEGER REFERENCES ACCOUNT(ACCOUNT_ID) ON UPDATE CASCADE ON DELETE SET NULL);"

		"CREATE TABLE PHYSICAL_ADDRESS(" // STREET+CITY+COUNTRY+ZIP are unique and can also make composite key
		"ADDRESS_ID INTEGER PRIMARY KEY,"
		"STREET TEXT,"
		"CITY TEXT," // can be forced to NOT NULL, depends
		"COUNTRY TEXT," // can be forced to NOT NULL, depends
		"ZIP INTEGER," // can be forced to NOT NULL, depends
		"CONSTRAINT u UNIQUE(STREET, CITY, COUNTRY, ZIP));" // maybe overkill, adds overhead, depends

		"CREATE TABLE EMAIL_ADDRESS("
		"EMAIL_ADDRESS_ID INTEGER PRIMARY KEY,"
		"ALIAS TEXT NOT NULL,"
		"DOMAIN TEXT NOT NULL," // from the description I suppose domain is "domain.com" and not "domain" or "com"
		"CONTACT INTEGER REFERENCES CONTACT(CONTACT_ID) ON UPDATE CASCADE ON DELETE SET NULL,"
		"CONSTRAINT u UNIQUE(ALIAS, DOMAIN));"
	);
	rc = sqlite3_exec(groupwareDB, sqlQuery.c_str(), callback, 0, &errMsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error: %s\n", errMsg);
		sqlite3_free(errMsg);
	}

	/* experimental/filesystem not working yet
	for (auto& p : recursive_directory_iterator(root))//we can also specify to "Follow rather than skip directory symlinks." and "Skip directories that would otherwise result in permission denied errors."
		cout << p << '\n'; */
	iterateDirectory(directory_iterator(root)); // because experimental/filesystem is limited, I have created own function for iteration

	sqlite3_close(groupwareDB);
}
