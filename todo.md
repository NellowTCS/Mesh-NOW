# TODO

- [ ] Add root ESP-IDF project or fix docs - quickstart, integration.yml, dependabot.yml assume it exists
- [ ] Fix release.yml paths - does "cd builds" at repo root but output is in examples/chat-app/builds/
- [ ] Add thread safety to peers[], pending_messages[], seen_message_ids[] - mutated from multiple tasks with no locks
- [ ] Actually check MSG_FLAG_REQUIRES_ACK - set but retransmit_task ignores it
- [ ] Fix timestamps - ms-since-boot is meaningless across nodes, web UI displays them
- [ ] Add component Kconfig for tunable params - beacon interval, TTL, retries, queue size all require editing source
- [ ] Add tests - zero unit/integration tests exist, PR template claims they pass
- [x] Fix routing mermaid diagram - was showing hop=0 reaching destination, code drops at hop=0
- [x] Fix message-queue.md struct - showed 3 fields, actual has 6 (added type, group_id, target_mac)
- [ ] MQTT transport bridging - extract transport abstraction layer, ESP-NOW adapter, MQTT adapter, broker config, pub/sub topic schema per node. Major architectural refactor.
- [x] Split mesh_now.c monolith - 1114 lines split into core/, codec/, crypto/, net/, queue/ subdirectories
