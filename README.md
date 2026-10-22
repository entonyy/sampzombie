# zombie_ai.inc

Smooth zombie behaviour for SA-MP servers using **FCNPC** and **ColAndreas**.

Drop it in, spawn zombies with one function, and they:

- **Wander** around their spawn point in smooth arcs (no 180° snaps, no standing frozen between steps).
- **Spot** players within 15 m who are in plain sight (a ColAndreas line-of-sight check, so they don't see through walls).
- **Chase**, aiming slightly ahead of where the player is running so they cut corners instead of trailing behind.
  - They keep going toward the last known spot for 2.5 s after losing sight, instead of freezing at the first corner.
- **Attack** at close range, and claw at vehicles if the player is inside one.
- **Hear gunshots** and walk over to investigate (each weapon has its own range; a silenced pistol is quiet, a sniper carries far).
- **Spread out**: a group splits over several survivors instead of everyone piling onto one.
- **React to hits**:
  - Bullets make them stagger briefly.
  - Melee stuns them, with a 10% chance to knock them down. They stand back up and keep coming.
- **Patrol underneath** players they can't reach (on a roof, a container or a high ledge) instead of grinding into the wall.

Most of the code is about keeping FCNPC movement **smooth and believable**:
- no stutter from re-pathing every tick
- no walking through walls
- no popping on top of buildings
- no "moonwalking" (walk animation playing while standing still)

---

## Requirements

| | |
|---|---|
| Server | SA-MP 0.3.7 or newer (0.3.DL works) |
| Plugins | **FCNPC** and **ColAndreas** |
| Includes | `a_samp`, `FCNPC`, `colandreas` |

MapAndreas is **not** needed.

---

## Installation

1. Copy `zombie_ai.inc` into your `pawno/include` folder.
2. Include it **after** FCNPC and ColAndreas:

```pawn
#include <a_samp>
#include <FCNPC>
#include <colandreas>
#include <zombie_ai>
```

3. Make sure ColAndreas is loaded before spawning zombies. Call `CA_Init()` in `OnGameModeInit` if your server doesn't already, since every movement check uses it.
4. Spawn zombies with `ZAI_Create(...)`. That's it.

**Your existing callbacks keep working.** The include hooks these callbacks with the standard ALS method:
- `OnGameModeInit`
- `OnFilterScriptInit`
- `OnPlayerDisconnect`
- `OnPlayerWeaponShot`
- `FCNPC_OnReachDestination`
- `FCNPC_OnTakeDamage`
- `FCNPC_OnDeath`

You can still write your own versions of all of them, and both run. For NPCs that aren't zombies, the FCNPC callbacks are passed straight to your code.

---

## Quick start

```pawn
#include <a_samp>
#include <FCNPC>
#include <colandreas>
#include <zombie_ai>

public OnGameModeInit()
{
    CA_Init();

    // 10 walkers around Grove Street
    for(new i = 0; i < 10; i++)
        ZAI_Create(2495.0 + random(20), -1680.0 + random(20), 13.5, 162);

    // one runner, 150 HP, hits for 20
    ZAI_Create(2500.0, -1670.0, 13.5, 230, ZAI_TYPE_RUNNER, 150.0, 20.0);
    return 1;
}

// respawn every zombie 30 seconds after it dies
public ZAI_OnZombieDeath(zombieid, killerid, weaponid)
{
    new Float:x, Float:y, Float:z;
    FCNPC_GetPosition(ZAI_GetNPC(zombieid), x, y, z);
    SetTimerEx("RespawnZombie", 30000, false, "fff", x, y, z);
    return 1;
}

forward RespawnZombie(Float:x, Float:y, Float:z);
public RespawnZombie(Float:x, Float:y, Float:z)
{
    ZAI_Create(x, y, z, 162);
}
```

---

## Functions

### Spawning
```pawn
ZAI_Create(Float:x, Float:y, Float:z, skin, type = ZAI_TYPE_WALKER, Float:health = 100.0, Float:damage = 15.0, world = 0, interior = 0)
```
Spawns a zombie and returns its **zombie id** (not the FCNPC id), or `ZAI_INVALID` (-1) if the pool is full or FCNPC couldn't create the NPC.

| Type | Behaviour |
|---|---|
| `ZAI_TYPE_WALKER` | Normal shambling walk |
| `ZAI_TYPE_RUNNER` | Shambles when idle, sprints when chasing |
| `ZAI_TYPE_CRAWLER` | Slow |
| `ZAI_TYPE_BRUTE` | Slightly slower than a walker (give it more health and damage) |

```pawn
ZAI_Destroy(zombieid)          // remove instantly, no death animation or callback
ZAI_IsValid(zombieid)          // 1 if the zombie id is in use
```

### Information
```pawn
ZAI_GetNPC(zombieid)             // FCNPC npcid of the zombie (use with any FCNPC_* function)
ZAI_GetZombieFromNPC(npcid)      // the reverse; ZAI_INVALID if that NPC isn't a zombie
ZAI_GetState(zombieid)           // ZAI_STATE_IDLE, ZAI_STATE_CHASE, ZAI_STATE_ATTACK or ZAI_STATE_DEAD
ZAI_GetTarget(zombieid)          // player it's chasing/attacking, or INVALID_PLAYER_ID
ZAI_GetType(zombieid)
Float:ZAI_GetDamage(zombieid)
ZAI_CountTargeting(playerid)     // how many zombies are on this player right now
```

### Control
```pawn
ZAI_SetDamage(zombieid, Float:damage)
ZAI_SetAnchor(zombieid, Float:x, Float:y)    // the point it wanders around (its spawn by default)
ZAI_SetTarget(zombieid, playerid)            // force it onto a player (scripted aggro)
ZAI_Stun(zombieid, ms)                       // freeze it in place for ms
ZAI_Halt(zombieid)                           // stop movement, attack and walk animation
```

### Noise
```pawn
ZAI_AlertNoise(Float:x, Float:y, Float:z, Float:radius, world = 0, interior = 0)
```
Every idle zombie within `radius` walks over to investigate for 12 seconds. Gunshots call this automatically. Use it for anything else loud, like explosions, car alarms, breaking a door, mining or a campfire:

```pawn
ZAI_AlertNoise(x, y, z, 40.0);   // a car alarm
```

```pawn
Float:ZAI_GetWeaponNoise(weaponid)   // how far a gunshot of that weapon carries (0.0 = silent)
```

---

## Callbacks (all optional)

Define any of these in your script to plug the zombies into your own systems.

```pawn
public ZAI_OnZombieCanTarget(zombieid, playerid)
```
Return **0** to stop zombies targeting this player. Use it for admin duty, AFK players, safe zones, spawn protection and similar.

```pawn
public ZAI_OnZombieCanTarget(zombieid, playerid)
{
    if(IsPlayerAdminOnDuty(playerid)) return 0;
    return 1;
}
```

```pawn
public ZAI_OnZombieAttack(zombieid, playerid, Float:damage)
```
A zombie hits a player on foot.
- Return **1** to let the include remove `damage` health.
- Return **0** if you handle it yourself (infection, bleeding, a wounded state, armour first...).

```pawn
public ZAI_OnZombieAttack(zombieid, playerid, Float:damage)
{
    if(random(100) < 15) InfectPlayer(playerid);   // your own system
    return 1;                                     // still apply the damage
}
```

```pawn
public ZAI_OnZombieTakeDamage(zombieid, issuerid, Float:amount, weaponid, bodypart)
```
A zombie was hurt. Return **0** to cancel the damage; it still staggers and turns on the attacker. To apply your own damage values (headshot multipliers and so on), return 0 and change the health yourself with `FCNPC_SetHealth`.

```pawn
public ZAI_OnZombieDeath(zombieid, killerid, weaponid)
```
A zombie died. It plays a death animation, lies there for `ZAI_CORPSE_TIME` ms, and then the NPC is destroyed and the slot freed. Spawn loot, give XP or schedule a respawn here.

---

## Settings

`#define` any of these **before** `#include <zombie_ai>` to change them.

| Setting | Default | What it does |
|---|---|---|
| `ZAI_MAX_ZOMBIES` | 100 | Pool size |
| `ZAI_NPC_NAME_PREFIX` | `"Zombie_"` | NPC names become `Zombie_0`, `Zombie_1`... |
| `ZAI_UPDATE_INTERVAL` | 1000 | AI tick in ms |
| `ZAI_CHASE_DISTANCE` | 15.0 | Spot range |
| `ZAI_IDLE_DISTANCE` | 25.0 | Gives up the chase past this range |
| `ZAI_ATTACK_DISTANCE` | 2.0 | Starts attacking inside this range |
| `ZAI_ATTACK_BREAK_DISTANCE` | 3.0 | Only stops attacking past this range (prevents flickering) |
| `ZAI_ATTACK_COOLDOWN` | 2000 | ms between hits on a player |
| `ZAI_VEHICLE_COOLDOWN` | 5000 | ms between hits on a vehicle |
| `ZAI_VEHICLE_DAMAGE` | 1.0 | Vehicle health removed per hit |
| `ZAI_LOS_GRACE_MS` | 2500 | Keeps chasing this long after losing sight |
| `ZAI_CHASE_REPATH_DIST` | 6.0 | Only re-paths once the target moved this far (anti-stutter) |
| `ZAI_WANDER_RADIUS` | 10.0 | Wander range around the anchor |
| `ZAI_MAX_Z_DIFF` | 2.5 | Players this far above can't be chased |
| `ZAI_UNREACHABLE_Z_DIFF` | 1.5 | Player this far above the ground under them counts as "on a roof" |
| `ZAI_HORDE_SPREAD` | 4.0 | How strongly zombies spread over several players |
| `ZAI_INVESTIGATE_TIME` | 12000 | ms a zombie follows a noise |
| `ZAI_GUNSHOT_NOISE` | `true` | Set to `false` to turn off automatic gunshot noise |
| `ZAI_NOISE_THROTTLE_MS` | 350 | Min gap between gunshot noise events per player |
| `ZAI_GUN_STAGGER_MS` | 350 | Stagger per bullet hit |
| `ZAI_MELEE_STUN_MS` | 800 | Stun per melee hit |
| `ZAI_KNOCKDOWN_CHANCE` | 10 | % chance a melee hit knocks it down |
| `ZAI_CORPSE_TIME` | 5000 | ms the body stays before the NPC is destroyed |

Example:
```pawn
#define ZAI_MAX_ZOMBIES     200
#define ZAI_CHASE_DISTANCE  20.0
#include <zombie_ai>
```

---

## Notes and tips

- **Virtual worlds and interiors:** a zombie only sees and hears players in its own world and interior, so zombies outside don't attack players inside houses at the same coordinates.
- **Zombie ids vs NPC ids:** every `ZAI_*` function takes the zombie id returned by `ZAI_Create`. Use `ZAI_GetNPC` when you need the FCNPC id, for example to set a skin or weapon with FCNPC functions.
- **Damage numbers:** the include uses FCNPC's damage amount as-is. For custom weapon damage, use `ZAI_OnZombieTakeDamage`.
- **Performance:** around 100 zombies and 50 players is fine with the default 1-second tick. For much bigger numbers, raise `ZAI_UPDATE_INTERVAL` a little.
- **Compiler warning 239** ("literal array/string passed to a non-const parameter"): this comes from SA-MP's own `a_samp.inc` together with newer compilers. FCNPC's include triggers it too. It's harmless. Most servers silence it with `#pragma warning disable 239` at the top of the gamemode.

---

## Credits

Zombie behaviour from **Dead State Roleplay**.
