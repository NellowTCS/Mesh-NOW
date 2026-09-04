# TODO

- [x] Fix peer table lifecycle - add_peer reactivates expired peers; beacon expiry compacts the table so peer_count stays the count of active peers
- [x] Use configured WiFi channel for ESP-NOW peers - replace hardcoded peer.channel = 1 with the channel source of truth shared with wifi_manager
- [ ] Add tests - zero unit/integration tests exist, PR template claims they pass
- [ ] Document encryption threat model - beacons/ACKs are deliberately unencrypted; state what this protects against and what it does not
- [ ] Audit ESP-NOW peer capacity vs MAX_PEERS - esp_now default allows 6 peers while the table allows 20; confirm UX and config
- [ ] MQTT transport bridging - extract transport abstraction layer, ESP-NOW adapter, MQTT adapter, broker config, pub/sub topic schema per node. Major architectural refactor.
