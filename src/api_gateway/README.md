# api_gateway

`api_gateway` is the module boundary that exposes a **dashboard connection endpoint**
for the JusticeFlow daemon.

## What it does (today)

- Exposes a Unix domain socket (default: `/tmp/justiceflow.sock`)
- Accepts dashboard connections
- Serves a read-only JSON request/response protocol

## What it hides

This module intentionally **abstracts** OS-level IPC details. Internals currently
delegate to `os_layer/ipc` (e.g. `ipc::DomainSocket`) but those types are not part
of the public API.

## Wire protocol (for dashboard client)

Transport: Unix domain socket, stream.

Framing per message:

- `[4 bytes little-endian uint32 payload_len][payload bytes: UTF-8 JSON]`

Request JSON schema:

```json
{
  "request_id": 42,
  "command": "GET_CASE_LIST",
  "params": { "limit": "100", "offset": "0" }
}
```

Response JSON schema:

```json
{
  "request_id": 42,
  "status": 0,
  "message": "OK",
  "data": {}
}
```

The full command catalogue currently lives in `os_layer/ipc/include/socket_request.h`.

## Future

- Add authenticated + write commands
- Move routing into `api_gateway::CommandRouter`
- Enforce token/rank/duty rules via `api_gateway::AuthValidator`
