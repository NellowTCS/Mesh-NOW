# TODO

- [ ] Add root ESP-IDF project or fix docs - quickstart, integration.yml, dependabot.yml assume it exists
- [ ] Fix release.yml paths - does "cd builds" at repo root but output is in examples/chat-app/builds/
- [ ] Add thread safety to peers[], pending_messages[], seen_message_ids[] - mutated from multiple tasks with no locks
- [x] Actually check MSG_FLAG_REQUIRES_ACK - retransmit_task now skips messages without the flag, configurable retry limit via Kconfig
- [ ] Fix timestamps - ms-since-boot is meaningless across nodes, web UI displays them
- [x] Add component Kconfig for tunable params - beacon interval, TTL, retries, queue size, peer expiry, seen buffer size
- [ ] Add tests - zero unit/integration tests exist, PR template claims they pass
- [ ] MQTT transport bridging - extract transport abstraction layer, ESP-NOW adapter, MQTT adapter, broker config, pub/sub topic schema per node. Major architectural refactor.
