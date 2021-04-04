
OpenDrakan network protocol (draft)
===================================

This document contains considerations for the network protocol I'm planning to
use in OpenDrakan. Initially, this will be incompatible with the original Drakan
protocol, but once the latter is sufficiently reverse-engineered, we might
specify a translation layer.

Tick number
-----------
At 60 ticks per second, a uint32 would overflow after about 2.2 years. uint64
would overflow after 9.8 billion years.

Tick numbers should be one-based, since some protocol features need to be able
to pass an invalid tick. E.g. when sending snapshots, it needs to be indicated
whether the snapshot is full or delta encoded against another snapshot.

Asset references
----------------
Some network packets need to reference assets (like animation or sound events).
However, asset references are context-dependent: their database index references
the dependency table of the database from which the reference originated. This
is suboptimal for networking, as the context of a reference is not always clear
(like when playing sounds).

To fix this, the server maintains a table in which the names/paths of all
databases it has loaded are associated with a 16-bit index. This table is sent
to clients during the handshake. A client will use the names to create a table
that maps these server DB indices to it's local DB indices.

Snapshot transmission
---------------------
Snapshots are sent to a client in two steps: First, an arbitrary number of
change packets referring to the snapshot using it's tick number are sent. Then,
a *confirmation* packet is transmitted, which states how many discrete changes
the snapshot with the given tick should contain, at which server time it was
taken, and possibly more metadata.

Upon reception of a confirmation packet, a client marks it's local version of
the snapshots as confirmed and stores the expected number of changes for that
snapshot. Once a confirmed snapshot contains as many changes as stated by the
confirmation, it is complete and can be moved into the local timeline.

Packet loss will cause either the confirmation or a number of changes to be
missing, so the expected change count will never match the one recorded. Thus,
an incomplete snapshot will not be considered by the client.

This scheme is also independent of the packet's order upon arrival.

Object States
-------------
Objects possess *states*, which covers things like position, rotation, animation
etc. These basic states are defined by the engine. Additionally, the RFL class
of an object can define *extra states*, which are sent in a separate channel.

States are also how the engine handles persistence across play sessions by
writing the states that changed since loading the level to a savegame.

State serialization
-------------------
Collections of states ("state bundles") are stored in a special, mildly
optimized format. It is a good idea to only send states that have changed since
the last acknowledged snapshot, so the binary state bundle format must indicate
which states are contained and which are not. Additionally, any state change can
be marked as a jump, indicating that it should not be interpolated.

This is done using bitmasks of (currently) 8 bits. First, the *value mask* is
written, in which a set bit indicates that the state with the bit's respective
index is contained in the bundle. After that, the *jump mask* is stored, where a
set bit indicates the state with the bit's index is a jump. Starting from state
0 up to state 6, these are then serialized and appended directly after the
bitmasks.

If a state bundle contains a state with a higher index than 6, the highest bit
in the value mask is set to indicate that after the blob of serialized lower
states there is another set of bitmasks that follow the same format as the
first, only that these ones now apply to the states 7 to 13. Serialization is
repeated in the same manner until all states have been serialized. Note that the
highest bit of the jump mask is currently unused.

Boolean states are handled specially. If 8 or less boolean states are serialized
after each other, they are combined into a single byte, starting with the least
significant bit. This makes it desireable to always group boolean states
together.

Part of the state structure could be expressed as such:
```
u8 valueMask0;
u8 jumpMask0;
u8 serializedStates0[*]; // size depends on states that are serialized, not included in packet
if(valueMask0 & 0x80)
{
    u8 valueMask1;
    u8 jumpMask1;
    u8 serializedStates1[*];
}
```

Object states have a fixed order which is as follows:
```
vec3 translation;
quat rotation;
vec3 scale;
bool visibility;
bool running;
```

Data structures
---------------
```
struct vec2
{
    f32 x;
    f32 y;
}

struct vec3
{
    f32 x;
    f32 y;
    f32 z;
}

struct quat
{
    f32 i;
    f32 j;
    f32 k;
    f32 w;
}
```

Basic packet structure
----------------------
```
u8 packet_type;
u16 payload_length;
u8 payload_bytes[~];
```

Handshake packets
-----------------
These packets are used to set up the engine when a client first connects to the
server.

### Global database table entry (reliable, downlink)
Transmits one entry of the global DB index table.
```
u32 global_index;
u8  path[]; // path of db file, relative to engine root. size derived from packet size
```

### Load level (reliable, downlink)
The total number of databases that are loaded is included in this packet so the
client can verify that it received a complete database table.
```
u32 total_loaded_db_count;
u8 level_path[]; // relative to engine root
```

Snapshot packets
----------------
These are used to send snapshots from the server to a client. Thus, they are
only valid in the downlink. A client does not send state updates to the server.

### Confirm Snapshot
reference_tick_number is the tick of the snapshot that was used for delta-
encoding. It may 0 to indicate this is a full snapshot.
```
u64 tick_number;
f64 realtime;
u32 number_of_discrete_changes;
u64 reference_tick_number;
```

### Object State Changed
```
u64 tick_number;
u32 object_id;
statebundle states;
```

### Object Extra State Changed
```
u64 tick_number;
u32 object_id;
statebundle states;
```

### Object Created
```
u64 tick_number;
u32 new_object_id;
u32 prototype_object_id; // currently, new objects can only be created as clones of exisiting objects
```

### Object lifecycle state changed
```
u64 tick_number;
u32 object_id;
u8 new_state; // 0 = loaded, 1 = spawned, 2 = running, 3 = destroyed
```

### Acknowledge snapshot (uplink)
Used by the client to tell the server that it has received a snapshot in full.
This is not strictly necessary, but the server can only use ACK'd snapshots in
delta encoding. That means the longer a client fails to ACK a snapshot, the
larger the snapshots sent by the server will become.
TODO: confirm and acknowledge are kinda ambiguous. maybe rename one of the two?
```
u64 tick_number;
```

Message Channel packets
-----------------------
These packets allow an RFL to send arbitrary messages over so called "channels".
Messages have no timing guarantee. They must not be used for predicting
state changes. While these are reliable (i.e. sent over TCP), they can arrive
in any tick, which might be future or past on the client. Any timing correction
must be handled by the receiver.

### Global message (downlink, reliable)
```
u32 channel_code;
f64 real_time; // server time at which the message was sent
u8  message_body[];  // size is to be derived from payload size field of the packet
```

### Object message (downlink, reliable)
```
u32 channel_code;
u32 sender_object_id;
u32 receiver_object_id;
f64 real_time;
u8  messageBody[];  // size is to be derived from payload size field of the packet
```

Event packets
-------------
These represent one-shot events that happen at a certain time, but are not
guaranteed to arrive at a given time. The receiver makes sure these are not
processed before they happened, but might need to account for delays. It is best
to send these as soon as possible.

### Object animation (downlink, reliable)
```
f64  realtime;
u32  id;
gref anim_ref;
i32  channel_index;
f32  speed_modifier;
```

### Sound (downlink, reliable)
```
f64  realtime;
gref sound_ref;
```

### Action (uplink, reliable)
```
f64 realtime;
u32 action_code;
u8  action_state;
```

### Analog Action (uplink, reliable)
```
f64 realtime;
u32 action_code;
f32 x;
f32 y;
```
