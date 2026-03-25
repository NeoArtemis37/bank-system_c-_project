#include "crow/crow.h"
#include <fstream>
#include <streambuf>
#include <sqlite3.h>
#include <iostream>
using namespace std;
#include <map>
#include <sstream>
#include <string>

// In-memory session store (for simplicity, consider using a more robust solution for production)
std::map<std::string, int> session_store;

std::string generate_session_id() {
    // Generate a unique session ID (e.g., UUID or a random string)
    // For simplicity, we will return a fixed session ID. In production, use a proper random string generator.
    return "unique_session_id_" + std::to_string(session_store.size() + 1);
}

void store_session(int user_id, const std::string& session_id) {
    // Store the session ID and associated user ID in the session store
    session_store[session_id] = user_id;
}

bool validate_session(const std::string& session_id, int& user_id) {
    // Validate the session ID and retrieve the associated user ID
    auto it = session_store.find(session_id);
    if (it != session_store.end()) {
        user_id = it->second;
        return true;
    }
    return false;
}



// Function to parse form data from the request body
std::map<std::string, std::string> parse_form_data(const std::string& body) {
    std::map<std::string, std::string> form_data;
    std::stringstream ss(body);
    std::string item;

    while (std::getline(ss, item, '&')) {
        size_t pos = item.find('=');
        if (pos != std::string::npos) {
            std::string key = item.substr(0, pos);
            std::string value = item.substr(pos + 1);
            form_data[key] = value;
        }
    }
    return form_data;
}

// Function to load HTML files
std::string load_file(const std::string& filename) {
    std::ifstream t(filename);
    return std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
}


//check the existence of db and create one if not 

void initialize_database() {
    sqlite3* DB;
    int exit = sqlite3_open("securebank.db", &DB);

    std::string sql_users = "CREATE TABLE IF NOT EXISTS USERS ("
                            "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
                            "USERNAME TEXT NOT NULL, "
                            "PASSWORD TEXT NOT NULL, "
                            "BALANCE REAL NOT NULL DEFAULT 0.0, "
                            "LOGIN_DATE TEXT NOT NULL);";
    
    std::string sql_transactions = "CREATE TABLE IF NOT EXISTS TRANSACTIONS ("
                                   "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
                                   "USER_ID INTEGER NOT NULL, "
                                   "DATE TEXT NOT NULL, "
                                   "AMOUNT REAL NOT NULL, "
                                   "TYPE TEXT NOT NULL, "
                                   "FOREIGN KEY(USER_ID) REFERENCES USERS(ID));";

    char* messageError;
    exit = sqlite3_exec(DB, sql_users.c_str(), NULL, 0, &messageError);
    if (exit != SQLITE_OK) {
        std::cerr << "Error creating USERS table: " << messageError << std::endl;
        sqlite3_free(messageError);
    }

    exit = sqlite3_exec(DB, sql_transactions.c_str(), NULL, 0, &messageError);
    if (exit != SQLITE_OK) {
        std::cerr << "Error creating TRANSACTIONS table: " << messageError << std::endl;
        sqlite3_free(messageError);
    }

    sqlite3_close(DB);
}



// Function to execute an SQL statement
bool execute_sql(const std::string& sql) {
    sqlite3* DB;
    int exit = sqlite3_open("securebank.db", &DB);

    char* messageError;
    exit = sqlite3_exec(DB, sql.c_str(), NULL, 0, &messageError);
    
    if (exit != SQLITE_OK) {
        std::cerr << "SQL error: " << messageError << std::endl;
        sqlite3_free(messageError);
        sqlite3_close(DB);
        return false;
    }
    
    sqlite3_close(DB);
    return true;
}

// Function to check if a user exists
bool user_exists(const std::string& username) {
    sqlite3* DB;
    sqlite3_stmt* stmt;
    int exit = sqlite3_open("securebank.db", &DB);
    
    std::string sql = "SELECT 1 FROM USERS WHERE USERNAME = ?;";
    sqlite3_prepare_v2(DB, sql.c_str(), -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    
    int step = sqlite3_step(stmt);
    bool exists = (step == SQLITE_ROW);
    
    sqlite3_finalize(stmt);
    sqlite3_close(DB);
    return exists;
}

// Function to verify user credentials
bool verify_user(const std::string& username, const std::string& password, int &user_id) {
    sqlite3* DB;
    sqlite3_stmt* stmt;
    int exit = sqlite3_open("securebank.db", &DB);
    
    std::string sql = "SELECT ID FROM USERS WHERE USERNAME = ? AND PASSWORD = ?;";
    sqlite3_prepare_v2(DB, sql.c_str(), -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);
    
    int step = sqlite3_step(stmt);
    bool verified = false;
    if (step == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
        verified = true;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(DB);
    return verified;
}



bool deposit_funds(int user_id, double amount) {
    CROW_LOG_INFO << "Starting deposit_funds for user_id: " << user_id << " amount: " << amount;

    sqlite3* DB;
    int exit = sqlite3_open("securebank.db", &DB);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to open database: " << sqlite3_errmsg(DB);
        sqlite3_close(DB);
        return false;
    }

    std::string sql_update_balance = "UPDATE USERS SET BALANCE = BALANCE + " + std::to_string(amount) + " WHERE ID = " + std::to_string(user_id) + ";";
    CROW_LOG_INFO << "Executing SQL: " << sql_update_balance;

    char* messageError = nullptr;
    exit = sqlite3_exec(DB, sql_update_balance.c_str(), NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to update balance: " << messageError;
        sqlite3_free(messageError);
        sqlite3_close(DB);
        return false;
    }

    std::string sql_insert_transaction = "INSERT INTO TRANSACTIONS (USER_ID, DATE, AMOUNT, TYPE) VALUES (" + std::to_string(user_id) + ", datetime('now'), " + std::to_string(amount) + ", 'deposit');";
    CROW_LOG_INFO << "Executing SQL: " << sql_insert_transaction;

    exit = sqlite3_exec(DB, sql_insert_transaction.c_str(), NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to insert transaction: " << messageError;
        sqlite3_free(messageError);
        sqlite3_close(DB);
        return false;
    }

    sqlite3_close(DB);
    CROW_LOG_INFO << "Deposit_funds completed successfully for user_id: " << user_id;
    return true;
}


bool has_sufficient_balance(int user_id, double amount) {
    sqlite3* DB;
    sqlite3_stmt* stmt;
    int exit = sqlite3_open("securebank.db", &DB);
    
    std::string sql = "SELECT BALANCE FROM USERS WHERE ID = " + std::to_string(user_id) + ";";
    sqlite3_prepare_v2(DB, sql.c_str(), -1, &stmt, 0);
    
    double balance = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        balance = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(DB);
    
    return balance >= amount;
}

bool withdraw_funds(int user_id, double amount) {
    CROW_LOG_INFO << "Starting withdraw_funds for user_id: " << user_id << " amount: " << amount;

    sqlite3* DB;
    int exit = sqlite3_open("securebank.db", &DB);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to open database: " << sqlite3_errmsg(DB);
        sqlite3_close(DB);
        return false;
    }

    std::string sql_update_balance = "UPDATE USERS SET BALANCE = BALANCE - " + std::to_string(amount) + " WHERE ID = " + std::to_string(user_id) + ";";
    CROW_LOG_INFO << "Executing SQL: " << sql_update_balance;

    char* messageError = nullptr;
    exit = sqlite3_exec(DB, sql_update_balance.c_str(), NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to update balance: " << messageError;
        sqlite3_free(messageError);
        sqlite3_close(DB);
        return false;
    }

    std::string sql_insert_transaction = "INSERT INTO TRANSACTIONS (USER_ID, DATE, AMOUNT, TYPE) VALUES (" + std::to_string(user_id) + ", datetime('now'), " + std::to_string(amount) + ", 'withdrawal');";
    CROW_LOG_INFO << "Executing SQL: " << sql_insert_transaction;

    exit = sqlite3_exec(DB, sql_insert_transaction.c_str(), NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to insert transaction: " << messageError;
        sqlite3_free(messageError);
        sqlite3_close(DB);
        return false;
    }

    sqlite3_close(DB);
    CROW_LOG_INFO << "withdraw_funds completed successfully for user_id: " << user_id;
    return true;
}


bool get_user_id_by_username(const std::string& username, int& user_id) {
    sqlite3* DB;
    sqlite3_stmt* stmt;
    int exit = sqlite3_open("securebank.db", &DB);

    std::string sql = "SELECT ID FROM USERS WHERE USERNAME = '" + username + "';";
    sqlite3_prepare_v2(DB, sql.c_str(), -1, &stmt, 0);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        sqlite3_close(DB);
        return true;
    } else {
        sqlite3_finalize(stmt);
        sqlite3_close(DB);
        return false;
    }
}
bool transfer_funds(int from_user_id, int to_user_id, double amount) {
    CROW_LOG_INFO << "Starting transfer_funds for from_user_id: " << from_user_id << " to_user_id: " << to_user_id << " amount: " << amount;

    sqlite3* DB;
    int exit = sqlite3_open("securebank.db", &DB);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to open database: " << sqlite3_errmsg(DB);
        sqlite3_close(DB);
        return false;
    }

    // Start transaction
    char* messageError = nullptr;
    exit = sqlite3_exec(DB, "BEGIN TRANSACTION;", NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to begin transaction: " << messageError;
        sqlite3_free(messageError);
        sqlite3_close(DB);
        return false;
    }

    // Deduct amount from sender's balance
    std::string sql_withdraw = "UPDATE USERS SET BALANCE = BALANCE - " + std::to_string(amount) + " WHERE ID = " + std::to_string(from_user_id) + ";";
    exit = sqlite3_exec(DB, sql_withdraw.c_str(), NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to withdraw funds: " << messageError;
        sqlite3_free(messageError);
        sqlite3_exec(DB, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(DB);
        return false;
    }

    // Add amount to recipient's balance
    std::string sql_deposit = "UPDATE USERS SET BALANCE = BALANCE + " + std::to_string(amount) + " WHERE ID = " + std::to_string(to_user_id) + ";";
    exit = sqlite3_exec(DB, sql_deposit.c_str(), NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to deposit funds: " << messageError;
        sqlite3_free(messageError);
        sqlite3_exec(DB, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(DB);
        return false;
    }

    // Log transaction for sender
    std::string sql_insert_transaction_withdraw = "INSERT INTO TRANSACTIONS (USER_ID, DATE, AMOUNT, TYPE) VALUES (" + std::to_string(from_user_id) + ", datetime('now'), " + std::to_string(amount) + ", 'transfer out');";
    exit = sqlite3_exec(DB, sql_insert_transaction_withdraw.c_str(), NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to log withdrawal transaction: " << messageError;
        sqlite3_free(messageError);
        sqlite3_exec(DB, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(DB);
        return false;
    }

    // Log transaction for recipient
    std::string sql_insert_transaction_deposit = "INSERT INTO TRANSACTIONS (USER_ID, DATE, AMOUNT, TYPE) VALUES (" + std::to_string(to_user_id) + ", datetime('now'), " + std::to_string(amount) + ", 'transfer in');";
    exit = sqlite3_exec(DB, sql_insert_transaction_deposit.c_str(), NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to log deposit transaction: " << messageError;
        sqlite3_free(messageError);
        sqlite3_exec(DB, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(DB);
        return false;
    }

    // Commit transaction
    exit = sqlite3_exec(DB, "COMMIT;", NULL, NULL, &messageError);
    if (exit != SQLITE_OK) {
        CROW_LOG_ERROR << "Failed to commit transaction: " << messageError;
        sqlite3_free(messageError);
        sqlite3_exec(DB, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(DB);
        return false;
    }

    sqlite3_close(DB);
    CROW_LOG_INFO << "transfer_funds completed successfully for from_user_id: " << from_user_id << " to_user_id: " << to_user_id;
    return true;
}



int main() {
    initialize_database();
    crow::SimpleApp app;


    // Route for default page render
    CROW_ROUTE(app, "/")
    .methods("GET"_method)([](const crow::request& req){
        return load_file("./templates/index.html");
    });

    // Route for registration page (GET)
    CROW_ROUTE(app, "/register")
    .methods("GET"_method)([](const crow::request& req) {
        cout<<" register file called ";
        return load_file("./templates/register.html");
    });

    // Handle registration form submission (POST)
    CROW_ROUTE(app, "/register").methods("POST"_method)([](const crow::request& req) {
        auto form_data = parse_form_data(req.body);
        if (form_data.find("username") == form_data.end() || form_data.find("password") == form_data.end()) {
            return crow::response(400, "Invalid input");
        }

        std::string username = form_data["username"];
        std::string password = form_data["password"];

        if (user_exists(username)) {
            return crow::response(400, "Username already exists");
        }

        // Updated SQL to include login date
        std::string sql = "INSERT INTO USERS (USERNAME, PASSWORD, LOGIN_DATE) VALUES ('" + username + "', '" + password + "', datetime('now'));";
        if (execute_sql(sql)) {
            crow::response res(302);
            res.set_header("Location", "/login");
            return res;
        } else {
            return crow::response(500, "Failed to register user");
        }
    });


    CROW_ROUTE(app, "/login")
    .methods("GET"_method)([](const crow::request& req) {
        return load_file("./templates/login.html");
    });

    CROW_ROUTE(app, "/login").methods("POST"_method)([](const crow::request& req) {
    auto form_data = parse_form_data(req.body);
    if (form_data.find("username") == form_data.end() || form_data.find("password") == form_data.end()) {
        return crow::response(400, "Invalid input");
    }

    std::string username = form_data["username"];
    std::string password = form_data["password"];
    int user_id;

    if (verify_user(username, password, user_id)) {
        // Generate a session ID
        std::string session_id = generate_session_id();
        // Store the session ID with the associated user ID
        store_session(user_id, session_id);

        // Set the session ID as a cookie in the response
        crow::response res(302);
        res.set_header("Set-Cookie", "session_id=" + session_id + "; HttpOnly");
        res.set_header("Location", "/dashboard");
        return res;
    } else {
        return crow::response(401, "Invalid username or password");
    }
});



     CROW_ROUTE(app, "/dashboard")
    ([](const crow::request& req, crow::response& res) {
        std::string session_id;
        if (req.get_header_value("Cookie").find("session_id=") != std::string::npos) {
            session_id = req.get_header_value("Cookie").substr(req.get_header_value("Cookie").find("session_id=") + 11);
        } else {
            res.code = 401;
            res.body = "Unauthorized";
            res.end();
            return;
        }

        int user_id;
        if (!validate_session(session_id, user_id)) {
            res.code = 401;
            res.body = "Unauthorized";
            res.end();
            return;
        }

        crow::mustache::context ctx;
        ctx["user_id"] = user_id;

        // Fetch user data
        sqlite3* DB;
        sqlite3_stmt* stmt;
        int exit = sqlite3_open("securebank.db", &DB);

        std::string sql_user = "SELECT USERNAME, BALANCE FROM USERS WHERE ID = " + std::to_string(user_id) + ";";
        sqlite3_prepare_v2(DB, sql_user.c_str(), -1, &stmt, 0);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            double balance = sqlite3_column_double(stmt, 1);
            ctx["username"] = username;
            ctx["balance"] = balance;
        }
        sqlite3_finalize(stmt);

        // Fetch last 10 transactions
        std::string sql_transactions = "SELECT DATE, AMOUNT, TYPE FROM TRANSACTIONS WHERE USER_ID = " + std::to_string(user_id) + " ORDER BY DATE DESC LIMIT 10;";
        sqlite3_prepare_v2(DB, sql_transactions.c_str(), -1, &stmt, 0);
        crow::json::wvalue::list transactions;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue transaction;
            transaction["date"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            transaction["amount"] = sqlite3_column_double(stmt, 1);
            transaction["type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            transactions.push_back(std::move(transaction));
        }
        ctx["transactions"] = std::move(transactions);
        sqlite3_finalize(stmt);
        sqlite3_close(DB);

        // Render the dashboard template
        auto page = crow::mustache::load("dashboard.html");
        res.code = 200;
        res.body = page.render(ctx).dump();
        res.end();
    });

    CROW_ROUTE(app, "/deposit").methods("POST"_method)([](const crow::request& req) {
    auto form_data = parse_form_data(req.body);
    if (form_data.find("user_id") == form_data.end() || form_data.find("amount") == form_data.end()) {
        CROW_LOG_ERROR << "Invalid input: Missing user_id or amount";
        return crow::response(400, "Invalid input");
    }

    std::string user_id_str = form_data["user_id"];
    std::string amount_str = form_data["amount"];

    CROW_LOG_INFO << "Received deposit request: user_id = " << user_id_str << ", amount = " << amount_str;

    // Log what we have received to check for issues
    CROW_LOG_INFO << "user_id (raw): " << user_id_str;
    CROW_LOG_INFO << "amount (raw): " << amount_str;

    int user_id = 0;
    double amount = 0.0;

    try {
        user_id = std::stoi(user_id_str);
    } catch (const std::invalid_argument& e) {
        CROW_LOG_ERROR << "Invalid user_id: " << user_id_str;
        return crow::response(400, "Invalid user_id");
    } catch (const std::out_of_range& e) {
        CROW_LOG_ERROR << "user_id out of range: " << user_id_str;
        return crow::response(400, "user_id out of range");
    }

    try {
        amount = std::stod(amount_str);
    } catch (const std::invalid_argument& e) {
        CROW_LOG_ERROR << "Invalid amount: " << amount_str;
        return crow::response(400, "Invalid amount");
    } catch (const std::out_of_range& e) {
        CROW_LOG_ERROR << "Amount out of range: " << amount_str;
        return crow::response(400, "Amount out of range");
    }

    if (amount <= 0) {
        CROW_LOG_ERROR << "Invalid input: Amount must be greater than zero";
        return crow::response(400, "Invalid input: Amount must be greater than zero");
    }

    CROW_LOG_INFO << "Processing deposit for user_id: " << user_id << " amount: " << amount;

    if (deposit_funds(user_id, amount)) {
        CROW_LOG_INFO << "Deposit successful";
        return crow::response(200, "Deposit successful");
    } else {
        CROW_LOG_ERROR << "Failed to deposit funds";
        return crow::response(500, "Failed to deposit funds");
    }
});


    CROW_ROUTE(app, "/withdraw").methods("POST"_method)([](const crow::request& req) {
    auto form_data = parse_form_data(req.body);
    if (form_data.find("user_id") == form_data.end() || form_data.find("amount") == form_data.end()) {
        CROW_LOG_ERROR << "Invalid input: Missing user_id or amount";
        return crow::response(400, "Invalid input");
    }

    std::string user_id_str = form_data["user_id"];
    std::string amount_str = form_data["amount"];

    CROW_LOG_INFO << "Received withdrawal request: user_id = " << user_id_str << ", amount = " << amount_str;

    int user_id;
    double amount;

    try {
        user_id = std::stoi(user_id_str);
    } catch (const std::invalid_argument& e) {
        CROW_LOG_ERROR << "Invalid user_id: " << user_id_str;
        return crow::response(400, "Invalid user_id");
    } catch (const std::out_of_range& e) {
        CROW_LOG_ERROR << "user_id out of range: " << user_id_str;
        return crow::response(400, "user_id out of range");
    }

    try {
        amount = std::stod(amount_str);
    } catch (const std::invalid_argument& e) {
        CROW_LOG_ERROR << "Invalid amount: " << amount_str;
        return crow::response(400, "Invalid amount");
    } catch (const std::out_of_range& e) {
        CROW_LOG_ERROR << "Amount out of range: " << amount_str;
        return crow::response(400, "Amount out of range");
    }

    if (amount <= 0) {
        CROW_LOG_ERROR << "Invalid input: Amount must be greater than zero";
        return crow::response(400, "Invalid input: Amount must be greater than zero");
    }

    // Verify that the user has enough balance
    if (!has_sufficient_balance(user_id, amount)) {
        CROW_LOG_ERROR << "Insufficient funds for user_id: " << user_id;
        return crow::response(400, "Insufficient funds");
    }

    CROW_LOG_INFO << "Processing withdrawal for user_id: " << user_id << " amount: " << amount;

    if (withdraw_funds(user_id, amount)) {
        CROW_LOG_INFO << "Withdrawal successful";
        return crow::response(200, "Withdrawal successful");
    } else {
        CROW_LOG_ERROR << "Failed to withdraw funds";
        return crow::response(500, "Failed to withdraw funds");
    }
});

   CROW_ROUTE(app, "/transfer").methods("POST"_method)([](const crow::request& req) {
    std::string session_id;
    if (req.get_header_value("Cookie").find("session_id=") != std::string::npos) {
        session_id = req.get_header_value("Cookie").substr(req.get_header_value("Cookie").find("session_id=") + 11);
    } else {
        return crow::response(401, "Unauthorized");
    }

    int from_user_id;
    if (!validate_session(session_id, from_user_id)) {
        return crow::response(401, "Unauthorized");
    }

    auto form_data = parse_form_data(req.body);
    if (form_data.find("to_username") == form_data.end() || form_data.find("amount") == form_data.end()) {
        return crow::response(400, "Invalid input");
    }

    std::string to_username = form_data["to_username"];
    std::string amount_str = form_data["amount"];
    int to_user_id;
    double amount;

    // Lookup recipient user ID by username
    if (!get_user_id_by_username(to_username, to_user_id)) {
        return crow::response(400, "Invalid recipient username");
    }

    try {
        amount = std::stod(amount_str);
    } catch (const std::invalid_argument& e) {
        return crow::response(400, "Invalid amount");
    } catch (const std::out_of_range& e) {
        return crow::response(400, "Amount out of range");
    }

    if (amount <= 0) {
        return crow::response(400, "Invalid input: Amount must be greater than zero");
    }

    if (!has_sufficient_balance(from_user_id, amount)) {
        return crow::response(400, "Insufficient funds");
    }

    if (transfer_funds(from_user_id, to_user_id, amount)) {
        return crow::response(200, "Transfer successful");
    } else {
        return crow::response(500, "Failed to transfer funds");
    }
});

   CROW_ROUTE(app, "/logs")
([](const crow::request& req, crow::response& res) {
    std::string session_id;
    if (req.get_header_value("Cookie").find("session_id=") != std::string::npos) {
        session_id = req.get_header_value("Cookie").substr(req.get_header_value("Cookie").find("session_id=") + 11);
    } else {
        res.code = 401;
        res.body = "Unauthorized";
        res.end();
        return;
    }

    int user_id;
    if (!validate_session(session_id, user_id)) {
        res.code = 401;
        res.body = "Unauthorized";
        res.end();
        return;
    }

    crow::mustache::context ctx;
    ctx["user_id"] = user_id;

    // Fetch user data
    sqlite3* DB;
    sqlite3_stmt* stmt;
    int exit = sqlite3_open("securebank.db", &DB);

    std::string sql_user = "SELECT USERNAME FROM USERS WHERE ID = " + std::to_string(user_id) + ";";
    sqlite3_prepare_v2(DB, sql_user.c_str(), -1, &stmt, 0);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        ctx["username"] = username;
    }
    sqlite3_finalize(stmt);

    // Fetch all transactions for the user
    std::string sql_transactions = "SELECT DATE, AMOUNT, TYPE FROM TRANSACTIONS WHERE USER_ID = " + std::to_string(user_id) + " ORDER BY DATE DESC;";
    sqlite3_prepare_v2(DB, sql_transactions.c_str(), -1, &stmt, 0);
    crow::json::wvalue::list transactions;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        crow::json::wvalue transaction;
        transaction["date"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        transaction["amount"] = sqlite3_column_double(stmt, 1);
        transaction["type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        transactions.push_back(std::move(transaction));
    }
    ctx["transactions"] = std::move(transactions);
    sqlite3_finalize(stmt);
    sqlite3_close(DB);

    // Render the logs template
    auto page = crow::mustache::load("logs.html");
    res.code = 200;
    res.body = page.render(ctx).dump();
    res.end();
});




   

    // Start the application
    app.port(18080).multithreaded().run();
    app.loglevel(crow::LogLevel::Warning);
}
