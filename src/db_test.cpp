#include <iostream>
#include <pqxx/pqxx>  // This is the official C++ Postgres library

int main() {
    try {
        // 1. Define the connection string (The "Keys" to the DB)
        std::string connection_string = "dbname=justiceflow user=justiceflow password=justiceflow123 host=localhost port=5432";

        // 2. Attempt to connect
        std::cout << "Attempting to connect to PostgreSQL..." << std::endl;
        pqxx::connection C(connection_string);

        // 3. Check if open
        if (C.is_open()) {
            std::cout << " SUCCESS: Connected to database: " << C.dbname() << std::endl;
            std::cout << "   User: " << C.username() << std::endl;
        } else {
            std::cout << " ERROR: Can't open database" << std::endl;
            return 1;
        }

        // 4. (Optional) Disconnect is automatic when C goes out of scope
    } catch (const std::exception &e) {
        std::cerr << " EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
