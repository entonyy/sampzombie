/*
 * zombie_ai.inc - smooth FCNPC zombie behaviour for SA-MP
 * ----------------------------------------------------------------------------
 * Taken from the Dead State Roleplay gamemode and made standalone.
 *
 * What it does: wandering, spotting, chasing, attacking, line of sight, noise
 * (gunshots) attraction, horde spreading, stagger/knockdown, and a lot of work
 * to keep FCNPC movement smooth: no stutter when re-pathing, no walking through
 * walls, no popping onto rooftops, no moonwalking in place.
 *
 * Requires: SA-MP 0.3.7+ (or 0.3.DL), FCNPC (include v2.x), ColAndreas.
 * Include order:
 *     #include <a_samp>
 *     #include <FCNPC>
 *     #include <colandreas>
 *     #include <zombie_ai>
 *
 * See README.md for the full API and examples.
 */

#if defined _zombie_ai_included
	#endinput
#endif
#define _zombie_ai_included

#if !defined FCNPC_INCLUDE_VERSION
	#error "zombie_ai: include <FCNPC> before <zombie_ai>"
#endif
#if !defined CA_RayCastLine
	#error "zombie_ai: include <colandreas> before <zombie_ai>"
#endif

// ============================================================================
// SETTINGS - #define any of these BEFORE including the file to change them.
// ============================================================================
#if !defined ZAI_MAX_ZOMBIES
	#define ZAI_MAX_ZOMBIES          100     // pool size
#endif
#if !defined ZAI_NPC_NAME_PREFIX
	#define ZAI_NPC_NAME_PREFIX      "Zombie_"
#endif
#if !defined ZAI_UPDATE_INTERVAL
	#define ZAI_UPDATE_INTERVAL      1000    // AI tick (ms)
#endif
#if !defined ZAI_CHASE_DISTANCE
	#define ZAI_CHASE_DISTANCE       15.0    // spots players within this range (with line of sight)
#endif
#if !defined ZAI_IDLE_DISTANCE
	#define ZAI_IDLE_DISTANCE        25.0    // drops the chase past this range
#endif
#if !defined ZAI_ATTACK_DISTANCE
	#define ZAI_ATTACK_DISTANCE      2.0     // starts attacking inside this range
#endif
#if !defined ZAI_ATTACK_BREAK_DISTANCE
	#define ZAI_ATTACK_BREAK_DISTANCE 3.0    // ...and only stops past this one (no flip-flopping)
#endif
#if !defined ZAI_ATTACK_COOLDOWN
	#define ZAI_ATTACK_COOLDOWN      2000    // ms between hits on a player on foot
#endif
#if !defined ZAI_VEHICLE_COOLDOWN
	#define ZAI_VEHICLE_COOLDOWN     5000    // ms between hits on a player's vehicle
#endif
#if !defined ZAI_VEHICLE_DAMAGE
	#define ZAI_VEHICLE_DAMAGE       1.0     // vehicle health removed per hit
#endif
#if !defined ZAI_LOS_GRACE_MS
	#define ZAI_LOS_GRACE_MS         2500    // keeps chasing the last known spot this long after losing sight
#endif
#if !defined ZAI_CHASE_REPATH_DIST
	#define ZAI_CHASE_REPATH_DIST    6.0     // only re-issue a chase path once the target moved this far
#endif
#if !defined ZAI_WANDER_RADIUS
	#define ZAI_WANDER_RADIUS        10.0    // wander legs are picked within this radius of the anchor
#endif
#if !defined ZAI_MAX_Z_DIFF
	#define ZAI_MAX_Z_DIFF           2.5     // players this far above the zombie can't be chased
#endif
#if !defined ZAI_UNREACHABLE_Z_DIFF
	#define ZAI_UNREACHABLE_Z_DIFF   1.5     // player this far above the ground under them = on a roof/container
#endif
#if !defined ZAI_HORDE_SPREAD
	#define ZAI_HORDE_SPREAD         4.0     // each zombie already on a player makes them "4m further away"
#endif
#if !defined ZAI_INVESTIGATE_TIME
	#define ZAI_INVESTIGATE_TIME     12000   // ms a zombie keeps walking to a noise before giving up
#endif
#if !defined ZAI_GUNSHOT_NOISE
	#define ZAI_GUNSHOT_NOISE        true    // gunshots automatically attract zombies (see ZAI_GetWeaponNoise)
#endif
#if !defined ZAI_NOISE_THROTTLE_MS
	#define ZAI_NOISE_THROTTLE_MS    350     // min gap between gunshot noise events per player (full-auto guard)
#endif
#if !defined ZAI_GUN_STAGGER_MS
	#define ZAI_GUN_STAGGER_MS       350     // a bullet hit halts the zombie for this long
#endif
#if !defined ZAI_MELEE_STUN_MS
	#define ZAI_MELEE_STUN_MS        800     // a melee hit stuns for this long
#endif
#if !defined ZAI_KNOCKDOWN_CHANCE
	#define ZAI_KNOCKDOWN_CHANCE     10      // % chance a melee hit knocks the zombie down
#endif
#if !defined ZAI_CORPSE_TIME
	#define ZAI_CORPSE_TIME          5000    // ms the body lies there before the NPC is destroyed
#endif

// Zombie types (movement/animation profiles)
#define ZAI_TYPE_WALKER  0
#define ZAI_TYPE_RUNNER  1
#define ZAI_TYPE_CRAWLER 2
#define ZAI_TYPE_BRUTE   3

// States (ZAI_GetState)
#define ZAI_STATE_DEAD   (-1)
#define ZAI_STATE_IDLE   0
#define ZAI_STATE_CHASE  1
#define ZAI_STATE_ATTACK 2

#define ZAI_INVALID      (-1)

// ============================================================================
// CALLBACKS you can define in your script (all optional)
// ============================================================================
// Return 0 to stop zombies targeting this player (admin duty, AFK, safe zone...).
forward ZAI_OnZombieCanTarget(zombieid, playerid);
// Zombie hits a player on foot. Return 1 to apply the damage, 0 if you handle it yourself.
forward ZAI_OnZombieAttack(zombieid, playerid, Float:damage);
// Zombie was hurt. Return 0 to cancel the damage (it still reacts).
forward ZAI_OnZombieTakeDamage(zombieid, issuerid, Float:amount, weaponid, bodypart);
// Zombie died. The body stays ZAI_CORPSE_TIME ms, then the NPC is destroyed.
forward ZAI_OnZombieDeath(zombieid, killerid, weaponid);

// ============================================================================
// INTERNAL STATE
// ============================================================================
enum E_ZAI
{
	bool:zUsed,
	zNPC,
	zType,
	Float:zDamage,
	zState,
	zTarget,
	zWorld,
	zInterior,
	Float:zAnchorX,
	Float:zAnchorY,
	Float:zPathX,
	Float:zPathY,
	Float:zPrevX,
	Float:zPrevY,
	Float:zPrevZ,
	Float:zNoiseX,
	Float:zNoiseY,
	Float:zNoiseZ,
	zInvestigateUntil,
	zLastSeen,
	zLastAttack,
	zStunUntil,
	zLastAttacker
};
static ZAI[ZAI_MAX_ZOMBIES][E_ZAI];
static bool:zai_Initialised = false;
static zai_Timer = 0;
static bool:zai_HasCanTarget, bool:zai_HasAttack, bool:zai_HasTakeDamage, bool:zai_HasDeath;
static zai_LastNoise[MAX_PLAYERS];

static stock Float:zai_Dist3D(Float:x1, Float:y1, Float:z1, Float:x2, Float:y2, Float:z2)
{
	return floatsqroot((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1) + (z2 - z1) * (z2 - z1));
}

// ============================================================================
// PUBLIC API
// ============================================================================

// Spawns a zombie. Returns its zombie id, or ZAI_INVALID if the pool is full / FCNPC failed.
stock ZAI_Create(Float:x, Float:y, Float:z, skin, type = ZAI_TYPE_WALKER, Float:health = 100.0, Float:damage = 15.0, world = 0, interior = 0)
{
	zai_Init();
	new id = ZAI_INVALID;
	for(new i = 0; i < ZAI_MAX_ZOMBIES; i++) if(!ZAI[i][zUsed]) { id = i; break; }
	if(id == ZAI_INVALID) return ZAI_INVALID;

	new name[MAX_PLAYER_NAME];
	format(name, sizeof(name), "%s%d", ZAI_NPC_NAME_PREFIX, id);
	new npc = FCNPC_Create(name);
	if(npc == INVALID_PLAYER_ID) return ZAI_INVALID;

	FCNPC_Spawn(npc, skin, x, y, z);
	FCNPC_SetHealth(npc, health);
	FCNPC_SetVirtualWorld(npc, world);
	FCNPC_SetInterior(npc, interior);

	ZAI[id][zUsed] = true;
	ZAI[id][zNPC] = npc;
	ZAI[id][zType] = type;
	ZAI[id][zDamage] = damage;
	ZAI[id][zState] = ZAI_STATE_IDLE;
	ZAI[id][zTarget] = INVALID_PLAYER_ID;
	ZAI[id][zWorld] = world;
	ZAI[id][zInterior] = interior;
	ZAI[id][zAnchorX] = x;
	ZAI[id][zAnchorY] = y;
	ZAI[id][zPathX] = 0.0;
	ZAI[id][zPathY] = 0.0;
	ZAI[id][zPrevX] = 0.0; // 0.0 = watchdog not armed until a real position is recorded
	ZAI[id][zPrevY] = 0.0;
	ZAI[id][zPrevZ] = 0.0;
	ZAI[id][zInvestigateUntil] = 0;
	ZAI[id][zLastSeen] = 0;
	ZAI[id][zLastAttack] = 0;
	ZAI[id][zStunUntil] = 0;
	ZAI[id][zLastAttacker] = INVALID_PLAYER_ID;
	return id;
}

// Removes a zombie immediately (no death animation, no callback).
stock ZAI_Destroy(zombieid)
{
	if(!ZAI_IsValid(zombieid)) return 0;
	if(FCNPC_IsValid(ZAI[zombieid][zNPC])) FCNPC_Destroy(ZAI[zombieid][zNPC]);
	ZAI[zombieid][zUsed] = false;
	ZAI[zombieid][zNPC] = INVALID_PLAYER_ID;
	ZAI[zombieid][zState] = ZAI_STATE_DEAD;
	return 1;
}

stock ZAI_IsValid(zombieid)
{
	return (zombieid >= 0 && zombieid < ZAI_MAX_ZOMBIES && ZAI[zombieid][zUsed]);
}

// FCNPC npcid of a zombie, and the reverse lookup (ZAI_INVALID if the NPC isn't a zombie).
stock ZAI_GetNPC(zombieid) { return ZAI_IsValid(zombieid) ? ZAI[zombieid][zNPC] : INVALID_PLAYER_ID; }
stock ZAI_GetZombieFromNPC(npcid)
{
	for(new i = 0; i < ZAI_MAX_ZOMBIES; i++) if(ZAI[i][zUsed] && ZAI[i][zNPC] == npcid) return i;
	return ZAI_INVALID;
}

stock ZAI_GetState(zombieid)  { return ZAI_IsValid(zombieid) ? ZAI[zombieid][zState] : ZAI_STATE_DEAD; }
stock ZAI_GetTarget(zombieid) { return ZAI_IsValid(zombieid) ? ZAI[zombieid][zTarget] : INVALID_PLAYER_ID; }
stock ZAI_GetType(zombieid)   { return ZAI_IsValid(zombieid) ? ZAI[zombieid][zType] : ZAI_INVALID; }

stock ZAI_SetDamage(zombieid, Float:damage) { if(!ZAI_IsValid(zombieid)) return 0; ZAI[zombieid][zDamage] = damage; return 1; }
stock Float:ZAI_GetDamage(zombieid) { return ZAI_IsValid(zombieid) ? ZAI[zombieid][zDamage] : 0.0; }

// The point the zombie wanders around when nobody is near (its spawn by default).
stock ZAI_SetAnchor(zombieid, Float:x, Float:y)
{
	if(!ZAI_IsValid(zombieid)) return 0;
	ZAI[zombieid][zAnchorX] = x;
	ZAI[zombieid][zAnchorY] = y;
	return 1;
}

// Force a zombie onto a player (scripted aggro). It still needs to reach them normally.
stock ZAI_SetTarget(zombieid, playerid)
{
	if(!ZAI_IsValid(zombieid) || ZAI[zombieid][zState] == ZAI_STATE_DEAD || !IsPlayerConnected(playerid)) return 0;
	ZAI[zombieid][zState] = ZAI_STATE_CHASE;
	ZAI[zombieid][zTarget] = playerid;
	ZAI[zombieid][zLastSeen] = GetTickCount();
	return 1;
}

// Freeze a zombie in place for ms (no moving or attacking).
stock ZAI_Stun(zombieid, ms)
{
	if(!ZAI_IsValid(zombieid)) return 0;
	ZAI[zombieid][zStunUntil] = GetTickCount() + ms;
	FCNPC_Stop(ZAI[zombieid][zNPC]);
	return 1;
}

// Make a noise: every idle zombie within radius walks over to investigate. Use it for anything
// loud (explosions, car alarms, mining, chopping, a dropped pan...). Gunshots do this by themselves.
stock ZAI_AlertNoise(Float:x, Float:y, Float:z, Float:radius, world = 0, interior = 0)
{
	new until = GetTickCount() + ZAI_INVESTIGATE_TIME;
	for(new i = 0; i < ZAI_MAX_ZOMBIES; i++)
	{
		if(!ZAI[i][zUsed] || ZAI[i][zState] == ZAI_STATE_DEAD) continue;
		if(ZAI[i][zWorld] != world || ZAI[i][zInterior] != interior) continue;
		new Float:zx, Float:zy, Float:zz;
		FCNPC_GetPosition(ZAI[i][zNPC], zx, zy, zz);
		if(zai_Dist3D(zx, zy, zz, x, y, z) > radius) continue;
		ZAI[i][zNoiseX] = x;
		ZAI[i][zNoiseY] = y;
		ZAI[i][zNoiseZ] = z;
		ZAI[i][zInvestigateUntil] = until; // newest noise wins, so a running firefight keeps pulling them
	}
	return 1;
}

// How far each weapon's gunshot carries. 0.0 = silent (melee). Used by the automatic gunshot noise.
stock Float:ZAI_GetWeaponNoise(weaponid)
{
	switch(weaponid)
	{
		case 22: return 45.0;          // Colt 45
		case 23: return 15.0;          // Silenced pistol
		case 24: return 70.0;          // Desert Eagle
		case 25: return 75.0;          // Shotgun
		case 26: return 65.0;          // Sawn-off
		case 27: return 80.0;          // Combat shotgun
		case 28, 29, 32: return 60.0;  // Uzi / MP5 / Tec-9
		case 30, 31: return 95.0;      // AK-47 / M4
		case 33: return 100.0;         // Country rifle
		case 34: return 110.0;         // Sniper rifle
		case 38: return 120.0;         // Minigun
	}
	return 0.0;
}

// How many zombies are chasing/attacking this player.
stock ZAI_CountTargeting(playerid)
{
	new count = 0;
	for(new i = 0; i < ZAI_MAX_ZOMBIES; i++)
		if(ZAI[i][zUsed] && (ZAI[i][zState] == ZAI_STATE_CHASE || ZAI[i][zState] == ZAI_STATE_ATTACK) && ZAI[i][zTarget] == playerid) count++;
	return count;
}

// Fully stops a zombie: movement, attack AND its looped walk animation (FCNPC_Stop alone leaves the
// loop playing, which looks like marching in place).
stock ZAI_Halt(zombieid)
{
	if(!ZAI_IsValid(zombieid)) return 0;
	FCNPC_Stop(ZAI[zombieid][zNPC]);
	FCNPC_StopAttack(ZAI[zombieid][zNPC]);
	FCNPC_ClearAnimations(ZAI[zombieid][zNPC]);
	return 1;
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static stock zai_Init()
{
	if(zai_Initialised) return 1;
	zai_Initialised = true;
	for(new i = 0; i < ZAI_MAX_ZOMBIES; i++)
	{
		ZAI[i][zUsed] = false;
		ZAI[i][zNPC] = INVALID_PLAYER_ID;
		ZAI[i][zState] = ZAI_STATE_DEAD;
		ZAI[i][zTarget] = INVALID_PLAYER_ID;
	}
	zai_HasCanTarget  = (funcidx("ZAI_OnZombieCanTarget") != -1);
	zai_HasAttack     = (funcidx("ZAI_OnZombieAttack") != -1);
	zai_HasTakeDamage = (funcidx("ZAI_OnZombieTakeDamage") != -1);
	zai_HasDeath      = (funcidx("ZAI_OnZombieDeath") != -1);
	if(zai_Timer == 0) zai_Timer = SetTimer("zai_Update", ZAI_UPDATE_INTERVAL, true);
	return 1;
}

static stock zai_Reset(zombieid)
{
	ZAI[zombieid][zState] = ZAI_STATE_IDLE;
	ZAI[zombieid][zTarget] = INVALID_PLAYER_ID;
	ZAI_Halt(zombieid);
}

// One ColAndreas ray chest-to-chest. (Sampling ground heights along the path instead made any
// overhang - bridge, roof, canopy - read as a wall and froze zombies mid-chase.)
static stock bool:zai_HasLOS(npcid, Float:tx, Float:ty, Float:tz)
{
	new Float:nx, Float:ny, Float:nz, Float:hx, Float:hy, Float:hz;
	FCNPC_GetPosition(npcid, nx, ny, nz);
	return CA_RayCastLine(nx, ny, nz + 0.5, tx, ty, tz + 0.5, hx, hy, hz) == 0;
}

// Ground Z near a reference height. CA_FindZ_For2DCoord casts from the sky, so under a bridge or
// overhang it returns the ROOF - which is what sends NPCs to rooftops. Cast a short ray from just
// above the reference instead, and only accept the sky-cast as a walkable rise, never a roof.
static stock bool:zai_GroundNear(Float:x, Float:y, Float:refZ, &Float:groundZ)
{
	new Float:hx, Float:hy;
	if(CA_RayCastLine(x, y, refZ + 2.0, x, y, refZ - 50.0, hx, hy, groundZ)) return true;
	groundZ = refZ;
	if(!CA_FindZ_For2DCoord(x, y, groundZ)) return false;
	if(groundZ - refZ > 3.0) return false;
	return true;
}

// Is the straight route from the NPC to (destX, destY) blocked by a wall or an unwalkable rise?
// FCNPC's ColAndreas move mode maps Z but does NOT collide sideways, so a path through a wall is
// walked straight through it and the NPC ends up snapped onto the roof. standoff = also demand
// 1.5m of space beyond the destination (idle movement), so zombies stop short of walls.
static stock zai_PathBlocked(npcid, Float:destX, Float:destY, bool:standoff = false)
{
	new Float:nx, Float:ny, Float:nz;
	FCNPC_GetPosition(npcid, nx, ny, nz);
	new Float:dx = destX - nx, Float:dy = destY - ny;
	new Float:len = floatsqroot(dx * dx + dy * dy);
	if(len < 0.01) return 0;

	new Float:endX = destX, Float:endY = destY;
	if(standoff)
	{
		endX += (dx / len) * 1.5;
		endY += (dy / len) * 1.5;
	}

	new Float:wallZ, Float:hx, Float:hy, Float:hz;
	if(!zai_GroundNear(destX, destY, nz, wallZ)) return 1;
	// chest-height ray for walls, knee-height ray for low walls / fences / containers
	if(CA_RayCastLine(nx, ny, nz + 1.0, endX, endY, wallZ + 1.0, hx, hy, hz)) return 1;
	if(CA_RayCastLine(nx, ny, nz - 0.3, endX, endY, wallZ + 0.7, hx, hy, hz)) return 1;

	// surface 1.5m ahead suddenly much higher = wall
	new Float:probeZ;
	if(zai_GroundNear(nx + (dx / len) * 1.5, ny + (dy / len) * 1.5, nz, probeZ) && (probeZ - nz) > 1.5) return 1;

	// destination too high, or steeper than stairs/ramps (rise bigger than the run to reach it)
	new Float:destGround;
	if(zai_GroundNear(destX, destY, nz, destGround))
	{
		new Float:rise = destGround - nz;
		if(rise > 4.0) return 1;
		if(rise > 1.0 && len < rise) return 1;
	}
	return 0;
}

static stock zai_ApplyMoveAnim(zombieid, &Float:speed, bool:chasing)
{
	new npc = ZAI[zombieid][zNPC];
	switch(ZAI[zombieid][zType])
	{
		case ZAI_TYPE_RUNNER:
		{
			speed = chasing ? FCNPC_MOVE_SPEED_RUN : FCNPC_MOVE_SPEED_WALK * 0.5;
			if(chasing) FCNPC_ApplyAnimation(npc, "PED", "JOG_maleA", 4.1, 1, 1, 1, 1, 0);
			else FCNPC_ApplyAnimation(npc, "PED", "WALK_old", 4.1, 1, 1, 1, 1, 0);
		}
		case ZAI_TYPE_CRAWLER:
		{
			speed = FCNPC_MOVE_SPEED_WALK * (chasing ? 0.7 : 0.4);
			FCNPC_ApplyAnimation(npc, "PED", "WALK_old", 4.1, 1, 1, 1, 1, 0);
		}
		case ZAI_TYPE_BRUTE:
		{
			speed = FCNPC_MOVE_SPEED_WALK * (chasing ? 0.8 : 0.5);
			FCNPC_ApplyAnimation(npc, "PED", "WALK_old", 4.1, 1, 1, 1, 1, 0);
		}
		default:
		{
			speed = FCNPC_MOVE_SPEED_WALK * (chasing ? 1.0 : 0.5);
			FCNPC_ApplyAnimation(npc, "PED", "WALK_old", 4.1, 1, 1, 1, 1, 0);
		}
	}
	return 1;
}

static stock zai_CanTarget(zombieid, playerid)
{
	if(!IsPlayerConnected(playerid) || IsPlayerNPC(playerid)) return 0;
	new st = GetPlayerState(playerid);
	if(st == PLAYER_STATE_WASTED || st == PLAYER_STATE_SPECTATING || st == PLAYER_STATE_NONE) return 0;
	if(GetPlayerVirtualWorld(playerid) != ZAI[zombieid][zWorld] || GetPlayerInterior(playerid) != ZAI[zombieid][zInterior]) return 0;
	new Float:hp;
	GetPlayerHealth(playerid, hp);
	if(hp < 1.0) return 0;
	if(zai_HasCanTarget && !CallLocalFunction("ZAI_OnZombieCanTarget", "dd", zombieid, playerid)) return 0;
	return 1;
}

// Closest valid player, with horde spreading: each OTHER zombie already on a player adds
// ZAI_HORDE_SPREAD metres, so a group spreads over several survivors instead of one.
static stock zai_ClosestPlayer(zombieid, Float:x, Float:y, Float:z)
{
	new best = INVALID_PLAYER_ID, Float:bestScore = 999999.0;
	for(new p = 0, pmax = GetPlayerPoolSize(); p <= pmax; p++)
	{
		if(!zai_CanTarget(zombieid, p)) continue;
		new Float:px, Float:py, Float:pz;
		GetPlayerPos(p, px, py, pz);
		new Float:d = zai_Dist3D(x, y, z, px, py, pz);
		if(d > ZAI_CHASE_DISTANCE + 1.0) continue;
		new others = ZAI_CountTargeting(p);
		if(ZAI[zombieid][zTarget] == p && (ZAI[zombieid][zState] == ZAI_STATE_CHASE || ZAI[zombieid][zState] == ZAI_STATE_ATTACK)) others--; // don't count itself
		new Float:score = d + float(others) * ZAI_HORDE_SPREAD;
		if(score < bestScore) { bestScore = score; best = p; }
	}
	return best;
}

static stock zai_Chase(zombieid, playerid)
{
	if(!IsPlayerConnected(playerid)) return 0;
	new npc = ZAI[zombieid][zNPC];
	new Float:px, Float:py, Float:pz;
	GetPlayerPos(playerid, px, py, pz);

	// Lead the target: aim ~0.8s ahead along their velocity (capped at 5m), so zombies cut the
	// corner instead of trailing the spot the player just left.
	new Float:vx, Float:vy, Float:vz;
	GetPlayerVelocity(playerid, vx, vy, vz);
	new Float:leadX = vx * 40.0, Float:leadY = vy * 40.0;
	new Float:leadLen = floatsqroot(leadX * leadX + leadY * leadY);
	if(leadLen > 5.0) { leadX = (leadX / leadLen) * 5.0; leadY = (leadY / leadLen) * 5.0; }
	px += leadX;
	py += leadY;

	// Anti-stutter: already heading roughly there? Leave the path alone. Re-issuing GoTo + the walk
	// animation every tick is what makes FCNPC movement jitter.
	if(FCNPC_IsMoving(npc))
	{
		new Float:ddx = px - ZAI[zombieid][zPathX], Float:ddy = py - ZAI[zombieid][zPathY];
		if(floatsqroot(ddx * ddx + ddy * ddy) < ZAI_CHASE_REPATH_DIST) return 1;
	}

	// Blocked? Try the player's real spot before giving up (the lead can point into a wall).
	if(zai_PathBlocked(npc, px, py)) { px -= leadX; py -= leadY; }
	if(zai_PathBlocked(npc, px, py))
	{
		zai_Reset(zombieid);
		return 0;
	}

	// Destination ground from the ZOMBIE's height, not the player's - otherwise a player on a roof
	// hands FCNPC a rooftop Z and the zombie walks up the wall.
	new Float:nx, Float:ny, Float:nz, Float:gz;
	FCNPC_GetPosition(npc, nx, ny, nz);
	zai_GroundNear(px, py, nz, gz);

	new Float:speed;
	zai_ApplyMoveAnim(zombieid, speed, true);
	FCNPC_GoTo(npc, px, py, gz, FCNPC_MOVE_TYPE_AUTO, speed, FCNPC_MOVE_MODE_COLANDREAS);
	ZAI[zombieid][zPathX] = px;
	ZAI[zombieid][zPathY] = py;
	return 1;
}

static stock zai_Attack(zombieid, playerid)
{
	new npc = ZAI[zombieid][zNPC];
	new now = GetTickCount();
	if(now < ZAI[zombieid][zStunUntil]) return 0;
	new st = GetPlayerState(playerid);

	if(st == PLAYER_STATE_DRIVER || st == PLAYER_STATE_PASSENGER)
	{
		// In a car: claw at the vehicle instead
		if(now - ZAI[zombieid][zLastAttack] < ZAI_VEHICLE_COOLDOWN) return 0;
		ZAI[zombieid][zLastAttack] = now;
		new veh = GetPlayerVehicleID(playerid);
		if(veh != INVALID_VEHICLE_ID)
		{
			new Float:vh;
			GetVehicleHealth(veh, vh);
			if(vh > 0.0) SetVehicleHealth(veh, (vh - ZAI_VEHICLE_DAMAGE < 0.0) ? 0.0 : vh - ZAI_VEHICLE_DAMAGE);
			FCNPC_ApplyAnimation(npc, "FIGHT_B", "FightB_1", 4.1, 0, 1, 1, 0, 0);
		}
		return 1;
	}

	if(now - ZAI[zombieid][zLastAttack] < ZAI_ATTACK_COOLDOWN) return 0;
	ZAI[zombieid][zLastAttack] = now;
	FCNPC_ApplyAnimation(npc, "FIGHT_B", "FightB_1", 4.1, 0, 1, 1, 0, 0);

	new apply = 1;
	if(zai_HasAttack) apply = CallLocalFunction("ZAI_OnZombieAttack", "ddf", zombieid, playerid, ZAI[zombieid][zDamage]);
	if(apply)
	{
		new Float:hp;
		GetPlayerHealth(playerid, hp);
		hp -= ZAI[zombieid][zDamage];
		SetPlayerHealth(playerid, (hp < 0.0) ? 0.0 : hp);
	}
	return 1;
}

static stock zai_Wander(zombieid)
{
	new npc = ZAI[zombieid][zNPC];
	if(FCNPC_IsMoving(npc)) return 1;

	new Float:zx, Float:zy, Float:zz;
	FCNPC_GetPosition(npc, zx, zy, zz);

	// Anchor = spawn, but if a player is still nearby (e.g. up on a roof it can't reach) wander
	// around THEM instead - reads as the zombie patrolling below.
	new Float:ax = ZAI[zombieid][zAnchorX], Float:ay = ZAI[zombieid][zAnchorY];
	new near = zai_ClosestPlayer(zombieid, zx, zy, zz);
	if(near != INVALID_PLAYER_ID)
	{
		new Float:npx, Float:npy, Float:npz;
		GetPlayerPos(near, npx, npy, npz);
		if(zai_Dist3D(zx, zy, zz, npx, npy, npz) <= ZAI_IDLE_DISTANCE) { ax = npx; ay = npy; }
	}

	// Prefer gentle turns so wandering flows in arcs instead of snapping 180 degrees.
	new Float:heading = FCNPC_GetAngle(npc) + 90.0; // SA-MP facing -> math angle
	new Float:speed;
	for(new attempt = 1; attempt <= 5; attempt++)
	{
		new Float:ang = float(random(360));
		new Float:dist = float(random(floatround(ZAI_WANDER_RADIUS))) + 2.0;
		new Float:tx = ax + dist * floatcos(ang, degrees);
		new Float:ty = ay + dist * floatsin(ang, degrees);
		new Float:cdx = tx - zx, Float:cdy = ty - zy;
		if(cdx * cdx + cdy * cdy < 4.0) continue; // micro-leg at its own feet looks twitchy

		new Float:travel = atan2(ty - zy, tx - zx);
		new Float:turn = travel - heading;
		while(turn > 180.0) turn -= 360.0;
		while(turn < -180.0) turn += 360.0;
		if(floatabs(turn) > 60.0)
		{
			if(attempt < 4) continue; // re-roll for a gentler direction
			// Everything gentle is blocked: walk the turn as a short angled leg (legs chain with no
			// pause, so a few of these trace a curve) before pivoting in place as a last resort.
			for(new side = 0; side < 2; side++)
			{
				new Float:leg = heading + (((side == 0) == (turn > 0.0)) ? 60.0 : -60.0);
				new Float:ix = zx + 4.0 * floatcos(leg, degrees), Float:iy = zy + 4.0 * floatsin(leg, degrees), Float:iz;
				if(zai_GroundNear(ix, iy, zz, iz) && floatabs(iz - zz) <= 4.0 && !zai_PathBlocked(npc, ix, iy, true))
				{
					zai_ApplyMoveAnim(zombieid, speed, false);
					FCNPC_GoTo(npc, ix, iy, iz, FCNPC_MOVE_TYPE_WALK, speed, FCNPC_MOVE_MODE_COLANDREAS);
					return 1;
				}
			}
			zai_TurnStep(zombieid, travel - 90.0);
			return 1;
		}

		new Float:tz;
		if(!zai_GroundNear(tx, ty, zz, tz)) continue;
		if(floatabs(tz - zz) > 4.0) continue;
		if(!zai_PathBlocked(npc, tx, ty, true))
		{
			zai_ApplyMoveAnim(zombieid, speed, false);
			FCNPC_GoTo(npc, tx, ty, tz, FCNPC_MOVE_TYPE_WALK, speed, FCNPC_MOVE_MODE_COLANDREAS);
			return 1;
		}
	}
	return 1;
}

// Idle + a noise is active: walk toward it. On arrival, or if it can't be reached, give up and
// carry on wandering there ("came to look, found nothing").
static stock zai_Investigate(zombieid)
{
	new npc = ZAI[zombieid][zNPC];
	new Float:nx = ZAI[zombieid][zNoiseX], Float:ny = ZAI[zombieid][zNoiseY], Float:nzz = ZAI[zombieid][zNoiseZ];
	if(FCNPC_IsMoving(npc))
	{
		if(zai_PathBlocked(npc, nx, ny, true)) ZAI[zombieid][zInvestigateUntil] = 0;
		return 1;
	}
	new Float:zx, Float:zy, Float:zz;
	FCNPC_GetPosition(npc, zx, zy, zz);
	if(zai_Dist3D(zx, zy, zz, nx, ny, nzz) < 4.0) { ZAI[zombieid][zInvestigateUntil] = 0; return 1; }
	if(zai_PathBlocked(npc, nx, ny, true))
	{
		ZAI[zombieid][zInvestigateUntil] = 0;
		zai_Wander(zombieid);
		return 1;
	}
	new Float:gz, Float:speed;
	zai_GroundNear(nx, ny, zz, gz); // street under a rooftop shooter, never the roof
	zai_ApplyMoveAnim(zombieid, speed, false);
	FCNPC_GoTo(npc, nx, ny, gz, FCNPC_MOVE_TYPE_WALK, speed * 2.0, FCNPC_MOVE_MODE_COLANDREAS);
	return 1;
}

static stock zai_Idle(zombieid)
{
	if(GetTickCount() < ZAI[zombieid][zInvestigateUntil]) zai_Investigate(zombieid);
	else zai_Wander(zombieid);
}

// Rotates a standing zombie in 50-degree steps every 150ms instead of one instant snap.
forward zai_TurnStep(zombieid, Float:target);
public zai_TurnStep(zombieid, Float:target)
{
	if(!ZAI_IsValid(zombieid) || ZAI[zombieid][zState] != ZAI_STATE_IDLE) return 0;
	new npc = ZAI[zombieid][zNPC];
	if(!FCNPC_IsValid(npc) || FCNPC_IsMoving(npc)) return 0;
	new Float:cur = FCNPC_GetAngle(npc);
	new Float:diff = target - cur;
	while(diff > 180.0) diff -= 360.0;
	while(diff < -180.0) diff += 360.0;
	if(floatabs(diff) <= 50.0) { FCNPC_SetAngle(npc, target); return 1; }
	FCNPC_SetAngle(npc, cur + (diff > 0.0 ? 50.0 : -50.0));
	SetTimerEx("zai_TurnStep", 150, false, "df", zombieid, target);
	return 1;
}

forward zai_GetUp(zombieid, targetid);
public zai_GetUp(zombieid, targetid)
{
	if(!ZAI_IsValid(zombieid) || ZAI[zombieid][zState] == ZAI_STATE_DEAD) return 1;
	if(!FCNPC_IsValid(ZAI[zombieid][zNPC])) return 1;
	FCNPC_ApplyAnimation(ZAI[zombieid][zNPC], "PED", "GETUP", 4.1, 0, 1, 1, 0, 0);
	if(IsPlayerConnected(targetid)) { ZAI[zombieid][zState] = ZAI_STATE_CHASE; ZAI[zombieid][zTarget] = targetid; }
	else { ZAI[zombieid][zState] = ZAI_STATE_IDLE; ZAI[zombieid][zTarget] = INVALID_PLAYER_ID; }
	return 1;
}

forward zai_RemoveCorpse(zombieid, npcid);
public zai_RemoveCorpse(zombieid, npcid)
{
	// Only if the slot still belongs to that same dead NPC (not re-used in the meantime)
	if(zombieid < 0 || zombieid >= ZAI_MAX_ZOMBIES || !ZAI[zombieid][zUsed] || ZAI[zombieid][zNPC] != npcid) return 0;
	ZAI_Destroy(zombieid);
	return 1;
}

static stock zai_Kill(zombieid, killerid, weaponid)
{
	if(ZAI[zombieid][zState] == ZAI_STATE_DEAD) return 0;
	ZAI[zombieid][zState] = ZAI_STATE_DEAD;
	ZAI[zombieid][zTarget] = INVALID_PLAYER_ID;
	new npc = ZAI[zombieid][zNPC];
	FCNPC_Stop(npc);
	FCNPC_StopAttack(npc);
	FCNPC_ApplyAnimation(npc, "PED", "KO_shot_stom", 4.1, 0, 1, 1, 1, 0);
	if(zai_HasDeath) CallLocalFunction("ZAI_OnZombieDeath", "ddd", zombieid, killerid, weaponid);
	SetTimerEx("zai_RemoveCorpse", ZAI_CORPSE_TIME, false, "dd", zombieid, npc);
	return 1;
}

// ============================================================================
// AI TICK
// ============================================================================
forward zai_Update();
public zai_Update()
{
	new now = GetTickCount();
	for(new i = 0; i < ZAI_MAX_ZOMBIES; i++)
	{
		if(!ZAI[i][zUsed] || ZAI[i][zState] == ZAI_STATE_DEAD) continue;
		new npc = ZAI[i][zNPC];
		if(!FCNPC_IsValid(npc)) continue;

		new Float:zx, Float:zy, Float:zz;
		FCNPC_GetPosition(npc, zx, zy, zz);

		// Anti-teleport watchdog: FCNPC can still snap a zombie on top of a building when it brushes
		// a footprint. A big rise in one tick whose straight line crosses geometry (a wall pop, not
		// stairs or a hill) gets put straight back where it was.
		new Float:maxRise = (ZAI[i][zState] == ZAI_STATE_IDLE) ? 1.2 : 2.5;
		if(ZAI[i][zPrevZ] != 0.0 && zz - ZAI[i][zPrevZ] > maxRise)
		{
			new Float:hx, Float:hy, Float:hz;
			if(CA_RayCastLine(ZAI[i][zPrevX], ZAI[i][zPrevY], ZAI[i][zPrevZ] + 0.5, zx, zy, zz + 0.5, hx, hy, hz))
			{
				zai_Reset(i);
				new Float:backZ;
				if(!zai_GroundNear(ZAI[i][zPrevX], ZAI[i][zPrevY], ZAI[i][zPrevZ], backZ)) backZ = ZAI[i][zPrevZ] - 1.0;
				FCNPC_SetPosition(npc, ZAI[i][zPrevX], ZAI[i][zPrevY], backZ + 1.0);
				zx = ZAI[i][zPrevX];
				zy = ZAI[i][zPrevY];
				zz = backZ + 1.0;
			}
		}
		ZAI[i][zPrevX] = zx;
		ZAI[i][zPrevY] = zy;
		ZAI[i][zPrevZ] = zz;

		if(now < ZAI[i][zStunUntil]) continue; // staggered - don't cancel the flinch by re-pathing

		new target = zai_ClosestPlayer(i, zx, zy, zz);
		if(target == INVALID_PLAYER_ID)
		{
			if(ZAI[i][zState] != ZAI_STATE_IDLE) zai_Reset(i);
			zai_Idle(i);
			continue;
		}

		new Float:px, Float:py, Float:pz;
		GetPlayerPos(target, px, py, pz);
		new Float:dist = zai_Dist3D(zx, zy, zz, px, py, pz);

		// Unreachable (too high, or up on a roof/container): patrol underneath instead of grinding
		// into the wall. Chase resumes by itself the moment they come down.
		new bool:unreachable = (pz - zz > ZAI_MAX_Z_DIFF);
		if(!unreachable && pz - zz > 1.0)
		{
			new Float:walkZ;
			if(!zai_GroundNear(px, py, zz, walkZ) || pz - walkZ > ZAI_UNREACHABLE_Z_DIFF) unreachable = true;
		}
		if(unreachable)
		{
			if(ZAI[i][zState] != ZAI_STATE_IDLE) zai_Reset(i);
			zai_Wander(i);
			continue;
		}

		if(ZAI[i][zState] == ZAI_STATE_CHASE && dist <= ZAI_ATTACK_DISTANCE)
		{
			if(zai_HasLOS(npc, px, py, pz))
			{
				ZAI[i][zState] = ZAI_STATE_ATTACK;
				ZAI[i][zTarget] = target;
				FCNPC_Stop(npc);
				FCNPC_ClearAnimations(npc); // drop the looped walk so it doesn't march in place mid-attack
				zai_Attack(i, target);
			}
			else zai_Reset(i); // player is on the other side of a thin wall
		}
		else if(ZAI[i][zState] == ZAI_STATE_ATTACK && dist <= ZAI_ATTACK_BREAK_DISTANCE && ZAI[i][zTarget] == target)
		{
			zai_Attack(i, target); // hysteresis: only a real step back breaks off the attack
		}
		else if(dist <= ZAI_CHASE_DISTANCE)
		{
			if(zai_HasLOS(npc, px, py, pz))
			{
				if(ZAI[i][zState] == ZAI_STATE_ATTACK) FCNPC_StopAttack(npc);
				ZAI[i][zState] = ZAI_STATE_CHASE;
				ZAI[i][zTarget] = target;
				ZAI[i][zLastSeen] = now;
				zai_Chase(i, target);
			}
			else if(ZAI[i][zState] == ZAI_STATE_CHASE && now - ZAI[i][zLastSeen] < ZAI_LOS_GRACE_MS)
			{
				// Lost sight a moment ago (corner, prop): keep heading for the last known spot
				if(!FCNPC_IsMoving(npc)) zai_Chase(i, ZAI[i][zTarget]);
			}
			else
			{
				if(ZAI[i][zState] != ZAI_STATE_IDLE) zai_Reset(i);
			}
		}
		else if(dist > ZAI_IDLE_DISTANCE && ZAI[i][zState] != ZAI_STATE_IDLE)
		{
			zai_Reset(i);
		}

		if(ZAI[i][zState] == ZAI_STATE_IDLE) zai_Idle(i);
	}
	return 1;
}

// ============================================================================
// CALLBACK HOOKS (ALS - your own versions of these callbacks keep working)
// ============================================================================
public OnGameModeInit()
{
	zai_Init();
	#if defined zai_OnGameModeInit
		return zai_OnGameModeInit();
	#else
		return 1;
	#endif
}
#if defined _ALS_OnGameModeInit
	#undef OnGameModeInit
#else
	#define _ALS_OnGameModeInit
#endif
#define OnGameModeInit zai_OnGameModeInit
#if defined zai_OnGameModeInit
	forward zai_OnGameModeInit();
#endif

public OnFilterScriptInit()
{
	zai_Init();
	#if defined zai_OnFilterScriptInit
		return zai_OnFilterScriptInit();
	#else
		return 1;
	#endif
}
#if defined _ALS_OnFilterScriptInit
	#undef OnFilterScriptInit
#else
	#define _ALS_OnFilterScriptInit
#endif
#define OnFilterScriptInit zai_OnFilterScriptInit
#if defined zai_OnFilterScriptInit
	forward zai_OnFilterScriptInit();
#endif

public OnPlayerDisconnect(playerid, reason)
{
	// Nobody should keep chasing a player id that is about to be reused
	for(new i = 0; i < ZAI_MAX_ZOMBIES; i++)
	{
		if(!ZAI[i][zUsed] || ZAI[i][zTarget] != playerid) continue;
		if(ZAI[i][zState] != ZAI_STATE_DEAD) zai_Reset(i);
	}
	zai_LastNoise[playerid] = 0;
	#if defined zai_OnPlayerDisconnect
		return zai_OnPlayerDisconnect(playerid, reason);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerDisconnect
	#undef OnPlayerDisconnect
#else
	#define _ALS_OnPlayerDisconnect
#endif
#define OnPlayerDisconnect zai_OnPlayerDisconnect
#if defined zai_OnPlayerDisconnect
	forward zai_OnPlayerDisconnect(playerid, reason);
#endif

public OnPlayerWeaponShot(playerid, weaponid, hittype, hitid, Float:fX, Float:fY, Float:fZ)
{
	#if ZAI_GUNSHOT_NOISE
		new Float:radius = ZAI_GetWeaponNoise(weaponid);
		if(radius > 0.0)
		{
			new tick = GetTickCount();
			if(tick - zai_LastNoise[playerid] >= ZAI_NOISE_THROTTLE_MS)
			{
				zai_LastNoise[playerid] = tick;
				new Float:sx, Float:sy, Float:sz;
				GetPlayerPos(playerid, sx, sy, sz);
				ZAI_AlertNoise(sx, sy, sz, radius, GetPlayerVirtualWorld(playerid), GetPlayerInterior(playerid));
			}
		}
	#endif
	#if defined zai_OnPlayerWeaponShot
		return zai_OnPlayerWeaponShot(playerid, weaponid, hittype, hitid, fX, fY, fZ);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerWeaponShot
	#undef OnPlayerWeaponShot
#else
	#define _ALS_OnPlayerWeaponShot
#endif
#define OnPlayerWeaponShot zai_OnPlayerWeaponShot
#if defined zai_OnPlayerWeaponShot
	forward zai_OnPlayerWeaponShot(playerid, weaponid, hittype, hitid, Float:fX, Float:fY, Float:fZ);
#endif

public FCNPC_OnReachDestination(npcid)
{
	new z = ZAI_GetZombieFromNPC(npcid);
	if(z != ZAI_INVALID)
	{
		if(ZAI[z][zState] == ZAI_STATE_DEAD) return 1;
		// Chain the next leg right now instead of standing still until the next AI tick
		if(ZAI[z][zState] == ZAI_STATE_IDLE) zai_Idle(z);
		else if(ZAI[z][zState] == ZAI_STATE_CHASE && IsPlayerConnected(ZAI[z][zTarget])) zai_Chase(z, ZAI[z][zTarget]);
		// Walk anims are looped: if nothing set it moving again, stop the walk-in-place
		if(!FCNPC_IsMoving(npcid)) FCNPC_ClearAnimations(npcid);
		return 1;
	}
	#if defined zai_FCNPC_OnReachDestination
		return zai_FCNPC_OnReachDestination(npcid);
	#else
		return 1;
	#endif
}
#if defined _ALS_FCNPC_OnReachDestination
	#undef FCNPC_OnReachDestination
#else
	#define _ALS_FCNPC_OnReachDestination
#endif
#define FCNPC_OnReachDestination zai_FCNPC_OnReachDestination
#if defined zai_FCNPC_OnReachDestination
	forward zai_FCNPC_OnReachDestination(npcid);
#endif

public FCNPC_OnTakeDamage(npcid, issuerid, Float:amount, weaponid, bodypart)
{
	new z = ZAI_GetZombieFromNPC(npcid);
	if(z != ZAI_INVALID)
	{
		if(ZAI[z][zState] == ZAI_STATE_DEAD) return 0;

		new apply = 1;
		if(zai_HasTakeDamage) apply = CallLocalFunction("ZAI_OnZombieTakeDamage", "ddfdd", z, issuerid, amount, weaponid, bodypart);
		if(apply)
		{
			new Float:hp = FCNPC_GetHealth(npcid) - amount;
			if(hp <= 0.0) { zai_Kill(z, issuerid, weaponid); return 0; }
			FCNPC_SetHealth(npcid, hp);
		}

		// Being hit by a player makes an idle zombie turn on them
		if(IsPlayerConnected(issuerid) && ZAI[z][zState] == ZAI_STATE_IDLE) ZAI_SetTarget(z, issuerid);

		new now = GetTickCount();
		if(weaponid >= 1 && weaponid <= 15) // melee: stun, sometimes a knockdown
		{
			ZAI[z][zStunUntil] = now + ZAI_MELEE_STUN_MS;
			if(ZAI[z][zLastAttack] < now - 1000) ZAI[z][zLastAttack] = now - 1000; // no instant counter-hit
			FCNPC_Stop(npcid);
			if(random(100) < ZAI_KNOCKDOWN_CHANCE)
			{
				ZAI[z][zStunUntil] = now + 2800;
				FCNPC_ApplyAnimation(npcid, "PED", "FALL_BACK", 4.1, 0, 1, 1, 0, 0);
				SetTimerEx("zai_GetUp", 2500, false, "dd", z, ZAI[z][zTarget]);
			}
		}
		else if(weaponid >= 22 && weaponid <= 38) // gunfire: a short stagger so sustained fire slows them
		{
			ZAI[z][zStunUntil] = now + ZAI_GUN_STAGGER_MS;
			FCNPC_Stop(npcid);
		}
		return 0; // health handled here - don't let FCNPC apply it a second time
	}
	#if defined zai_FCNPC_OnTakeDamage
		return zai_FCNPC_OnTakeDamage(npcid, issuerid, amount, weaponid, bodypart);
	#else
		return 1;
	#endif
}
#if defined _ALS_FCNPC_OnTakeDamage
	#undef FCNPC_OnTakeDamage
#else
	#define _ALS_FCNPC_OnTakeDamage
#endif
#define FCNPC_OnTakeDamage zai_FCNPC_OnTakeDamage
#if defined zai_FCNPC_OnTakeDamage
	forward zai_FCNPC_OnTakeDamage(npcid, issuerid, Float:amount, weaponid, bodypart);
#endif

public FCNPC_OnDeath(npcid, killerid, reason)
{
	new z = ZAI_GetZombieFromNPC(npcid);
	if(z != ZAI_INVALID)
	{
		zai_Kill(z, killerid, reason); // safety net when FCNPC kills it before OnTakeDamage does
		return 1;
	}
	#if defined zai_FCNPC_OnDeath
		return zai_FCNPC_OnDeath(npcid, killerid, reason);
	#else
		return 1;
	#endif
}
#if defined _ALS_FCNPC_OnDeath
	#undef FCNPC_OnDeath
#else
	#define _ALS_FCNPC_OnDeath
#endif
#define FCNPC_OnDeath zai_FCNPC_OnDeath
#if defined zai_FCNPC_OnDeath
	forward zai_FCNPC_OnDeath(npcid, killerid, reason);
#endif
