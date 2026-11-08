# Security

QtLiveInspector is a **remote-control interface for applications**: anyone
who can talk to the probe can click, type, read and write every property of
every object in the target process. That power is the point of the tool —
and exactly why its attack surface is documented here.

## Trust model

```
operator machine                    target machine
┌──────────────┐   JSON over TCP    ┌──────────────────────────────┐
│ qli_cli.py   │ ◄────────────────► │ probe (injected into the app) │
│ inspector GUI│   newline-JSON     │  • full object-tree access    │
│ qli_mcp.py   │                    │  • synthesized input          │
└──────────────┘                    │  • arbitrary slot invocation  │
                                    └──────────────────────────────┘
```

The probe is a *debugging and test instrument*, not a production service.
Inject it only into applications and environments you control.

## Defaults and what they protect

| Control | Default | Meaning |
|---|---|---|
| Bind address | `127.0.0.1` | Only local processes can connect. |
| Auth token | auto-generated | A random 128-bit token, printed once to the target's log (`auth token: ...`). Every command must present it. |
| Read-only mode | off | `QLI_READONLY=1` accepts only observation commands (`hello`, `tree`, `properties`, `screenshot`, `perfStats`, `isIdle`, `locator`) and rejects every mutation. |
| Audit log | on | Every authenticated mutating command is logged to the target's log with peer address and target object. |

## Sharp edges — read before exposing a probe

- **`QLI_HOST=any` (or `0.0.0.0`) puts a full remote-control endpoint on
  every interface of the machine.** Combined with `typeText`, arbitrary slot
  invocation (`invoke`) and property writes, this is equivalent to handing
  out shell access to the application's user. Only use it on isolated
  test networks, and always keep token auth on.
- **`QLI_TOKEN=` (explicitly empty) disables authentication.**
  Intended for development on a single user's machine. The probe logs a loud
  warning. Never combine with `QLI_HOST=any`.
- **The token travels as a JSON field on a plain-TCP socket.** Loopback
  traffic is not encrypted; any local process running as the same user can
  read it (or simply read the target's log). A determined local attacker
  with the same UID already has easier ways to control the app — the token
  protects against *casual* and *remote* misuse, not against same-user
  malware.
- **`invoke` runs any slot with a JSON-typed argument list.** Slots named
  on objects reachable from the tree are callable. In read-only mode this
  is rejected.
- **Read-only mode is for safety rails, not sandboxing.** Property reads
  can still return sensitive data shown in UI fields (passwords typed into
  plain text fields, etc.). Do not point a probe at applications handling
  secrets you would not print to a log.

## The standalone input visualizer

`libqtinspectorinputviz.so` has **no network surface at all** — no sockets,
no listener, no protocol. It only draws touch feedback into the application.
It is safe to ship in demos and on embedded devices where the full probe
would be unacceptable.

## Hardening checklist for shared/remote use

1. Keep the loopback bind (`QLI_HOST` unset) unless the network exposure is
   intentional and the segment is trusted.
2. Keep token auth on; distribute the token over a channel that is not the
   target's stdout when the log is broadly readable.
3. Prefer `QLI_READONLY=1` when the operator only needs to observe.
4. Run the target with the lowest privileges it needs — the probe inherits
   them.
5. Remember the audit log: review the target's log for unexpected commands.

## Reporting

Security issues can be reported by opening an issue on the project
repository (or contacting the maintainers listed in the git history).
Please include the probe version (`hello` response), how it was injected,
and the relevant target log lines.
