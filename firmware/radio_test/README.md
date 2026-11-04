# radio_test

Checks whether ESP-NOW gets out of the keyboard's aluminium case before anything is
soldered in. One S3 Mini goes in the keyboard as `sender`; the bench S3 Mini (see
CLAUDE.md) sits where the core lives as `receiver` and prints what it hears.

1. Flash both (commands in `platformio.ini`). Set `RADIO_TEST_CHANNEL` to your access
   point's channel first: the core is on that channel, so the keyboard will be too.
2. Open the receiver's monitor. With the sender on the bench next to it you should see
   `rx 10/10` and RSSI around -30 to -50.
3. Power the sender from a small LiPo or power bank (battery + into its 5V pin works),
   put it in the keyboard where Keychron's own antenna lead ends, and close the case.
4. Leave the receiver running a few minutes with the keyboard in its normal place, then
   try a few other spots for the sender.

RSSI above about -80 dBm with under 5% lost is plenty: the keyboard only sends the odd
request, and it resends until the core's STATE shows it. If it is much worse, try the
`sender_lr`/`receiver_lr` pair (ESP-NOW long-range mode), then a board with a u.FL
antenna connector.
