# Squirt UX — Serial Protocol Reference

This documents the serial protocol used by the **current_lean** web UX client
(Web Serial API, `115200` baud, 8N1). It is derived from the client source:

- Transmit (UI → device): [listeners.js](javaScript/listeners.js), [serial.js](javaScript/serial.js)
- Receive (device → UI): [serial.js](javaScript/serial.js) `readLoop()` / `statusHandler()` / `dataHandler()`

> ℹ️ The firmware in `include/messages.h` is aligned to this protocol.
> **Ground_Com** is the bridge: it parses these single-letter serial commands
> from the UI and forwards them to Float_Cont over ESP-NOW (`GroundToFloat`),
> and converts the `FloatStatus` / `FloatDataPoint` structs it gets back into the
> `Status` / `data` JSON lines documented below. The UI continues to speak
> line-based ASCII/JSON over USB serial; the binary structs only exist on the
> internal ESP-NOW/UART links.

---

## Transport / Framing

| Direction | Format | Framing | Encoding |
|-----------|--------|---------|----------|
| UX → Device (commands) | Plain text, `LETTER [arg]` | One command per line, terminated with `\n` | UTF-8 (`TextEncoderStream`) |
| Device → UX (responses) | JSON object | One JSON object per line, terminated with `\n` | UTF-8 (`TextDecoderStream`) |

- The client appends `\n` to every command it sends (`writeToStream`).
- The client buffers incoming bytes and splits on `\n`, then `JSON.parse()`s each
  line. Lines that fail to parse are still echoed to the on-screen terminal but
  are otherwise ignored.
- Commands are **case-sensitive single letters**. Arguments (where present) are
  separated by a single space.

---

## Commands the Client Can **Transmit** (UX → Device)

| Command | Argument | UI trigger | Meaning |
|---------|----------|-----------|---------|
| `S` | — | "Status" request button | Request a status update. Device should reply with a `Status` JSON message. |
| `N <name>` | team/float name string | "Set Name" button | Set the device/team name. Client rejects an empty name. |
| `C` | — | "Calibrate" button | Trigger calibration. Reflected back via `calibration` flag in `Status`. |
| `T <epoch>` | Unix epoch **seconds** | "Set Time" button | Set the device clock. Client sends `Math.floor(Date.now()/1000)`. Reflected via `timeSet` flag in `Status`. |
| `M <value>` | motor/ballast position | "Manual" input / Enter key | Set motor position to an arbitrary value. |
| `M 100` | (fixed) | "Up" button | Convenience: full up. |
| `M 50` | (fixed) | "Neutral" button | Convenience: neutral. |
| `M 0` | (fixed) | "Down" button | Convenience: full down. |
| `P` | — | "Profile" button | Start / run the dive profile. |
| `D` | — | "Data" button | Request stored data dump. Device should stream `data` JSON messages. |
| `R` | — | "Clear Data" / "Clear All" buttons | Reset / clear stored data on the device. Guarded by a confirm dialog. |

**Free-form terminal:** the manual serial input box (`sendSerialLine`) sends
whatever the user types, verbatim + `\n`. So in practice the device may receive
any arbitrary line in addition to the buttons above.

### Quick reference (wire format)

```
S
N <name>
C
T <epochSeconds>
M <value>        // 100 = up, 50 = neutral, 0 = down via buttons
P
D
R
```

---

## Messages the Client Can **Receive, Handle, and Decode** (Device → UX)

Every inbound line is parsed as JSON. The client dispatches on the `type` field
(`switch(msg.type)` in `readLoop`). Two types are handled; any other type is
echoed to the terminal but not acted on.

> Note the casing: the status type is `"Status"` (capital **S**) and the data
> type is `"data"` (lowercase **d**).

### 1. `Status` message → `statusHandler()`

Updates the dashboard status fields and the calibration/time indicator lights.

| Field | Type | Used for |
|-------|------|----------|
| `type` | string | Must be `"Status"` to route here. |
| `team` | string | Team/name status display. |
| `depth` | number/string | Depth status display. |
| `pressure` | number/string | Pressure status display. |
| `temperature` | number/string | Temperature status display. |
| `battery` | number/string | Battery status display. |
| `time` | number (epoch **seconds**) | Shown as a localized date/time (`new Date(time*1000)`). |
| `calibration` | boolean | Truthy → calibrate indicator turns **green**, else **red**. |
| `timeSet` | boolean | Truthy → time indicator turns **green**, else **blue**. |

> `position` is referenced in the source but commented out — not currently
> displayed.

Example:

```json
{"type":"Status","team":"Devil Fish","depth":1.42,"pressure":101.3,"temperature":22.5,"battery":87,"time":1718900000,"calibration":true,"timeSet":true}
```

### 2. `data` message → `dataHandler()`

Appends a point to the depth-vs-time Plotly graph.

| Field | Type | Used for |
|-------|------|----------|
| `type` | string | Must be `"data"` to route here. |
| `time` | number (epoch **seconds**) | X-axis value (`new Date(time*1000)`). |
| `depth_m` | number | Y-axis value (depth in meters). |
| `reset` | boolean (optional) | If present and truthy, the graph is cleared **before** this point is added. Use on the first point of a fresh dump. |

Example:

```json
{"type":"data","time":1718900001,"depth_m":1.55}
```

First point of a new dump (clears the existing trace first):

```json
{"type":"data","reset":true,"time":1718900000,"depth_m":0.0}
```

---

## Behavior Notes

- **Unhandled JSON types** (any `type` other than `Status`/`data`, or JSON with
  no `type`) are echoed to the on-screen terminal but trigger no handler.
- **Non-JSON lines** are echoed to the terminal (the `JSON.parse` throws and is
  caught); they do not update any UI element.
- **Graph data** is stored only in the browser (Plotly `trace1`). "Download CSV"
  exports it with headers `Time,Depth`; there is no separate data-decode beyond
  the `data` messages above.
- **Indicator color logic:** calibrate → green/red; time → green/blue.
