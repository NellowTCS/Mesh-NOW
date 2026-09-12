# TODO

- [ ] Add tests - zero unit/integration tests exist, PR template claims they pass
- [ ] Document encryption threat model - beacons/ACKs are deliberately unencrypted; state what this protects against and what it does not
- [ ] Audit ESP-NOW peer capacity vs MAX_PEERS - esp_now default allows 6 peers while the table allows 20; confirm UX and config
- [ ] MQTT transport bridging - extract transport abstraction layer, ESP-NOW adapter, MQTT adapter, broker config, pub/sub topic schema per node. Major architectural refactor.
