# Implementation plan and gates

1. Inspect current EutherVox, EutherNet, EutherID and repository ownership. Done;
   evidence and integration gaps in ECOSYSTEM.md.
2. Implement one host vertical slice before networking a physical node. Done:
   learning, NEC/SIRC decode, persistent signals, scoped execution, history,
   durable duplicate protection, mock and HTTP integration tests.
3. Implement ESP-IDF RMT bench node and USB adapter. Source complete; cross-build
   verification recorded in VALIDATION.md. Select actual chip/pins only after
   hardware identification. No flash or emission is authorized by a build alone.
4. First controlled physical capture/replay. Await board/LED/receiver/power details
   and assembled hardware. Record original remote, target, waveform, build hash,
   current/carrier checks and observed device response. Do not mark A/B passed
   from host simulation.
5. Add authenticated native node networking and remote learning with enrollment,
   TLS, persistent request identity, explicit node binding and heartbeat expiry.
6. Wire verified Vox gateway route and EutherID service contract; test authenticated
   Swedish utterance through actual target response. Never interpret free-form
   model output as a pulse command.
7. Agree EutherNet registration/collector interface, then multiple-node recovery
   and configuration migrations. Discovery never grants permission to transmit.
