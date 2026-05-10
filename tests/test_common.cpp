// tests/test_common.cpp
#include "test_common.h"

namespace test_utils
{
    namespace
    {
        int next_suffix()
        {
            static int counter = 1000;
            return counter++;
        }
    }

    JusticeFlow::Case createTestCase(PGconn *conn, int officer_id)
    {
        JusticeFlow::Case test_case;
        const int suffix = next_suffix();
        test_case.case_id = 0; // will be updated from RETURNING on success; stays 0 if INSERT fails
        test_case.fir_number = "FIR-TEST-" + std::to_string(suffix);
        test_case.case_type = JusticeFlow::CaseType::MURDER;
        test_case.case_status = JusticeFlow::CaseStatus::REGISTERED;
        test_case.incident_date = time(nullptr);
        test_case.incident_address = "123 Test Street";
        test_case.station_id = 1;
        test_case.filed_by = officer_id;
        test_case.filed_at = time(nullptr);

        if (conn != nullptr && PQstatus(conn) == CONNECTION_OK)
        {
            char query[1024];
            std::snprintf(
                query, sizeof(query),
                "INSERT INTO cases (fir_number, case_type, case_status, incident_date, incident_address, incident_description, "
                "incident_lat, incident_lon, station_id, primary_complainant_cnic, filed_by, filed_at) "
                "VALUES ('%s', 'MURDER', 'REGISTERED', NOW(), '123 Test Street', 'test', 0, 0, 1, '12345-6789012-3', %d, NOW()) "
                "RETURNING case_id",
                test_case.fir_number.c_str(), officer_id > 0 ? officer_id : 1);
            PGresult *res = PQexec(conn, query);
            if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1)
            {
                test_case.case_id = std::atoi(PQgetvalue(res, 0, 0));
            }
            PQclear(res);
        }

        return test_case;
    }

    JusticeFlow::Officer createTestOfficer(PGconn *conn)
    {
        JusticeFlow::Officer officer;
        const int suffix = next_suffix();
        char cnic_buf[20];
        std::snprintf(cnic_buf, sizeof(cnic_buf), "12345-6789%03d-3", suffix % 1000);
        officer.officerId = suffix;
        officer.beltNumber = "PC-" + std::to_string(suffix);
        officer.cnic = cnic_buf;
        officer.qualification = "High School";
        officer.currentRank = JusticeFlow::OfficerRank::CONSTABLE;
        officer.stationId = 1;
        officer.status = JusticeFlow::OfficerStatus::ACTIVE;

        if (conn != nullptr && PQstatus(conn) == CONNECTION_OK)
        {
            char query[1024];
            std::snprintf(query, sizeof(query),
                          "INSERT INTO persons (cnic, full_name, gender) VALUES ('%s', 'Test Officer', 'MALE') ON CONFLICT (cnic) DO NOTHING",
                          officer.cnic.c_str());
            PGresult *res1 = PQexec(conn, query);
            PQclear(res1);

            std::snprintf(query, sizeof(query),
                          "INSERT INTO officers (belt_number, cnic, qualification, joining_date, joining_rank, current_rank, bps_scale, station_id, status) "
                          "VALUES ('%s', '%s', 'High School', CURRENT_DATE, 'CONSTABLE', 'CONSTABLE', 7, 1, 'ACTIVE') "
                          "ON CONFLICT (cnic) DO UPDATE SET current_rank = EXCLUDED.current_rank RETURNING officer_id",
                          officer.beltNumber.c_str(), officer.cnic.c_str());
            PGresult *res2 = PQexec(conn, query);
            if (PQresultStatus(res2) == PGRES_TUPLES_OK && PQntuples(res2) == 1)
            {
                officer.officerId = std::atoi(PQgetvalue(res2, 0, 0));
            }
            PQclear(res2);
        }

        return officer;
    }

    JusticeFlow::SessionContext createTestSession(int officer_id)
    {
        JusticeFlow::SessionContext session;
        session.officerId = officer_id;
        session.cnic = "12345-6789012-3";
        session.rank = JusticeFlow::OfficerRank::CONSTABLE;
        session.stationId = 1;
        session.createdAt = time(nullptr);
        session.expiresAt = time(nullptr) + 3600; // 1 hour from now
        session.isValid = true;
        session.sessionToken = "test_token_12345";

        return session;
    }

    void cleanupTestData(PGconn *conn)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
        {
            return;
        }
        PGresult *res1 = PQexec(conn, "DELETE FROM cases WHERE fir_number LIKE 'FIR-TEST-%'");
        PQclear(res1);
        PGresult *res2 = PQexec(conn, "DELETE FROM officers WHERE cnic LIKE '12345-6789%-3'");
        PQclear(res2);
        PGresult *res3 = PQexec(conn, "DELETE FROM persons WHERE cnic LIKE '12345-6789%-3'");
        PQclear(res3);
    }

} // namespace test_utils
