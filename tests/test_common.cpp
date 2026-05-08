// tests/test_common.cpp
#include "test_common.h"

namespace test_utils
{

    JusticeFlow::Case createTestCase(PGconn *conn, int officer_id)
    {
        JusticeFlow::Case test_case;
        test_case.case_id = 1;
        test_case.fir_number = "FIR-TEST-001";
        test_case.case_type = JusticeFlow::CaseType::MURDER;
        test_case.case_status = JusticeFlow::CaseStatus::REGISTERED;
        test_case.incident_date = time(nullptr);
        test_case.incident_address = "123 Test Street";
        test_case.station_id = 1;
        test_case.filed_by = officer_id;
        test_case.filed_at = time(nullptr);

        // TODO: Actually insert into database if needed

        return test_case;
    }

    JusticeFlow::Officer createTestOfficer(PGconn *conn)
    {
        JusticeFlow::Officer officer;
        officer.officerId = 1;
        officer.beltNumber = "BP-001";
        officer.cnic = "12345-6789012-3";
        officer.qualification = "High School";
        officer.currentRank = JusticeFlow::OfficerRank::CONSTABLE;
        officer.stationId = 1;
        officer.status = JusticeFlow::OfficerStatus::ACTIVE;

        // TODO: Actually insert into database if needed

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
        // TODO: Implement database cleanup
        // DELETE FROM cases WHERE fir_number LIKE 'FIR-TEST-%'
        // DELETE FROM officers WHERE cnic = '12345-6789012-3'
    }

} // namespace test_utils