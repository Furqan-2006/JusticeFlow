#!/usr/bin/env python3
"""
JusticeFlow Synthetic Data Generator  — v2 (schema-verified)
==============================================================
Built against the actual 02_tables.sql column names.

Requirements:
    pip install psycopg2-binary faker numpy --break-system-packages

Usage:
    python3 generate_data.py              # generate
    python3 generate_data.py --clear      # wipe then generate
"""

import sys
import random
import traceback
from datetime import datetime, timedelta, date

import numpy as np
import psycopg2
from psycopg2.extras import execute_batch
from faker import Faker

# ── Deterministic seed ───────────────────────────────────────
SEED = 42
random.seed(SEED)
np.random.seed(SEED)
fake = Faker()
Faker.seed(SEED)

# ══════════════════════════════════════════════════════════════
#  DATABASE CONFIG  — update password before running
# ══════════════════════════════════════════════════════════════
DB_CONFIG = {
    'host':     'localhost',
    'port':     5432,
    'dbname':   'justiceflow',
    'user':     'postgres',
    'password': '123'    # ← change this
}

# ── Volume config ─────────────────────────────────────────────
N_STATIONS   = 50
N_PERSONS    = 500
N_OFFICERS   = 150
N_CASES      = 1000
N_EVIDENCE   = 2000
N_WARRANTS   = 400
N_ARRESTS    = 350
N_BAIL       = 200
N_FORENSIC   = 300
N_SHEETS     = 300
N_DUTY       = 500
N_VEHICLES   = 150
BATCH        = 100

# ── Date range ────────────────────────────────────────────────
START_DT = datetime(2020, 1, 1)
END_DT   = datetime(2025, 12, 31)
START_D  = date(2020, 1, 1)
END_D    = date(2025, 12, 31)

# ══════════════════════════════════════════════════════════════
#  PAKISTANI DATA CONSTANTS
# ══════════════════════════════════════════════════════════════

MALE_NAMES = [
    'Muhammad','Ahmed','Ali','Hassan','Hussain','Usman','Omar','Bilal',
    'Kamran','Tariq','Zubair','Imran','Asif','Salman','Faisal','Adnan',
    'Farhan','Shoaib','Wasim','Khalid','Nadeem','Aamir','Sajid','Tahir',
    'Rizwan','Hamid','Waqar','Shahid','Arif','Naeem','Danish','Zafar',
    'Kashif','Junaid','Irfan','Iqbal','Fahad','Babar','Talha','Sohail',
]
FEMALE_NAMES = [
    'Fatima','Ayesha','Zainab','Maryam','Sana','Nadia','Sara','Hina',
    'Rabia','Amina','Saima','Lubna','Nazia','Uzma','Shazia','Rehana',
    'Bushra','Rukhsana','Nasreen','Tahira','Sadia','Farzana','Samina',
]
LAST_NAMES = [
    'Khan','Sheikh','Malik','Chaudhry','Qureshi','Siddiqui','Ansari',
    'Baig','Mirza','Akhtar','Hussain','Ali','Ahmed','Raza','Shah','Butt',
    'Rajput','Javed','Hashmi','Abbasi','Memon','Patel','Baloch','Laghari',
]

CNIC_DISTRICTS = [
    '42101','42201','42301','42401','42501',
    '42601','42701','42801','42901',
    '41301','41302','43101',
]

# 50 stations: (code, name, city, district, zone, type)
# NOTE: Stations table has NO lat/lon columns
STATION_DATA = [
    # Karachi South
    ('KHI-SD', 'Saddar Police Station',         'Karachi', 'Karachi South',   'South',   'POLICE_STATION'),
    ('KHI-CL', 'Clifton Police Station',         'Karachi', 'Karachi South',   'South',   'POLICE_STATION'),
    ('KHI-GD', 'Garden Police Station',          'Karachi', 'Karachi South',   'South',   'POLICE_STATION'),
    ('KHI-KM', 'Kemari Police Station',          'Karachi', 'Karachi South',   'South',   'POLICE_STATION'),
    ('KHI-LY', 'Lyari Police Station',           'Karachi', 'Karachi South',   'South',   'POLICE_STATION'),
    ('KHI-KH', 'Kharadar Police Station',        'Karachi', 'Karachi South',   'South',   'POLICE_STATION'),
    ('KHI-RC', 'Ranchore Line Police Station',   'Karachi', 'Karachi South',   'South',   'POLICE_STATION'),
    ('KHI-CS', 'Civil Lines Police Station',     'Karachi', 'Karachi South',   'South',   'POLICE_STATION'),
    ('KHI-SZ', 'Karachi South Zone HQ',          'Karachi', 'Karachi South',   'South',   'ZONE_HQ'),
    # Karachi East
    ('KHI-GI', 'Gulshan-e-Iqbal Police Station','Karachi', 'Karachi East',    'East',    'POLICE_STATION'),
    ('KHI-FB', 'Federal B Area Police Station',  'Karachi', 'Karachi East',    'East',    'POLICE_STATION'),
    ('KHI-ML', 'Malir Police Station',           'Karachi', 'Karachi East',    'East',    'POLICE_STATION'),
    ('KHI-LN', 'Landhi Police Station',          'Karachi', 'Karachi East',    'East',    'POLICE_STATION'),
    ('KHI-KG', 'Korangi Police Station',         'Karachi', 'Karachi East',    'East',    'POLICE_STATION'),
    ('KHI-SR', 'Shah Rasool Colony PS',          'Karachi', 'Karachi East',    'East',    'POLICE_STATION'),
    ('KHI-AZ', 'Aziz Bhatti Police Station',     'Karachi', 'Karachi East',    'East',    'POLICE_STATION'),
    ('KHI-EZ', 'Karachi East Zone HQ',           'Karachi', 'Karachi East',    'East',    'ZONE_HQ'),
    # Karachi West
    ('KHI-OR', 'Orangi Police Station',          'Karachi', 'Karachi West',    'West',    'POLICE_STATION'),
    ('KHI-BL', 'Baldia Police Station',          'Karachi', 'Karachi West',    'West',    'POLICE_STATION'),
    ('KHI-ST', 'SITE-A Police Station',          'Karachi', 'Karachi West',    'West',    'POLICE_STATION'),
    ('KHI-MB', 'Mominabad Police Station',       'Karachi', 'Karachi West',    'West',    'POLICE_STATION'),
    ('KHI-SM', 'Samanabad Police Station',       'Karachi', 'Karachi West',    'West',    'POLICE_STATION'),
    ('KHI-WZ', 'Karachi West Zone HQ',           'Karachi', 'Karachi West',    'West',    'ZONE_HQ'),
    # Karachi Central
    ('KHI-NZ', 'Nazimabad Police Station',       'Karachi', 'Karachi Central', 'Central', 'POLICE_STATION'),
    ('KHI-NG', 'New Karachi Police Station',     'Karachi', 'Karachi Central', 'Central', 'POLICE_STATION'),
    ('KHI-PH', 'Paposh Nagar Police Station',    'Karachi', 'Karachi Central', 'Central', 'POLICE_STATION'),
    ('KHI-NK', 'North Karachi Police Station',   'Karachi', 'Karachi Central', 'Central', 'POLICE_STATION'),
    ('KHI-GR', 'Gulberg Police Station',         'Karachi', 'Karachi Central', 'Central', 'POLICE_STATION'),
    ('KHI-CZ', 'Karachi Central Zone HQ',        'Karachi', 'Karachi Central', 'Central', 'ZONE_HQ'),
    # Karachi Malir
    ('KHI-MA', 'Malir City Police Station',      'Karachi', 'Karachi Malir',   'Malir',   'POLICE_STATION'),
    ('KHI-BI', 'Bin Qasim Police Station',       'Karachi', 'Karachi Malir',   'Malir',   'POLICE_STATION'),
    ('KHI-MH', 'Murad Memon Police Station',     'Karachi', 'Karachi Malir',   'Malir',   'POLICE_STATION'),
    ('KHI-MZ', 'Karachi Malir Zone HQ',          'Karachi', 'Karachi Malir',   'Malir',   'ZONE_HQ'),
    # Karachi HQ
    ('KHI-DH', 'Karachi District HQ',            'Karachi', 'Karachi',         None,      'DISTRICT_HQ'),
    # Hyderabad
    ('HYD-01', 'Hyderabad City Police Station',  'Hyderabad', 'Hyderabad',     None,      'POLICE_STATION'),
    ('HYD-02', 'Latifabad Police Station',        'Hyderabad', 'Hyderabad',     None,      'POLICE_STATION'),
    ('HYD-03', 'Qasimabad Police Station',        'Hyderabad', 'Hyderabad',     None,      'POLICE_STATION'),
    ('HYD-HQ', 'Hyderabad District HQ',           'Hyderabad', 'Hyderabad',     None,      'DISTRICT_HQ'),
    # Sukkur
    ('SKR-01', 'Sukkur City Police Station',      'Sukkur',  'Sukkur',          None,      'POLICE_STATION'),
    ('SKR-02', 'Sukkur Rohri Police Station',     'Sukkur',  'Sukkur',          None,      'POLICE_STATION'),
    ('SKR-HQ', 'Sukkur District HQ',              'Sukkur',  'Sukkur',          None,      'DISTRICT_HQ'),
    # Larkana
    ('LRK-01', 'Larkana City Police Station',     'Larkana', 'Larkana',         None,      'POLICE_STATION'),
    ('LRK-HQ', 'Larkana District HQ',             'Larkana', 'Larkana',         None,      'DISTRICT_HQ'),
    # Other Sindh
    ('MPK-01', 'Mirpurkhas Police Station',       'Mirpurkhas', 'Mirpurkhas',   None,      'POLICE_STATION'),
    ('NWB-01', 'Nawabshah Police Station',         'Nawabshah',  'Nawabshah',    None,      'POLICE_STATION'),
    ('JAC-01', 'Jacobabad Police Station',         'Jacobabad',  'Jacobabad',    None,      'POLICE_STATION'),
    ('KHP-01', 'Khairpur Police Station',          'Khairpur',   'Khairpur',     None,      'POLICE_STATION'),
    ('DAD-01', 'Dadu Police Station',              'Dadu',       'Dadu',         None,      'POLICE_STATION'),
    ('THA-01', 'Thatta Police Station',            'Thatta',     'Thatta',       None,      'POLICE_STATION'),
    ('SNG-01', 'Sanghar Police Station',           'Sanghar',    'Sanghar',      None,      'POLICE_STATION'),
]
assert len(STATION_DATA) == 50

# Karachi lat/lon clusters (only used for Cases.incident_lat/lon)
CRIME_CLUSTERS = [
    (24.8607, 67.0011, 0.008, 0.20),  # Lyari
    (24.9418, 66.9750, 0.009, 0.15),  # Orangi
    (24.8282, 67.1287, 0.007, 0.12),  # Korangi
    (24.8925, 67.2088, 0.010, 0.10),  # Malir
    (24.9268, 67.0819, 0.008, 0.10),  # Gulshan
    (24.8615, 67.0104, 0.007, 0.08),  # SITE
    (24.9056, 67.0400, 0.007, 0.08),  # Nazimabad
    (24.8742, 67.1289, 0.009, 0.07),  # Landhi
    (24.8104, 67.0314, 0.008, 0.05),  # Clifton
    (24.9568, 67.0200, 0.006, 0.05),  # New Karachi
]
CLUSTER_W = [c[3] for c in CRIME_CLUSTERS]

# Enums — exact values from 01_types.sql
OFFICER_RANKS = ['CONSTABLE','HEAD_CONSTABLE','ASI','SI','INSPECTOR',
                 'DSP','SP','SSP','DIG','ADDL_IG','IGP']
RANK_BPS      = {'CONSTABLE':7,'HEAD_CONSTABLE':9,'ASI':11,'SI':14,
                 'INSPECTOR':16,'DSP':17,'SP':18,'SSP':19,
                 'DIG':20,'ADDL_IG':21,'IGP':22}
RANK_W        = [40,25,15,10,6,2,1,1,0,0,0]   # realistic pyramid

FIR_RANKS     = ['ASI','SI','INSPECTOR']
WARRANT_RANKS = ['INSPECTOR','DSP','SP','SSP','DIG','ADDL_IG','IGP']
DSP_PLUS      = ['DSP','SP','SSP','DIG','ADDL_IG','IGP']

CASE_TYPES = [
    'MURDER','ATTEMPTED_MURDER','MANSLAUGHTER','KIDNAPPING','HUMAN_TRAFFICKING',
    'ROBBERY','ARMED_ROBBERY','ASSAULT','AGGRAVATED_ASSAULT','RAPE',
    'SEXUAL_ASSAULT','BURGLARY','HOME_INVASION','ARSON','VANDALISM',
    'DRUG_TRAFFICKING','DRUG_POSSESSION','TERRORISM','EXTORTION','GANG_ACTIVITY',
    'THEFT','FRAUD','CYBERCRIME','HIT_AND_RUN','VEHICLE_THEFT',
    'DOMESTIC_VIOLENCE','HARASSMENT','BRIBERY','FORGERY','PUBLIC_DISTURBANCE',
]
CASE_TYPE_W = [7,4,2,4,1,8,4,5,3,2,2,4,2,2,3,4,3,2,2,2,
               10,4,3,3,5,4,3,2,2,2]

CASE_STATUSES = ['REGISTERED','UNDER_INVESTIGATION','EVIDENCE_COLLECTED',
                 'PENDING_TRIAL','CLOSED','REOPENED']
CASE_STATUS_W = [10,25,20,15,25,5]

EVIDENCE_TYPES    = ['PHYSICAL','DIGITAL','TESTIMONIAL','FORENSIC','DOCUMENTARY']
EVIDENCE_STATUSES = ['RECEIVED','SEALED','SENT_TO_LAB','RETURNED_FROM_LAB',
                     'PRODUCED_IN_COURT','DISPOSED']

WARRANT_TYPES   = ['ARREST','SEARCH','SEIZURE']
WARRANT_STATUSES= ['ISSUED','EXECUTED','CANCELLED','EXPIRED']

BAIL_TYPES    = ['REGULAR','ANTICIPATORY','INTERIM','SURETY']
BAIL_STATUSES = ['ACTIVE','REVOKED','EXPIRED','CANCELLED']

# exact enum values from 01_types.sql
FORENSIC_STATUSES = ['REQUESTED','RECEIVED_BY_LAB','UNDER_EXAMINATION',
                     'REPORT_READY','REPORT_DELIVERED','CONTESTED']
EXAM_PURPOSES = ['DNA_ANALYSIS','FINGERPRINT_ANALYSIS','BALLISTICS_ANALYSIS',
                 'TOXICOLOGY_ANALYSIS','DIGITAL_FORENSICS','DOCUMENT_EXAMINATION',
                 'BLOOD_ANALYSIS','NARCOTICS_TESTING','TRACE_EVIDENCE_ANALYSIS','OTHER']

SHEET_STATUSES = ['DRAFT','FILED','SUBMITTED_TO_COURT',
                  'ACCEPTED_BY_COURT','REJECTED_BY_COURT']

INVOLVEMENT_TYPES = ['SUSPECT','ACCUSED','CONVICTED','ACQUITTED']
VEHICLE_TYPES     = ['CAR','MOTORCYCLE','TRUCK','BUS','VAN','SUV','RICKSHAW','OTHER']
VEHICLE_ROLES     = ['STOLEN','USED_IN_CRIME','ABANDONED','EVIDENCE',
                     'SUSPECTS_VEHICLE','VICTIMS_VEHICLE']
SEIZURE_STATUSES  = ['SEIZED','NOT_SEIZED','RELEASED','AUCTIONED',
                     'DESTROYED','RETAINED_FOR_EVIDENCE']
SHIFT_TYPES       = ['MORNING','AFTERNOON','NIGHT','SPLIT','ON_CALL']
DUTY_STATUSES     = ['SCHEDULED','ON_DUTY','COMPLETED','ABSENT','ON_LEAVE','SUSPENDED']

# exact enum values
RELATION_TO_VICTIM   = ['SELF','PARENT','SPOUSE','SIBLING','CHILD',
                         'RELATIVE','WITNESS','THIRD_PARTY','OTHER']
COMPLAINANT_STATUSES = ['ACTIVE','WITHDRAWN','DECEASED','UNREACHABLE']
INJURY_SEVERITY      = ['NONE','MINOR','MODERATE','SEVERE','FATAL']
VULN_CATEGORY        = ['NONE','MINOR','ELDERLY','DIFFERENTLY_ABLED','FEMALE_ALONE']
WITNESS_PROTECTION   = ['NONE','MONITORED','PROTECTED','RELOCATED']
ASSOC_TYPES          = ['CO_ACCUSED','GANG_MEMBER','ACCOMPLICE','FAMILY','KNOWN_ASSOCIATE']

PAKISTANI_LAWS = [
    'PPC Section 302 - Murder',
    'PPC Section 324 - Attempted Murder',
    'PPC Section 365 - Kidnapping',
    'PPC Section 392 - Robbery',
    'PPC Section 379 - Theft',
    'PPC Section 353 - Assault on Public Servant',
    'PPC Section 406 - Criminal Breach of Trust',
    'PPC Section 420 - Cheating and Dishonestly',
    'PPC Section 452 - House Trespass',
    'PPC Section 506 - Criminal Intimidation',
    'PECA 2016 Section 20 - Cyber Stalking',
    'PECA 2016 Section 21 - Hate Speech Online',
    'CNSA 1997 Section 9 - Drug Trafficking',
    'CNSA 1997 Section 6 - Drug Possession',
    'Arms Ordinance 1965 Section 13 - Illegal Arms Possession',
    'Zainab Alert Act 2020 - Missing Child Case',
    'Anti-Terrorism Act 1997 Section 7',
    'Prevention of Corruption Act 1947',
    'CrPC 1898 Section 107 - Security for Keeping Peace',
    'PPC Section 147 - Rioting',
]

FORENSIC_LABS = [
    'Federal Forensic Science Agency, Karachi',
    'Sindh Police Forensic Laboratory, Karachi',
    'PCSIR Laboratory Complex, Karachi',
    'Aga Khan University Hospital Forensic Lab',
    'Jinnah Postgraduate Medical Centre Forensic Lab',
]
COURTS = [
    'Additional Sessions Court, Karachi South',
    'Sessions Court, Karachi East',
    'Anti-Terrorism Court No. 1, Karachi',
    'Anti-Terrorism Court No. 2, Karachi',
    'Judicial Magistrate Court, Malir',
    'Sessions Court, Hyderabad',
    'Sessions Court, Sukkur',
    'District Court, Larkana',
]
MAGISTRATES = [
    'Rana Asif Mehmood', 'Khalid Mehmood Siddiqui', 'Nadia Fatima Malik',
    'Abdul Rehman Qureshi', 'Tariq Hussain Shah', 'Samina Anwar Khan',
    'Muhammad Zubair Baig', 'Fauzia Amjad Mirza', 'Saleem Akhtar Abbasi',
]
VEHICLE_MAKES = [
    ('Toyota','Corolla'),('Toyota','Hilux'),('Honda','Civic'),('Honda','CD70'),
    ('Suzuki','Mehran'),('Suzuki','Alto'),('Yamaha','YBR125'),('Ravi','Loader'),
    ('Hyundai','Tucson'),('KIA','Sportage'),('Isuzu','D-Max'),('FAW','Carrier'),
]
LOCATIONS = ['Lea Market','Saddar','Lyari','Orangi Town','Malir','Korangi',
             'Gulshan-e-Iqbal','SITE Area','Nazimabad','Kemari','Clifton','Landhi']
COLORS = ['White','Black','Silver','Red','Blue','Green','Grey','Brown','Yellow']
EVIDENCE_DESC = {
    'PHYSICAL':    ['Weapon recovered at scene','Blood-stained clothing',
                    'Fingerprint lifts','Vehicle number plate','Currency notes'],
    'DIGITAL':     ['Mobile phone extraction','Laptop hard drive image',
                    'CCTV recording copy','Call records printout'],
    'TESTIMONIAL': ['Witness oral statement','Victim recorded testimony',
                    'Expert witness deposition'],
    'FORENSIC':    ['DNA swab sample','Ballistic cartridge',
                    'Toxicology blood sample','Fingerprint card'],
    'DOCUMENTARY': ['FIR certified copy','Medical injury report',
                    'Bank transaction record','Property document'],
}

# ══════════════════════════════════════════════════════════════
#  HELPERS
# ══════════════════════════════════════════════════════════════

def gen_cnic(used: set) -> str:
    while True:
        dist = random.choice(CNIC_DISTRICTS)
        seq  = random.randint(1_000_001, 9_999_999)
        chk  = random.randint(0, 9)
        c    = f"{dist}-{seq}-{chk}"
        if c not in used:
            used.add(c)
            return c

def gen_name(gender: str):
    first = random.choice(MALE_NAMES   if gender == 'MALE'
                          else FEMALE_NAMES if gender == 'FEMALE'
                          else MALE_NAMES + FEMALE_NAMES)
    return f"{first} {random.choice(LAST_NAMES)}"

def gen_mobile():
    pfx = random.choice(['0300','0301','0311','0312','0321','0322','0331','0333'])
    return f"{pfx}-{''.join(str(random.randint(0,9)) for _ in range(7))}"

def gen_belt(rank: str, used: set) -> str:
    prefix = 'PC' if rank=='CONSTABLE' else 'HC' if rank=='HEAD_CONSTABLE' else 'K'
    while True:
        b = f"{prefix}-{random.randint(1001,9999):04d}"
        if b not in used:
            used.add(b)
            return b

def gen_vreg(used: set) -> str:
    while True:
        letters = ''.join(random.choices('ABCDEFGHJKLMNPRSTUVWXYZ', k=3))
        r = f"{letters}-{random.randint(100,9999)}"
        if r not in used:
            used.add(r)
            return r

def gen_beat_code(used: set, station_code: str, i: int) -> str:
    while True:
        b = f"{station_code}-B{i:02d}"
        if b not in used:
            used.add(b)
            return b

def rand_dt(start=START_DT, end=END_DT) -> datetime:
    delta = end - start
    return start + timedelta(seconds=random.randint(0, int(delta.total_seconds())))

def rand_date(s: date = START_D, e: date = END_D) -> date:
    return rand_dt(datetime.combine(s, datetime.min.time()),
                   datetime.combine(e, datetime.min.time())).date()

def past_dt(before: datetime = None) -> datetime:
    """Return a datetime strictly before `before` (defaults to now)."""
    before = before or datetime.now()
    start  = before - timedelta(days=365*5)
    if start < START_DT:
        start = START_DT
    if start >= before:
        return before - timedelta(hours=1)
    return rand_dt(start, before)

def karachi_coord():
    c   = random.choices(CRIME_CLUSTERS, weights=CLUSTER_W)[0]
    lat = round(float(np.random.normal(c[0], c[2])), 6)
    lon = round(float(np.random.normal(c[1], c[2])), 6)
    return lat, lon

def ok(msg):  print(f"    ✓ {msg}", flush=True)
def bx(cur, sql, rows, sz=BATCH): execute_batch(cur, sql, rows, page_size=sz)

# ══════════════════════════════════════════════════════════════
#  GENERATORS  (FK dependency order)
# ══════════════════════════════════════════════════════════════

# ── 1. Stations ───────────────────────────────────────────────
def gen_stations(cur) -> list:
    print("\n[1/16] Stations")
    sql = """
        INSERT INTO Stations
            (station_code, station_name, city, district, zone,
             station_type, address, phone, is_active)
        VALUES (%s,%s,%s,%s,%s,%s::station_type_enum,%s,%s,TRUE)
        RETURNING station_id, station_code, district
    """
    stations = []
    for code, name, city, dist, zone, stype in STATION_DATA:
        cur.execute(sql, (
            code, name, city, dist, zone, stype,
            f"{name}, {dist}, {city}, Sindh, Pakistan",
            f"021-{random.randint(30000000,39999999)}",
        ))
        r = cur.fetchone()
        stations.append({'station_id': r[0], 'code': r[1],
                         'district': r[2], 'city': city})
    ok(f"{len(stations)} stations")
    return stations


# ── 2. Persons ────────────────────────────────────────────────
def gen_persons(cur) -> list:
    print(f"\n[2/16] Persons ({N_PERSONS})")
    sql = """
        INSERT INTO Persons
            (cnic, full_name, gender, dob, mobile, email,
             permanent_address, current_address)
        VALUES (%s,%s,%s::gender_enum,%s,%s,%s,%s,%s)
    """
    used, cnics, rows = set(), [], []
    for _ in range(N_PERSONS):
        gender  = random.choices(['MALE','FEMALE','OTHER'], weights=[65,30,5])[0]
        name    = gen_name(gender)
        cnic    = gen_cnic(used)
        dob     = rand_date(date(1955,1,1), date(2005,12,31))
        dist    = random.choice(['Karachi South','Karachi East','Karachi West',
                                 'Karachi Central','Hyderabad'])
        addr    = f"H#{random.randint(1,500)}, St.{random.randint(1,30)}, {dist}, Sindh"
        email   = f"{name.lower().replace(' ','.')}{random.randint(1,999)}@gmail.com"
        rows.append((cnic, name, gender, dob, gen_mobile(), email, addr, addr))
        cnics.append(cnic)
    bx(cur, sql, rows)
    ok(f"{len(cnics)} persons")
    return cnics


# ── 3. Officers ───────────────────────────────────────────────
def gen_officers(cur, persons: list, stations: list) -> list:
    print(f"\n[3/16] Officers ({N_OFFICERS})")
    sql = """
        INSERT INTO Officers
            (cnic, belt_number, joining_rank, current_rank, bps_scale,
             joining_date, station_id, status, qualification, retirement_date)
        VALUES (%s,%s,%s::officer_rank_enum,%s::officer_rank_enum,
                %s,%s,%s,%s::officer_status_enum,%s,%s)
        RETURNING officer_id, cnic, current_rank, station_id, status
    """
    rank_pool  = random.choices(OFFICER_RANKS, weights=RANK_W, k=N_OFFICERS)
    used_belts = set()
    officers   = []
    for cnic, rank in zip(random.sample(persons, N_OFFICERS), rank_pool):
        station = random.choice(stations)
        status  = random.choices(
            ['ACTIVE','SUSPENDED','ON_LEAVE','RETIRED','TERMINATED'],
            weights=[80,5,8,5,2]
        )[0]
        joining = rand_date(date(2000,1,1), date(2023,12,31))
        # chk_retirement_status: RETIRED requires retirement_date, others must have NULL
        retirement = rand_date(date(2020,1,1), date(2025,12,31)) \
                     if status == 'RETIRED' else None
        cur.execute(sql, (
            cnic, gen_belt(rank, used_belts),
            rank, rank,
            RANK_BPS[rank], joining,
            station['station_id'], status,
            random.choice(['B.A.','B.Sc.','M.A.','Matric','Intermediate',None]),
            retirement
        ))
        r = cur.fetchone()
        officers.append({'officer_id': r[0], 'cnic': r[1],
                         'rank': r[2], 'station_id': r[3], 'status': r[4]})
    ok(f"{len(officers)} officers")
    return officers


# ── 4. Cases ──────────────────────────────────────────────────
def gen_cases(cur, stations: list, officers: list, persons: list) -> list:
    print(f"\n[4/16] Cases ({N_CASES})")
    sql = """
        INSERT INTO Cases
            (case_type, case_status, approval_status,
             incident_date, incident_address, incident_description,
             incident_lat, incident_lon,
             station_id, primary_complainant_cnic, filed_by)
        VALUES
            (%s::case_type_enum, %s::case_status_enum,
             %s::approval_status_enum,
             %s, %s, %s, %s, %s,
             %s, %s, %s)
        RETURNING case_id, station_id, case_status, filed_at
    """
    fir_ok  = [o for o in officers if o['rank'] in FIR_RANKS and o['status']=='ACTIVE']
    if not fir_ok:
        fir_ok = [o for o in officers if o['status']=='ACTIVE'] or officers

    khi   = [s for s in stations if 'Karachi' in s['district']]
    other = [s for s in stations if 'Karachi' not in s['district']]

    cases = []
    for _ in range(N_CASES):
        station = random.choice(khi if random.random()<0.80 and khi else other or stations)
        officer = random.choice(fir_ok)
        ctype   = random.choices(CASE_TYPES, weights=CASE_TYPE_W)[0]
        status  = random.choices(CASE_STATUSES, weights=CASE_STATUS_W)[0]
        approval= random.choices(['NOT_REQUIRED','PENDING_APPROVAL','APPROVED','REJECTED'],
                                  weights=[60,10,25,5])[0]
        # incident_date must be <= NOW() per CHECK constraint
        inc_dt  = rand_dt(START_DT, datetime.now() - timedelta(hours=1))
        loc     = random.choice(LOCATIONS)
        desc    = f"{ctype.replace('_',' ').title()} reported near {loc}."

        lat = lon = None
        if 'Karachi' in station['district']:
            lat, lon = karachi_coord()

        comp_cnic = random.choice(persons)

        cur.execute(sql, (
            ctype, status, approval,
            inc_dt,
            f"Near {loc}, {station['district']}, Sindh",
            desc, lat, lon,
            station['station_id'], comp_cnic, officer['officer_id']
        ))
        r = cur.fetchone()
        cases.append({'case_id': r[0], 'station_id': r[1],
                      'status': r[2], 'filed_at': r[3],
                      'filed_by': officer['officer_id']})
    ok(f"{len(cases)} cases")
    return cases


# ── 5. Case Officers ──────────────────────────────────────────
def gen_case_officers(cur, cases: list, officers: list):
    print(f"\n[5/16] Case Officer Assignments")
    sql = """
        INSERT INTO Case_Officers
            (case_id, officer_id, role, assigned_by, assigned_at)
        VALUES (%s,%s,%s::case_officer_role_enum,%s,%s)
        ON CONFLICT DO NOTHING
    """
    active  = [o for o in officers if o['status']=='ACTIVE']
    sho     = [o for o in active if o['rank']=='INSPECTOR'] or active
    counts  = {o['officer_id']: 0 for o in active}
    roles   = ['DUTY_INCHARGE','SIO','IO','LEAD_INVESTIGATOR',
               'SUPPORTING','EVIDENCE_CUSTODIAN']
    rows = []
    for case in cases:
        assigner  = random.choice(sho)
        avail     = [o for o in active if counts[o['officer_id']]<10]
        if not avail: continue
        sample    = random.sample(avail, min(random.randint(1,3), len(avail)))
        for i, o in enumerate(sample):
            rows.append((case['case_id'], o['officer_id'],
                         roles[min(i, len(roles)-1)],
                         assigner['officer_id'],
                         case['filed_at'] + timedelta(hours=random.randint(1,24))))
            counts[o['officer_id']] += 1
    bx(cur, sql, rows)
    ok(f"{len(rows)} assignments")


# ── 6. Case Roles (Complainants / Victims / Witnesses / Accused) ──
def gen_case_roles(cur, persons: list, cases: list, officers: list) -> list:
    print(f"\n[6/16] Case Roles")
    active = [o for o in officers if o['status']=='ACTIVE']

    # ── Complainants ──
    c_sql = """
        INSERT INTO Complainants
            (case_id, person_cnic, relation_to_victim, status,
             added_by, notify_on_update)
        VALUES (%s,%s,%s::relationship_to_victim_enum,
                %s::complainant_status_enum,%s,%s)
        ON CONFLICT DO NOTHING
    """
    c_rows = []
    for case in cases:
        for cnic in random.sample(persons, random.randint(1,2)):
            withdrawn = random.random() < 0.05
            c_rows.append((
                case['case_id'], cnic,
                random.choice(RELATION_TO_VICTIM),
                'WITHDRAWN' if withdrawn else
                random.choices(['ACTIVE','DECEASED','UNREACHABLE'],weights=[85,5,10])[0],
                random.choice(active)['officer_id'],
                random.random() < 0.8
            ))
    bx(cur, c_sql, c_rows)
    ok(f"  Complainants: {len(c_rows)}")

    # ── Victims ──
    v_sql = """
        INSERT INTO Victims
            (case_id, person_cnic, injury_type, injury_severity,
             vulnerability_category, added_by)
        VALUES (%s,%s,%s,%s::injury_severity_enum,
                %s::vulnerability_category_enum,%s)
        ON CONFLICT DO NOTHING
    """
    v_rows = []
    for case in random.sample(cases, int(len(cases)*0.60)):
        for cnic in random.sample(persons, random.randint(1,2)):
            v_rows.append((
                case['case_id'], cnic,
                random.choice(['Gunshot wound','Knife wound','Blunt trauma',
                               'Physical assault','Acid attack',None]),
                random.choices(INJURY_SEVERITY, weights=[30,25,20,15,10])[0],
                random.choices(VULN_CATEGORY,   weights=[50,15,15,10,10])[0],
                random.choice(active)['officer_id']
            ))
    bx(cur, v_sql, v_rows)
    ok(f"  Victims: {len(v_rows)}")

    # ── Witnesses ──
    w_sql = """
        INSERT INTO Witnesses
            (case_id, person_cnic, statement_text, statement_recorded_at,
             recorded_by, protection_status, is_identity_concealed, added_by)
        VALUES (%s,%s,%s,%s,%s,%s::witness_protection_enum,%s,%s)
        ON CONFLICT DO NOTHING
    """
    w_rows = []
    for case in random.sample(cases, int(len(cases)*0.50)):
        officer = random.choice(active)
        for cnic in random.sample(persons, random.randint(1,2)):
            concealed = random.random() < 0.10
            stmt = (f"I observed the incident near {random.choice(LOCATIONS)} "
                    f"at approx. {random.randint(0,23):02d}:{random.randint(0,59):02d} hrs. "
                    f"The accused was seen {random.choice(['fleeing the scene','involved in altercation','carrying a weapon'])}.")
            prot = random.choices(WITNESS_PROTECTION, weights=[60,15,15,10])[0]
            w_rows.append((
                case['case_id'], cnic, stmt,
                case['filed_at'] + timedelta(days=random.randint(1,7)),
                officer['officer_id'], prot, concealed,
                officer['officer_id']
            ))
    bx(cur, w_sql, w_rows)
    ok(f"  Witnesses: {len(w_rows)}")

    # ── Accused ──
    # master_accused_cnic is nullable (alias link) — omit from INSERT
    a_sql = """
        INSERT INTO Accused
            (case_id, person_cnic, involvement_type, added_by)
        VALUES (%s,%s,%s::involvement_type_enum,%s)
        ON CONFLICT DO NOTHING
        RETURNING accused_id, person_cnic, case_id
    """
    accused = []
    for case in random.sample(cases, int(len(cases)*0.70)):
        officer = random.choice(active)
        for cnic in random.sample(persons, random.randint(1,2)):
            inv = random.choices(INVOLVEMENT_TYPES, weights=[40,35,15,10])[0]
            cur.execute(a_sql, (case['case_id'], cnic, inv, officer['officer_id']))
            r = cur.fetchone()
            if r:
                accused.append({'accused_id': r[0], 'cnic': r[1], 'case_id': r[2]})
    ok(f"  Accused: {len(accused)}")
    return accused


# ── 7. Evidence ───────────────────────────────────────────────
def gen_evidence(cur, cases: list, officers: list) -> list:
    print(f"\n[7/16] Evidence ({N_EVIDENCE})")
    sql = """
        INSERT INTO Evidence
            (case_id, evidence_type, evidence_status, description,
             quantity, file_path, collected_by, collected_at,
             collection_location, is_deleted)
        VALUES (%s,%s::evidence_type_enum,%s::evidence_status_enum,
                %s,%s,%s,%s,%s,%s,FALSE)
        RETURNING evidence_id, case_id, evidence_status
    """
    active  = [o for o in officers if o['status']=='ACTIVE']
    ev_list = []
    for case in random.choices(cases, k=N_EVIDENCE):
        etype   = random.choice(EVIDENCE_TYPES)
        estatus = random.choices(EVIDENCE_STATUSES, weights=[20,30,10,15,15,10])[0]
        officer = random.choice(active)
        col_dt  = case['filed_at'] + timedelta(hours=random.randint(2,48))
        desc    = random.choice(EVIDENCE_DESC[etype])
        # DIGITAL and DOCUMENTARY require file_path (CHECK constraint)
        fpath   = (f"/evidence/{case['case_id']}/{etype.lower()}_{random.randint(1000,9999)}.pdf"
                   if etype in ('DIGITAL','DOCUMENTARY') else None)
        cur.execute(sql, (
            case['case_id'], etype, estatus, desc,
            random.randint(1,5), fpath,
            officer['officer_id'], col_dt,
            f"Crime scene, {random.choice(LOCATIONS)}, Karachi", 
        ))
        r = cur.fetchone()
        if r:
            ev_list.append({'evidence_id': r[0], 'case_id': r[1], 'status': r[2]})
    ok(f"{len(ev_list)} evidence items")
    return ev_list


# ── 8. Evidence Custody Log ───────────────────────────────────
def gen_custody_log(cur, evidence_items: list, officers: list):
    print(f"\n[8/16] Evidence Custody Log")
    sql = """
        INSERT INTO Evidence_Custody_Log
            (evidence_id, transferred_from, transferred_to,
             transfer_reason, transferred_at, status_at_transfer, notes)
        VALUES (%s,%s,%s,%s,%s,%s::evidence_status_enum,%s)
    """
    active = [o for o in officers if o['status']=='ACTIVE']
    rows   = []
    reasons = ['Initial collection','Lab submission','Court production',
               'Secure storage transfer','Custody handover']
    for ev in random.sample(evidence_items, min(1500, len(evidence_items))):
        prev_id = None
        for _ in range(random.randint(1,3)):
            # chk_different_custodians: transferred_from <> transferred_to
            candidates = [o for o in active if o['officer_id'] != prev_id]
            if not candidates:
                candidates = active
            officer = random.choice(candidates)
            rows.append((
                ev['evidence_id'],
                prev_id,
                officer['officer_id'],
                random.choice(reasons),
                rand_dt(),
                random.choice(EVIDENCE_STATUSES[:4]),
                None
            ))
            prev_id = officer['officer_id']
    bx(cur, sql, rows)
    ok(f"{len(rows)} custody entries")


# ── 9. Warrants ───────────────────────────────────────────────
def gen_warrants(cur, cases: list, officers: list, persons: list) -> list:
    print(f"\n[9/16] Warrants ({N_WARRANTS})")
    sql = """
        INSERT INTO Warrants
            (case_id, warrant_type, warrant_status,
             issuing_court, magistrate_name,
             issue_date, valid_until, target_address,
             accused_cnic, requested_by,
             executed_by, executed_at,
             cancelled_by, cancelled_at, cancellation_reason)
        VALUES (%s,%s::warrant_type_enum,%s::warrant_status_enum,
                %s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s)
        RETURNING warrant_id, case_id, warrant_status
    """
    auth   = [o for o in officers if o['rank'] in WARRANT_RANKS and o['status']=='ACTIVE'] \
             or [o for o in officers if o['status']=='ACTIVE']
    active = [o for o in officers if o['status']=='ACTIVE']
    cancel_reasons = ['Accused surrendered voluntarily',
                      'Case settled','Insufficient evidence',
                      'Accused deceased','Jurisdictional transfer']
    warrants = []
    for case in random.choices(cases, k=N_WARRANTS):
        wtype   = random.choices(WARRANT_TYPES, weights=[60,30,10])[0]
        status  = random.choices(WARRANT_STATUSES, weights=[40,35,15,10])[0]
        req     = random.choice(auth)
        issue   = (case['filed_at'] + timedelta(days=random.randint(1,30))).date()
        valid   = issue + timedelta(days=random.randint(31,180))
        # CHECK: valid_until > issue_date (already guaranteed above)
        # SEARCH/SEIZURE warrants require target_address
        t_addr  = (f"H#{random.randint(1,200)}, Block {random.randint(1,20)}, "
                   f"{random.choice(LOCATIONS)}, Karachi") \
                  if wtype in ('SEARCH','SEIZURE') else None
        acc_cnic = random.choice(persons) if wtype=='ARREST' and random.random()<0.7 else None

        exec_by = exec_at = None
        if status == 'EXECUTED':
            exec_by = random.choice(active)['officer_id']
            exec_at = datetime.combine(issue, datetime.min.time()) + timedelta(days=random.randint(1,30))

        can_by = can_at = can_rsn = None
        if status == 'CANCELLED':
            can_by  = random.choice(active)['officer_id']
            can_at  = datetime.combine(issue, datetime.min.time()) + timedelta(days=random.randint(1,20))
            can_rsn = random.choice(cancel_reasons)

        cur.execute(sql, (
            case['case_id'], wtype, status,
            random.choice(COURTS), random.choice(MAGISTRATES),
            issue, valid, t_addr, acc_cnic, req['officer_id'],
            exec_by, exec_at,
            can_by, can_at, can_rsn
        ))
        r = cur.fetchone()
        if r:
            warrants.append({'warrant_id': r[0], 'case_id': r[1], 'status': r[2]})
    ok(f"{len(warrants)} warrants")
    return warrants


# ── 10. Arrests ───────────────────────────────────────────────
def gen_arrests(cur, cases: list, officers: list,
                accused: list, warrants: list) -> list:
    print(f"\n[10/16] Arrests ({N_ARRESTS})")
    sql = """
        INSERT INTO Arrests
            (accused_cnic, case_id, warrant_id,
             arresting_officer_id, arrested_at, arrest_location,
             custody_status, is_disputed, dispute_reason)
        VALUES (%s,%s,%s,%s,%s,%s,%s::custody_status_enum,%s,%s)
        RETURNING arrest_id, case_id, accused_cnic, arrested_at, custody_status
    """
    active  = [o for o in officers if o['status']=='ACTIVE']
    exec_w  = [w for w in warrants if w['status']=='EXECUTED']
    case_acc= {}
    for a in accused:
        case_acc.setdefault(a['case_id'], []).append(a['cnic'])

    candidates = [c for c in cases if c['case_id'] in case_acc] or cases
    arrests    = []
    for case in random.choices(candidates, k=N_ARRESTS):
        acc_list = case_acc.get(case['case_id'])
        if not acc_list: continue
        acc_cnic = random.choice(acc_list)
        officer  = random.choice(active)
        w_pool   = [w for w in exec_w if w['case_id']==case['case_id']]
        w_id     = random.choice(w_pool)['warrant_id'] \
                   if w_pool and random.random()<0.6 else None
        custody    = random.choices(
            ['IN_CUSTODY','BAIL_GRANTED','REMANDED','RELEASED','ESCAPED'],
            weights=[30,25,20,20,5]
        )[0]
        arr_dt     = case['filed_at'] + timedelta(days=random.randint(1,60))
        is_disputed= random.random() < 0.08
        # chk_dispute_requires_reason: is_disputed=TRUE requires dispute_reason
        dispute_rsn= random.choice([
            'Arrest made without proper warrant',
            'Wrong person arrested — mistaken identity',
            'Excessive force used during arrest',
            'Procedural violations during arrest',
        ]) if is_disputed else None
        cur.execute(sql, (
            acc_cnic, case['case_id'], w_id,
            officer['officer_id'], arr_dt,
            f"Near {random.choice(LOCATIONS)}, Karachi",
            custody, is_disputed, dispute_rsn
        ))
        r = cur.fetchone()
        if r:
            arrests.append({'arrest_id': r[0], 'case_id': r[1],
                            'accused_cnic': r[2], 'arrested_at': r[3],
                            'custody': r[4]})
    ok(f"{len(arrests)} arrests")
    return arrests


# ── 11. Bail Records ──────────────────────────────────────────
def gen_bail_records(cur, arrests: list, persons: list, officers: list):
    print(f"\n[11/16] Bail Records ({N_BAIL})")
    sql = """
        INSERT INTO Bail_Records
            (arrest_id, court_name, magistrate_name,
             bail_date, bail_type, bail_status, bail_amount,
             surety_name, surety_cnic, surety_address,
             valid_until, recorded_by)
        VALUES (%s,%s,%s,%s,%s::bail_type_enum,%s::bail_status_enum,
                %s,%s,%s,%s,%s,%s)
    """
    active   = [o for o in officers if o['status']=='ACTIVE']
    eligible = [a for a in arrests if a['custody']=='BAIL_GRANTED']
    eligible += random.sample(
        [a for a in arrests if a['custody'] in ['IN_CUSTODY','REMANDED']],
        min(80, len([a for a in arrests if a['custody'] in ['IN_CUSTODY','REMANDED']]))
    )
    if not eligible: eligible = arrests
    count = 0
    for arrest in random.sample(eligible, min(N_BAIL, len(eligible))):
        bail_date   = arrest['arrested_at'].date() + timedelta(days=random.randint(3,30))
        valid_until = bail_date + timedelta(days=random.randint(30,365))
        if valid_until <= bail_date:
            valid_until = bail_date + timedelta(days=30)
        # surety — if surety_name set, surety_cnic AND surety_address must be set
        use_surety = random.random() < 0.60
        s_name = s_cnic = s_addr = None
        if use_surety:
            s_name = gen_name('MALE')
            s_cnic = random.choice(persons)
            s_addr = f"H#{random.randint(1,200)}, {random.choice(LOCATIONS)}, Karachi"
        cur.execute(sql, (
            arrest['arrest_id'],
            random.choice(COURTS), random.choice(MAGISTRATES),
            bail_date,
            random.choices(BAIL_TYPES, weights=[50,20,20,10])[0],
            random.choices(BAIL_STATUSES, weights=[50,20,20,10])[0],
            round(random.uniform(10_000, 500_000), 2),
            s_name, s_cnic, s_addr,
            valid_until,
            random.choice(active)['officer_id']
        ))
        count += 1
    ok(f"{count} bail records")


# ── 12. Forensic Lab Requests ─────────────────────────────────
def gen_forensic(cur, cases: list, officers: list, evidence_items: list):
    print(f"\n[12/16] Forensic Lab Requests ({N_FORENSIC})")
    req_sql = """
        INSERT INTO Forensic_Lab_Requests
            (case_id, lab_name, examination_purpose, request_status,
             authorized_by, sent_date, received_by_lab_date,
             report_expected_date, findings, report_delivered_date)
        VALUES (%s,%s,%s::examination_purpose_enum,
                %s::forsenic_request_status_enum,
                %s,%s,%s,%s,%s,%s)
        RETURNING request_id, case_id
    """
    link_sql = """
        INSERT INTO Forensic_Request_Evidence (request_id, evidence_id, notes)
        VALUES (%s,%s,%s)
        ON CONFLICT DO NOTHING
    """
    auth   = [o for o in officers if o['rank'] in WARRANT_RANKS and o['status']=='ACTIVE'] \
             or [o for o in officers if o['status']=='ACTIVE']
    case_ev= {}
    for ev in evidence_items:
        case_ev.setdefault(ev['case_id'], []).append(ev['evidence_id'])

    count = 0
    for case in random.choices(cases, k=N_FORENSIC):
        status   = random.choices(FORENSIC_STATUSES, weights=[10,15,20,15,35,5])[0]
        sent     = (case['filed_at'] + timedelta(days=random.randint(3,45))).date()
        recv     = sent + timedelta(days=random.randint(1,7)) \
                   if status != 'REQUESTED' else None
        expected = sent + timedelta(days=random.randint(14,60))
        findings = delivered = None
        if status in ('REPORT_READY','REPORT_DELIVERED','CONTESTED'):
            findings  = random.choice([
                'Match confirmed with reference sample.',
                'No match found in database.',
                'Inconclusive — insufficient sample quality.',
                'Positive for controlled substance.',
                'Ballistic match confirmed.',
            ])
            if status == 'REPORT_DELIVERED':
                delivered = expected + timedelta(days=random.randint(-5,10))
                # CHECK: delivered >= received (if received exists)
                if recv and delivered < recv:
                    delivered = recv + timedelta(days=1)

        cur.execute(req_sql, (
            case['case_id'], random.choice(FORENSIC_LABS),
            random.choice(EXAM_PURPOSES), status,
            random.choice(auth)['officer_id'],
            sent, recv, expected, findings, delivered
        ))
        r = cur.fetchone()
        if r:
            req_id  = r[0]
            ev_pool = case_ev.get(case['case_id'], [])
            for ev_id in random.sample(ev_pool, min(random.randint(1,3), len(ev_pool))):
                cur.execute(link_sql, (req_id, ev_id, 'Submitted for analysis'))
            count += 1
    ok(f"{count} forensic requests")


# ── 13. Charge Sheets ─────────────────────────────────────────
def gen_charge_sheets(cur, cases: list, officers: list, accused: list):
    print(f"\n[13/16] Charge Sheets ({N_SHEETS})")
    sheet_sql = """
        INSERT INTO charge_sheets
            (case_id, sheet_type, charge_sheet_status,
             laws_invoked, filed_by, court_name, magistrate_name,
             filing_date, is_locked, locked_at, locked_by)
        VALUES (%s,%s::sheet_type_enum,%s::charge_sheet_status_enum,
                %s,%s,%s,%s,%s,%s,%s,%s)
        RETURNING charge_sheet_id, case_id
    """
    acc_sql = """
        INSERT INTO charge_sheet_accused
            (charge_sheet_id, accused_cnic, specific_charges, added_by)
        VALUES (%s,%s,%s,%s)
        ON CONFLICT DO NOTHING
    """
    active  = [o for o in officers if o['status']=='ACTIVE']
    case_acc= {}
    for a in accused:
        case_acc.setdefault(a['case_id'], []).append(a['cnic'])

    eligible = [c for c in cases if c['case_id'] in case_acc] or cases
    count = 0
    for case in random.choices(eligible, k=min(N_SHEETS, len(eligible))):
        status   = random.choices(SHEET_STATUSES, weights=[15,25,30,20,10])[0]
        # laws_invoked: empty ok for DRAFT, must be non-empty otherwise
        laws     = [] if status=='DRAFT' and random.random()<0.3 \
                   else random.sample(PAKISTANI_LAWS, random.randint(1,4))
        is_locked= status in ('SUBMITTED_TO_COURT','ACCEPTED_BY_COURT','REJECTED_BY_COURT')
        # filing_date required if not DRAFT
        filing_d = (case['filed_at'] + timedelta(days=random.randint(30,180))).date() \
                   if status != 'DRAFT' else None
        officer  = random.choice(active)
        # chk_lock_requires_officer: is_locked=TRUE needs locked_at AND locked_by
        locked_at = datetime.now() if is_locked else None
        locked_by = officer['officer_id'] if is_locked else None

        cur.execute(sheet_sql, (
            case['case_id'], 'ORIGINAL', status,
            laws, officer['officer_id'],
            random.choice(COURTS), random.choice(MAGISTRATES),
            filing_d, is_locked, locked_at, locked_by
        ))
        r = cur.fetchone()
        if r:
            sid      = r[0]
            acc_pool = case_acc.get(case['case_id'], [])
            adder    = random.choice(active)
            for cnic in random.sample(acc_pool, min(2, len(acc_pool))):
                # specific_charges must be non-empty (CHECK constraint)
                charges = random.sample(PAKISTANI_LAWS, random.randint(1,3))
                try:
                    cur.execute(acc_sql, (sid, cnic, charges, adder['officer_id']))
                except Exception:
                    pass   # ON CONFLICT or other — skip
            count += 1
    ok(f"{count} charge sheets")


# ── 14. Vehicles + Vehicle Cases ──────────────────────────────
def gen_vehicles(cur, persons: list, cases: list, officers: list):
    print(f"\n[14/16] Vehicles ({N_VEHICLES})")
    v_sql = """
        INSERT INTO vehicles
            (registration_number, vehicle_type, make, model,
             model_year, color, registered_owner_cnic,
             registered_owner_name, seizure_status)
        VALUES (%s,%s::vehicle_type_enum,%s,%s,%s,%s,%s,%s,
                %s::seizure_status_enum)
        RETURNING vehicle_id
    """
    vc_sql = """
        INSERT INTO Vehicle_cases
            (vehicle_id, case_id, vehicle_role, condition_notes, added_by)
        VALUES (%s,%s,%s::vehicle_role_enum,%s,%s)
        ON CONFLICT DO NOTHING
    """
    active = [o for o in officers if o['status']=='ACTIVE']
    used   = set()
    vids   = []
    for _ in range(N_VEHICLES):
        make, model = random.choice(VEHICLE_MAKES)
        own_cnic    = random.choice(persons) if random.random()<0.75 else None
        own_name    = gen_name('MALE') if own_cnic else None
        cur.execute(v_sql, (
            gen_vreg(used),
            random.choices(VEHICLE_TYPES, weights=[25,35,10,5,8,7,5,5])[0],
            make, model, random.randint(2000,2024),
            random.choice(COLORS),
            own_cnic, own_name,
            random.choices(SEIZURE_STATUSES, weights=[30,40,10,5,5,10])[0]
        ))
        r = cur.fetchone()
        if r: vids.append(r[0])

    vc_rows = []
    for vid in random.sample(vids, min(int(N_VEHICLES*0.6), len(vids))):
        case    = random.choice(cases)
        officer = random.choice(active)
        vc_rows.append((
            vid, case['case_id'], random.choice(VEHICLE_ROLES),
            random.choice(['Good condition','Damaged','Burnt','Parts missing',None]),
            officer['officer_id']
        ))
    bx(cur, vc_sql, vc_rows)
    ok(f"{len(vids)} vehicles, {len(vc_rows)} case links")


# ── 15. Patrol Routes + Duty Roster ───────────────────────────
def gen_patrol_duty(cur, stations: list, officers: list):
    print(f"\n[15/16] Patrol Routes + Duty Roster ({N_DUTY})")
    r_sql = """
        INSERT INTO Patrol_Routes
            (beat_code, route_name, area_description, landmarks,
             station_id, is_active)
        VALUES (%s,%s,%s,%s,%s,TRUE)
        RETURNING route_id, station_id
    """
    d_sql = """
        INSERT INTO Duty_Roster
            (officer_id, station_id, patrol_route_id,
             shift_type, duty_date,
             scheduled_start, scheduled_end,
             duty_status, absence_reason, assigned_by)
        VALUES (%s,%s,%s,%s::shift_type_enum,%s,%s,%s,
                %s::duty_status_enum,%s,%s)
    """
    shift_h = {'MORNING':(6,14),'AFTERNOON':(14,22),'NIGHT':(22,6),
               'SPLIT':(8,20),'ON_CALL':(0,24)}
    landmark_sets = [
        ['Main Chowk','Railway Station','General Hospital','Government School'],
        ['Bus Terminal','Central Market','Petrol Pump','Public Park'],
        ['Commercial Bank','Masjid','Shopping Mall','Police Picket'],
    ]
    beat_used = set()
    routes    = []
    for station in stations:
        for i in range(random.randint(2,4)):
            beat = gen_beat_code(beat_used, station['code'], i+1)
            cur.execute(r_sql, (
                beat,
                f"Beat {beat} — {station['code']}",
                f"Patrol area for {station['code']} station, beat {i+1}",
                random.choice(landmark_sets),
                station['station_id']
            ))
            r = cur.fetchone()
            if r:
                routes.append({'route_id': r[0], 'station_id': r[1]})
    ok(f"  {len(routes)} patrol routes")

    active = [o for o in officers if o['status']=='ACTIVE']
    sho    = [o for o in active if o['rank']=='INSPECTOR'] or active
    d_rows = []
    for _ in range(N_DUTY):
        officer  = random.choice(active)
        station  = random.choice(stations)
        s_routes = [r for r in routes if r['station_id']==station['station_id']]
        # patrol_route must belong to same station (trigger validates this)
        route_id = random.choice(s_routes)['route_id'] \
                   if s_routes and random.random()<0.70 else None
        shift    = random.choice(SHIFT_TYPES)
        d_date   = rand_date()
        sh, eh   = shift_h[shift]
        s_start  = datetime.combine(d_date, datetime.min.time()).replace(hour=sh % 24)
        # scheduled_end must be strictly AFTER scheduled_start
        # NIGHT (22→6) and ON_CALL (0→24) cross midnight → add 1 day
        crosses_midnight = eh <= sh
        s_end = datetime.combine(
            d_date + timedelta(days=1 if crosses_midnight else 0),
            datetime.min.time()
        ).replace(hour=eh % 24)
        # ON_CALL: eh=24 → 0:00 next day is correct, but 0%24=0 which is midnight
        # ensure s_end > s_start in all cases
        if s_end <= s_start:
            s_end = s_start + timedelta(hours=8)
        status   = random.choices(DUTY_STATUSES, weights=[10,5,60,10,10,5])[0]
        # ABSENT requires absence_reason (CHECK constraint)
        absence  = random.choice(['Medical leave','Personal emergency',
                                  'Family obligation','No show']) \
                   if status == 'ABSENT' else None
        assigner = random.choice(sho)
        d_rows.append((
            officer['officer_id'], station['station_id'], route_id,
            shift, d_date, s_start, s_end,
            status, absence, assigner['officer_id']
        ))
    bx(cur, d_sql, d_rows)
    ok(f"  {len(d_rows)} duty entries")


# ── 16. Rank History + Deployments ────────────────────────────
def gen_history(cur, officers: list, stations: list):
    print(f"\n[16/16] Rank History + Deployments")
    rh_sql = """
        INSERT INTO Officer_Rank_History
            (officer_id, old_rank, new_rank, promotion_type,
             effective_date, order_date, promoted_by)
        VALUES (%s,%s::officer_rank_enum,%s::officer_rank_enum,
                %s,%s,%s,%s)
    """
    dep_sql = """
        INSERT INTO Officer_Deployments
            (officer_id, from_station_id, to_station_id,
             deployment_reason, order_number,
             deployed_from, deployed_until, deployed_by, is_active)
        VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s)
    """
    rank_idx = {r: i for i, r in enumerate(OFFICER_RANKS)}
    dsp_plus = [o for o in officers if o['rank'] in DSP_PLUS and o['status']=='ACTIVE'] \
               or [o for o in officers if o['status']=='ACTIVE']

    rh_rows, dep_rows = [], []
    for officer in random.sample(officers, min(100, len(officers))):
        curr = rank_idx.get(officer['rank'], 0)
        if curr == 0: continue
        n   = random.randint(1, min(curr, 3))
        base= curr - n
        for i in range(n):
            eff  = rand_date(date(2005,1,1), date(2024,12,31))
            rh_rows.append((
                officer['officer_id'],
                OFFICER_RANKS[base + i],
                OFFICER_RANKS[base + i + 1],
                random.choice(['REGULAR','ACTING']),
                eff, eff,   # effective_date, order_date (order_date <= effective_date ok)
                f"SP/PROMO/{random.randint(100,999)}/{eff.year}"
            ))

    for officer in random.sample(officers, min(50, len(officers))):
        deployer = random.choice(dsp_plus)
        from_s   = random.choice(stations)
        to_s     = random.choice([s for s in stations
                                   if s['station_id'] != from_s['station_id']] or stations)
        dep_from = rand_date()
        dep_till = dep_from + timedelta(days=random.randint(30,365)) \
                   if random.random()<0.7 else None
        if dep_till and dep_till <= dep_from:
            dep_till = dep_from + timedelta(days=30)
        is_active = dep_till is None or dep_till > date.today()
        dep_rows.append((
            officer['officer_id'],
            from_s['station_id'], to_s['station_id'],
            random.choice(['Emergency reinforcement','Temporary shortage',
                           'Special operation','Training attachment']),
            f"DEP/{random.randint(100,999)}/{dep_from.year}",
            dep_from, dep_till,
            deployer['officer_id'],
            is_active
        ))

    if rh_rows:  bx(cur, rh_sql, rh_rows)
    if dep_rows: bx(cur, dep_sql, dep_rows)
    ok(f"  {len(rh_rows)} rank history entries")
    ok(f"  {len(dep_rows)} deployment entries")


# ══════════════════════════════════════════════════════════════
#  CLEAR TABLES
# ══════════════════════════════════════════════════════════════
def clear_all(cur):
    print("\n⚠  Clearing all tables...")
    # audit.Audit_Log has a trigger blocking DELETE — use TRUNCATE instead
    # TRUNCATE bypasses row-level triggers
    truncate_tables = [
        'analytics.Officer_Workload_Assignments',
        'analytics.Case_Priority_Scores',
        'analytics.Crime_Hotspots',
        'analytics.Model_Performance_Log',
        'audit.Audit_Log',
        'Evidence_Custody_Log',
        'Evidence',
    ]
    delete_tables = [
        'charge_sheet_accused', 'charge_sheets',
        'Vehicle_cases', 'vehicles',
        'Duty_Roster', 'Patrol_Routes',
        'Forensic_Request_Evidence', 'Forensic_Lab_Requests',
        'Bail_Records', 'Arrests', 'Warrants',
        'Accused_Associations', 'Accused',
        'Witnesses', 'Victims', 'Complainants',
        'Case_Jurisdiction_History', 'Case_Status_Log',
        'Case_Officers', 'Cases',
        'Officer_Deployments', 'Officer_Rank_History',
        'Officers', 'Persons',
        'Sequence_Registry', 'Stations',
    ]
    for t in truncate_tables:
        cur.execute(f'TRUNCATE {t} CASCADE')
        print(f"    truncated {t}")
    for t in delete_tables:
        cur.execute(f'DELETE FROM {t}')
        print(f"    cleared {t}")


# ══════════════════════════════════════════════════════════════
#  MAIN
# ══════════════════════════════════════════════════════════════
def main():
    print("=" * 58)
    print("  JusticeFlow — Synthetic Data Generator  v3")
    print("  Sindh Province | Karachi Focus | 2020 – 2025")
    print("  Schema-verified against 02_tables.sql")
    print("=" * 58)

    if '--clear' in sys.argv:
        do_clear = True
    else:
        do_clear = False

    try:
        conn = psycopg2.connect(**DB_CONFIG)
        conn.autocommit = False
    except Exception as e:
        print(f"\n❌  Cannot connect: {e}")
        sys.exit(1)

    cur = conn.cursor()
    try:
        # Don't set app.current_officer_id to a string —
        # audit function casts it to BIGINT with exception handler (returns NULL safely)

        if do_clear:
            clear_all(cur)
            conn.commit()
            print("  ✓ All tables cleared\n")

        stations = gen_stations(cur);          conn.commit()
        persons  = gen_persons(cur);           conn.commit()
        officers = gen_officers(cur, persons, stations);       conn.commit()
        cases    = gen_cases(cur, stations, officers, persons); conn.commit()
        gen_case_officers(cur, cases, officers);               conn.commit()
        accused  = gen_case_roles(cur, persons, cases, officers); conn.commit()
        ev_items = gen_evidence(cur, cases, officers);         conn.commit()
        gen_custody_log(cur, ev_items, officers);              conn.commit()
        warrants = gen_warrants(cur, cases, officers, persons); conn.commit()
        arrests  = gen_arrests(cur, cases, officers, accused, warrants); conn.commit()
        gen_bail_records(cur, arrests, persons, officers);     conn.commit()
        gen_forensic(cur, cases, officers, ev_items);          conn.commit()
        gen_charge_sheets(cur, cases, officers, accused);      conn.commit()
        gen_vehicles(cur, persons, cases, officers);           conn.commit()
        gen_patrol_duty(cur, stations, officers);              conn.commit()
        gen_history(cur, officers, stations);                  conn.commit()

        # ── Final count ───────────────────────────────────────
        print("\n" + "=" * 58)
        print("  ✓  Generation complete — Final row counts:")
        print("=" * 58)
        tables = [
            ('Stations','public'),('Persons','public'),
            ('Officers','public'),('Cases','public'),
            ('Case_Officers','public'),('Complainants','public'),
            ('Victims','public'),('Witnesses','public'),
            ('Accused','public'),('Evidence','public'),
            ('Evidence_Custody_Log','public'),('Warrants','public'),
            ('Arrests','public'),('Bail_Records','public'),
            ('Forensic_Lab_Requests','public'),
            ('Forensic_Request_Evidence','public'),
            ('charge_sheets','public'),('charge_sheet_accused','public'),
            ('vehicles','public'),('Vehicle_cases','public'),
            ('Patrol_Routes','public'),('Duty_Roster','public'),
            ('Officer_Rank_History','public'),
            ('Officer_Deployments','public'),
            ('Sequence_Registry','public'),
            ('Audit_Log','audit'),
        ]
        total = 0
        for tbl, schema in tables:
            q = f'SELECT COUNT(*) FROM {schema}.{tbl}' if schema != 'public' \
                else f'SELECT COUNT(*) FROM {tbl}'
            cur.execute(q)
            n = cur.fetchone()[0]
            total += n
            print(f"  {tbl:<38} {n:>6} rows")
        print(f"  {'─'*45}")
        print(f"  {'TOTAL':<38} {total:>6} rows")
        print("=" * 58)

    except Exception:
        conn.rollback()
        print("\n❌  Error — transaction rolled back.")
        traceback.print_exc()
        sys.exit(1)
    finally:
        cur.close()
        conn.close()

if __name__ == '__main__':
    main()