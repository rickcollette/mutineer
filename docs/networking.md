# Networking / Protocols

- Telnet is default. For TLS/SSH place a terminator in front: stunnel, HAProxy `mode tcp`, or sshd `ForceCommand`. Point terminator to `mutineer` port (default 2929).
- Recommend: stunnel service `accept = 992`, `connect = 127.0.0.1:2929`, `protocol = proxy` for logging client IPs.
- FTN/netmail: queued netmail can be exported with `mutineer-netmail-export`, which writes outbound packet files, records them in `mail_packets`, and marks source rows sent after successful export.
- Protocol registry: configure `protocols` table; Mutineer will call the first active entry matching direction.
