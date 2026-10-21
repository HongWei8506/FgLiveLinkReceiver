# FaceGood LiveLink Receiver Behavior

> Engine version: UE 5.8
>
> Last updated: 2026-10-21

This document describes the Runtime behavior of `FgLiveLinkReceiver` when used
with Live Link Hub, MetaHuman Live Link, and the existing FaceGood UDP JSON
protocol.

## 1. Architecture

The supported data flow is:

```text
FaceGood application
    -> UDP unicast or multicast JSON
    -> FgLiveLinkReceiver
    -> LiveLinkHub Basic Role subjects
    -> LiveLinkHub recording/routing
    -> UE client LiveLink
    -> ABP_MH_LiveLink
    -> MetaHuman
```

LiveLinkHub receives and distributes Live Link subjects. It does not evaluate
MetaHuman Control Rig data itself. The UE client consumes each subject with the
standard MetaHuman Live Link animation Blueprint.

## 2. Wire Protocol

The UDP wire format is unchanged. A payload may contain one or more top-level
subject objects:

```json
{
  "FACEGOOD": {
    "FrameId": 1,
    "Properties": [
      {
        "Name": "CTRL_L_eye_blink",
        "Value": 0.5
      },
      {
        "Name": "CTRL_C_jaw.translateX",
        "Value": 0.1
      },
      {
        "Name": "CTRL_C_jaw.translateY",
        "Value": 0.2
      }
    ],
    "Joints": {
      "Names": ["Root", "Head"],
      "ParentIdx": [-1, 0],
      "Transforms": [
        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
      ]
    }
  }
}
```

Subject names are case-sensitive and become Live Link Subject names.

### 2.1 Datagrams

- Transport: UDP Datagram.
- Encoding: UTF-8.
- Unicast and multicast are supported.
- A datagram may contain multiple complete JSON objects.
- A JSON object may span multiple ordered datagrams.
- Split fragments must arrive in order.
- Incomplete peer state expires after 2 seconds.
- Pending data is limited to 4 MiB per sender.
- Invalid JSON or an invalid subject is skipped without stopping the receiver.

The receiver cannot provide ACK, sender retransmission, or recovery of lost
application-level fragments without additional sender metadata. These
requirements would need a protocol revision or a reliable transport.

## 3. Live Link Output

The source publishes `ULiveLinkBasicRole` subjects using
`FLiveLinkBaseStaticData` and `FLiveLinkBaseFrameData`.

### 3.1 Property Names

Original property names are preserved. MetaHuman channel aliases are also
added:

| Input component | Added MetaHuman alias |
|---|---|
| `.translateX` | `.tx` |
| `.translateY` | `.ty` |
| `.translateZ` | `.tz` |

Scalar `CTRL_` properties also receive a `.ty` alias, except for
`CTRL_expressions_` and `CTRL_rigLogic_` properties.

These aliases allow the existing FaceGood protocol to feed MetaHuman raw
Control Rig channel names without changing the sender.

### 3.2 Supplemented Properties

If the sender does not provide the following properties, the receiver appends
them with these defaults:

| Property | Default |
|---|---:|
| `HeadControlSwitch` | `0` |
| `HeadRoll` | `0` |
| `HeadPitch` | `0` |
| `HeadYaw` | `0` |
| `HeadTranslationX` | `0` |
| `HeadTranslationY` | `0` |
| `HeadTranslationZ` | `0` |
| `MHFDSVersion` | `1` |
| `DisableFaceOverride` | `1` |

Explicit values from the sender take precedence.

### 3.3 Joints

The current Basic Role output sends float properties only. The `Joints` object
is retained in the protocol for compatibility, but its transforms are not
published by this source. Bone/transform streaming would require a separate
Animation Role subject or another driving layer.

## 4. Frame Ordering

Each subject tracks its own `FrameId`.

- A new or forward-skipped frame is accepted.
- A duplicate `FrameId` is rejected.
- A small backwards `FrameId` is treated as a stale datagram and rejected.
- A backwards jump greater than 600 frames is treated as a sender restart.
- Missing `FrameId` is allowed, but duplicate detection is unavailable.

This prevents stale UDP packets from moving an animation backwards.

## 5. Scene Time and Frame-rate Estimation

Live Link frames contain `MetaData.SceneTime`, a frame number qualified by a
frame rate.

### 5.1 Stable Scene Time

`SceneTime.Rate` is intentionally fixed at the standard nominal rate:

```text
30/1 fps
```

The sender protocol does not contain an authoritative frame rate or timestamp.
Using an arbitrary estimated rate directly in `SceneTime.Rate` can produce a
non-standard frame rate that UE cannot display correctly in some locales.

The nominal SceneTime rate should remain stable. It is used for timecode,
timeline, and playback interpretation. It is not intended to represent every
instantaneous frame interval.

### 5.2 Estimated Source Rate

The receiver estimates the effective input rate from `FrameId` progression and
UDP arrival time:

```text
effective FPS = frame-id delta / arrival-time delta
```

Estimator behavior:

- Statistics window: approximately 0.2 seconds.
- Minimum window: 6 samples.
- Smoothing factor: `0.35`.
- Valid range: 1-240 fps.
- A large backwards `FrameId` jump resets the estimate.

The estimate is metadata only and does not change `SceneTime.Rate`.

### 5.3 Frame Metadata

The frame metadata contains:

| Metadata key | Meaning |
|---|---|
| `EstimatedSourceFPS` | Smoothed effective input rate, three decimal places |
| `SceneTimeFrameRate` | Nominal SceneTime fraction, currently `30/1` |

For exact timing, the sender should eventually provide an authoritative frame
rate, timestamp, or timecode.

## 6. LiveLinkHub Usage

1. Install the plugin in a directory visible to LiveLinkHub.
2. Enable `FgLiveLinkReceiver` in the Hub Plugin Browser.
3. Restart LiveLinkHub.
4. Open the Sources panel and add `FaceGood LiveLink Receiver`.
5. Bind an address such as `0.0.0.0:54321`.
6. Start sending FaceGood JSON to the Hub machine and UDP port.

A green subject indicates active incoming data. A yellow subject indicates a
stale or disconnected subject.

### 6.1 Inspecting Received Data

1. Select the active subject.
2. Open the Details panel.
3. Use `View Options > Show Static Data` to inspect `PropertyNames`.
4. Use `View Options > Show Frame Data` to inspect `FrameId`,
   `PropertyValues`, and metadata.
5. Adjust `Refresh Delay` for faster updates.

Static and frame property arrays use the same index. For example:

```text
PropertyNames[10] = CTRL_C_jaw.ty
PropertyValues[10] = 0.23
```

## 7. MetaHuman Client

For a standard UE 5.8 MetaHuman:

1. Connect the UE Editor client to LiveLinkHub.
2. Confirm the subject appears in `Window > Live Link`.
3. Open the MetaHuman Character asset.
4. Select `Live Link` as the preview animation controller.
5. Select the same Subject name used by the Hub.

For a level character, use the standard MetaHuman animation Blueprint and set
the subject property:

```text
Anim Class: ABP_MH_LiveLink_C
LLink_Face_Subj: <subject name>
```

`FgControlRig` is not required for the standard MetaHuman Live Link path.

## 8. Compatibility Notes

- The source supports UE 5.8 and LiveLinkHub Program builds.
- The plugin declares `SupportedPrograms: ["LiveLinkHub"]`.
- Win64 is the currently declared supported platform.
- The protocol remains compatible with older FaceGood senders.
- Existing `Properties` and `Joints` field names are unchanged.
- No engine or LiveLinkHub source modification is required for normal plugin
  installation.

## 9. Diagnostics

If a source appears but no subject is created:

- Confirm the sender targets the Hub machine and UDP port.
- Confirm each top-level JSON key is a subject name.
- Check that `Properties` is an array.
- Check Hub firewall and UDP receive rules.
- Confirm the Subject name case matches the UE client and MetaHuman settings.

If data is active but the frame-rate row is unexpected:

- Check `SceneTimeFrameRate` in frame metadata.
- Check `EstimatedSourceFPS` for the arrival-rate estimate.
- Remember that non-standard instantaneous rates are not used as SceneTime.
