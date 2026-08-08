# Protocol Discovery and Confidence

> **Unofficial reverse-engineering notes.** This document records observed behavior and controlled tests. It is not a vendor protocol specification and may not apply to other controller revisions.

## Method

The work began with passive RS-485 capture so the original controller and display could communicate without injected traffic. Candidate baud rates and serial formats were compared until stable repeated frames established `19200 baud, 8 data bits, no parity, 1 stop bit`.

Packet boundaries and repeated byte patterns led to the following framing model:

```text
STX + escaped(command + payload) + ETX + XOR checksum
```

- `STX`: `0x02`
- `ETX`: `0x03`
- `DLE`: `0x10`
- Reserved command or payload bytes are preceded by `DLE`.
- The checksum is the XOR of the unescaped command and payload bytes.

The original panel was then operated one function at a time. Captures before, during, and after each action were compared. Candidate command payloads were replayed only after their timing relationship to normal bus traffic was understood.

## Timing discovery

Arbitrary transmissions were unreliable. Successful virtual-key inputs occurred in a short response window following received command `0x30`. Controlled trials produced the retry delay sequence used by the component:

```text
7 ms, 6 ms, 8 ms, 5 ms, 7 ms, 6 ms, 8 ms, 5 ms
```

The RS-485 transceiver enters transmit mode, waits 2 ms, sends and flushes the frame, then returns to receive mode. Command `0x38` is treated as input acknowledgement. A neutral `00 00 00` key state is sent afterward in another command-`0x30` response slot.

## Verified controls

All virtual-key inputs use command `0x80` with a three-byte payload.

| Function | Payload | Hold time | Verification |
| --- | --- | --- | --- |
| Pump A | `00 02 01` | 300 ms | Command-`0x30` state and physical transition |
| Pump B | `00 04 01` | 300 ms | Command-`0x30` state and physical transition |
| Heater pump cycle | `10 00 01` | 200 ms | Command-`0x30` state and physical transition |
| Blower cycle | `80 00 01` | 300 ms | Acknowledgement; state text observed on display |
| Key release | `00 00 00` | None | Sent after each press |

## State decoding

Only the following command-`0x30` fields are decoded:

| Equipment | Zero-based payload rule | Result |
| --- | --- | --- |
| Pump A | Payload byte 2, mask `0x10` | Set = on |
| Pump B | Payload byte 2, mask `0x80` | Set = on |
| Heater enabled | `(payload[2] & 0x03) == 0x03` | False = off |
| Heater mode | Payload byte 3, mask `0x04` | Set = auto; clear = low |

Display commands `0x21` and `0x22` contain text used to report observed blower states such as `OFF`, `RAMPING`, `ON`, and `ON HIGH`. This is display-derived feedback and has lower confidence than the physically validated binary pump mappings.

## Confidence rules

- **Physically verified:** A controlled input was followed by both expected binary feedback and an observed equipment transition.
- **Observed:** Traffic or display behavior was repeatable, but no independent binary equipment mapping was established.
- **Unknown:** Meaning was not established with sufficient evidence. Unknown bytes remain uninterpreted.

An acknowledgement confirms that the controller accepted an input; it does not by itself prove that equipment reached the requested state. The component therefore requires state feedback for Pump A, Pump B, and heater-pump actions.

## Scope decisions

The ESPHome component intentionally omits features that were observed but did not meet the same safety or feedback threshold. This includes lighting cycles, menu navigation, setpoint editing, schedules, default loading, raw frame transmission, and undecoded status fields.

The component is designed to coexist with normal controller traffic and serializes actions so only one virtual key operation can run at a time. It is not a replacement for equipment interlocks, temperature limits, flow protection, or electrical safety devices.
