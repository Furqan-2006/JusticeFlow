-- Known officer the C++ tests sign in as. Load into the TEST database only
-- (`make test-db` does this); never into a database holding real data.
INSERT INTO persons (cnic, full_name, gender)
VALUES ('90000-0000001-1', 'Test Inspector', 'MALE')
ON CONFLICT (cnic) DO NOTHING;

INSERT INTO officers (belt_number, cnic, joining_date, joining_rank, current_rank,
                      bps_scale, station_id, status, password_hash)
SELECT 'K-900001', '90000-0000001-1', CURRENT_DATE, 'INSPECTOR', 'INSPECTOR',
       16, min(station_id), 'ACTIVE', crypt('JusticeDemo@2026', gen_salt('bf'))
FROM stations
ON CONFLICT (cnic) DO NOTHING;
