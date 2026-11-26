# JusticeFlow end-to-end report

This report covers the work that took JusticeFlow from a set of parts that did not run together to a system you can start with one command and check with another. It lists what was broken, what changed, how it was verified, and what is still open.

## 1. Result

From a clean checkout, with PostgreSQL running and admin credentials in the usual `PG*` variables:

```bash
make demo     # reset and seed the database, build, start the daemon and dashboard
make smoke    # run the demo story and check every step
make demo-stop
```

`make smoke` makes 22 checks and passes. It exercises the full story through the same doors a user would use:

| Step | What is checked |
|------|-----------------|
| Gateway | `PING` answers; an anonymous request for cases is refused |
| Sign-in | A wrong password is rejected with a generic message; an Inspector signs in; the session is counted |
| FIR | An FIR is registered with a generated number; an injection attempt in the address is stored as plain text and the `cases` table survives |
| Evidence | Evidence is recorded with a generated number |
| Warrant | A warrant is issued |
| Expiry | After the warrant's dates are moved into the past, the daemon's scheduler marks it `EXPIRED` |
| AI agents | All three agents ran and report OK; hotspot, priority and workload data exist |
| Audit | The new rows are attributed to the officer's id and belt number; the application role cannot `UPDATE` or `DELETE` audit rows |
| Dashboard | The case page shows the FIR, the evidence and the expired warrant |

The seed data is 22,405 rows including 150 officers (all with the development password `justice123`).

## 2. Architecture as it now runs

```
Flask dashboard ──(Unix socket, length-prefixed JSON)──▶ C++ daemon ──▶ PostgreSQL
  signed cookie holds                │                        ▲
  the session token                  ├─ CommandRouter: LOGIN/LOGOUT/WHOAMI, token gate, writes
                                     ├─ scheduler thread: warrant expiry, runs the 3 AI agents
                                     └─ shared memory: session count, agent status
                                                              │
                          Python agents (justice_ai role) ────┘ write to the analytics schema
```

The dashboard never opens a database connection. Every read and write goes through the gateway, and every write runs as the signed-in officer.

## 3. What was fixed, by phase

Each phase was a pull request authored as Furqan with Claude as co-author.

### Phase 0: a database that builds and seeds (PR #20)

| Problem | Fix |
|---------|-----|
| The schema aborted partway through the `Officers` table because of a stray `;` | Removed it |
| A CNIC check used a regex that PostgreSQL read differently from what was intended | Corrected in `schema_patch.sql`, which is now part of `init-db` |
| `crypt()` and `gen_salt()` were used without the `pgcrypto` extension | The extension is created in `00_schema.sql` |
| The seeder's SQL placeholders did not match its parameters; officers had no real password hash | Fixed; officers are inserted with a bcrypt hash of `justice123`; the seeder reads `PGHOST`, `PGPORT`, `PGDATABASE`, `PGUSER`, `PGPASSWORD` |
| Role creation failed on a second run | Wrapped in a guard so it can be repeated |
| No single command to rebuild | Added `make db-reset` |

### Phase 1: the OS layer boots (PR #21)

| Problem | Fix |
|---------|-----|
| The daemon exited as soon as standard input closed | Added `--headless` and `--pid-file`; shutdown waits for SIGINT or SIGTERM using `sigwait()` on the main thread |
| `SIGTERM` could hang the daemon: on Linux, `close()` does not wake a thread blocked in `accept()` | The listening socket is `shutdown()` first |
| The shared status table could not be used: its condition variable was never initialised and the segment name did not match the one the code used | `create()` zeroes the table, initialises the process-shared condition variable and sets agent names; one `SHM_NAME` constant |
| Database connection and shared memory were never set up before the gateway started | `initialize()` now does both, in the documented order, and shutdown releases them |

### Phase 2: the read path (PRs #22, #23)

| Problem | Fix |
|---------|-----|
| The application role had no privileges on the tables it queried | Grants in `06_permissions.sql`: select, insert and update on `public`, select on `analytics`, sequence access; no delete anywhere |
| The request parser rejected any request whose `params` contained a nested value | Balanced-brace parsing for nested objects |
| Five handlers queried columns and tables that do not exist | Rewritten against the real schema (case list and detail, hotspots, priority cases, officer workload) |
| The dashboard had its own database connection and mock data | Rebuilt as a Flask app that uses only the gateway (`dashboard/client/gateway.py`); server-rendered pages, no CDN, live polling that writes with `textContent` only. The old Streamlit prototype is kept under `dashboard/legacy/` |

### Phase 3: authentication and writes (PRs #24, #25, #26)

| Problem | Fix |
|---------|-----|
| Any client could read any data without signing in | A `CommandRouter` handles `LOGIN`, `LOGOUT`, `WHOAMI` and refuses protected reads and all writes without a valid token |
| `AuthManager` had a login bypass: setting `JF_TEST_AUTH_FALLBACK` let anyone sign in without a database check | Removed |
| Brute-force guessing was unrestricted | Five failed logins per CNIC lock it for five minutes; "no such CNIC", "wrong password" and "locked" return the same message |
| Only four of eleven ranks could log in (ASI and Head Constable were refused), and `validateRank` used a different scale from the one its callers pass | All ranks parse; rank comparisons use the enum's seniority order, which matches the database enum |
| Writes were built by concatenating strings into SQL, so they were injectable, and the audit log's `changed_by` columns were empty | New `UnixSocket::executeAs()`: one locked transaction that sets `app.current_officer_id`, runs a statement with bound parameters, then commits or rolls back |
| FIR, evidence and warrant writes did not exist on the gateway | `REGISTER_FIR`, `ADD_EVIDENCE`, `GET_CASE_EVIDENCE`, `ISSUE_WARRANT`, `GET_CASE_WARRANTS`, with business rules (ASI/SI/Inspector file FIRs, Inspector or above request warrants, only at the officer's own station and only on open cases) checked in the router and again by the existing database triggers |
| The dashboard could not sign in or write | `/login`, `/logout` (POST), a signed HttpOnly session cookie, a Register FIR form, evidence and warrant sections on the case page; redirect targets restricted to local paths |

### Phase 4: the daemon runs the background work (PR #27, merged to `main` through #28)

| Problem | Fix |
|---------|-----|
| The scheduler, warrant expiry and agent spawner were never started | `SchedulerRunner`: a one-second ticker thread started with the daemon (the original SIGALRM design conflicts with `sigwait()`-based shutdown) |
| `ExpireWarrantsJob` queried columns that do not exist | Rewritten: `ISSUED` warrants whose `valid_until` has passed become `EXPIRED` |
| Agent spawning used FIFOs and a per-agent protocol that did not match the C++ side, and read the result before the agent had written it | `AgentJob` starts each agent with `posix_spawn`, reaps it with `waitpid`, kills it on timeout, terminates it at shutdown, and records the outcome (0, the exit code, or 124 for a timeout) in shared memory |
| `workload_agent.py` wrote its own struct at offset 0 of the shared segment, which is where the daemon's robust mutex lives, so running it beside the daemon could corrupt the table | Both of its IPC writers are now no-ops; the daemon is the only writer |
| `ha.py` and `pa.py` printed an error and exited 0 when the database was unreachable, so a failed run looked healthy | They exit non-zero on failure |
| Agents ran with the daemon's own database login | The child gets the `justice_ai` role from `JF_AI_DB_*` only |

### Phase 5: demo, verification and cleanup (PR #28)

| Problem | Fix |
|---------|-----|
| `main` did not compile: PR #14 added a second `main()` (`dashboard_bridge.cpp`) that the Makefile's source glob pulled into the daemon, and it failed `-Werror` | Removed it, its Python client and a 24,000-line `json.hpp` nothing used; the gateway replaces all three |
| No one-command start and no end-to-end check | `make demo`, `make demo-stop`, `make smoke` |
| A hardcoded password (`justiceflow123`) sat in `dbconfig.h`, two scripts, two agent stubs and the tests | Removed everywhere; the C++ config has no default password; the setup script generates one; the old agent stubs are deleted; tests use the schema's actual role password |
| The README described cmake builds and `db/` paths that do not exist | Getting-started, tests and security sections rewritten; repository tree corrected |

## 4. Security changes in one list

- No unauthenticated reads or writes through the gateway.
- Removed the environment-variable login bypass.
- Parameterised SQL for every client-driven write; the injection test is part of the smoke test.
- Audit rows record the real officer and belt number, and the application role cannot change or delete them.
- Generic login failures, per-CNIC throttling, session tokens from the operating system's random source.
- The dashboard keeps its token in a signed HttpOnly cookie and cannot reach the database.
- The agents' database role is separate from the daemon's, and the agents can no longer touch the daemon's shared memory.
- No hardcoded database password remains in the tree.

## 5. Still open

These are known and were not changed, so nobody should assume they are done.

**Act on this first.** The old password `justiceflow123` is in the public repository's git history. Treat it as exposed and change it anywhere it was used.

| Item | Detail |
|------|--------|
| Legacy subsystem code | `src/system/subsystem1`, `subsystem2` and `subsystem3` (and the CLI built on them) are not on the gateway's request path. Subsystem 2 builds SQL by string concatenation and has an incomplete case-type mapping; subsystems 1 and 3 query schemas (`subsystem3.warrants` and similar) that do not exist. The gateway implements the demo commands directly instead. Either port these classes to the real schema and parameterised SQL, or retire them |
| CLI mode | Starting `justiceflow` without `--headless` runs the interactive CLI over those legacy classes. It was not tested |
| Unit and integration binaries | The C++ test targets (`make list-tests`) were not run as part of this work; some had a wrong database password, which is now corrected, but their status is unknown |
| Thread pool, timer, signal handler | The `ThreadPool`, `Timer` and `SignalHandler` classes are not started by the daemon; nothing on the request path uses them |
| Development role passwords | The schema creates `justice_app` and `justice_ai` with passwords equal to their names, and `make demo` uses them. Fine for a laptop demo; change them for anything shared |
| Warrant dates | `valid_until` is a date, so a warrant cannot expire on the day it is issued. The demo simulates time passing by moving a warrant's dates back as the database admin |
| Seed data | Every seeded case has the same `filed_at`, which flattens any ordering by filing time. The workload query returns `on_duty` as `"t"` or `"f"` rather than a boolean |
| Sessions and lockouts | Held in the daemon's memory; they are lost on restart and not shared between daemons |
| Dashboard hardening | Flask's development server, no CSRF tokens (the cookie is `SameSite=Lax`), and the cookie is not marked `Secure` because the demo runs over plain HTTP. Put it behind a production server and HTTPS before exposing it |
| Gateway capacity | One thread per client connection and a single serialised database connection; fine for a demo, not tuned for load |
| Agent schedule | Agents run every five minutes by default (`JF_AGENT_INTERVAL_S`); analytics tables are insert-only by design, so they grow with every run |

## 6. Operating notes

- PostgreSQL must be running. `make demo` stops with a clear message if the admin connection fails.
- The daemon needs `JF_DB_PASS` (and `JF_AI_DB_PASS` for the agents). `scripts/demo.sh` sets development defaults; set your own to override them.
- Useful variables: `JF_AGENT_INTERVAL_S` (300), `JF_EXPIRE_INTERVAL_S` (30, 5 under `make demo`), `JF_AGENT_SCRIPT_DIR`, `JF_DASHBOARD_PORT`, `JF_DASHBOARD_SECRET`, `JF_SMOKE_WAIT`.
- Logs from `make demo` are in `.demo/`.
- On Linux, `close()` on a listening socket does not wake a thread blocked in `accept()`. If shutdown ever hangs again, look there first.
