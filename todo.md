# TODO

- [x] Add root ESP-IDF project or fix docs - CI workflows now use `path: 'examples/chat-app'` in esp-idf-ci-action
- [x] Fix release.yml paths - build-all uses esp-idf-ci-action with correct path, packaging uses examples/chat-app/builds/
- [x] Add thread safety to peers[], pending_messages[], seen_message_ids[] - FreeRTOS mutex protects all shared state
- [x] Fix timestamps - mesh-relative time sync via beacons, first beacon sets epoch reference
- [ ] Add tests - zero unit/integration tests exist, PR template claims they pass
- [ ] MQTT transport bridging - extract transport abstraction layer, ESP-NOW adapter, MQTT adapter, broker config, pub/sub topic schema per node. Major architectural refactor.
