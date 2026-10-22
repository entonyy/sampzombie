// Example filterscript for zombie_ai.inc
// /zombie        - spawn a zombie 8m in front of you
// /zombierunner  - spawn a runner
// Zombies respawn where they died 30 seconds later.

#define FILTERSCRIPT

#include <a_samp>
#include <FCNPC>
#include <colandreas>
#include <zombie_ai>

public OnFilterScriptInit()
{
	CA_Init(); // ColAndreas collision data - required for the movement checks
	print("[zombie_ai] example loaded");
	return 1;
}

static SpawnInFront(playerid, type)
{
	new Float:x, Float:y, Float:z, Float:a;
	GetPlayerPos(playerid, x, y, z);
	GetPlayerFacingAngle(playerid, a);
	x += 8.0 * floatsin(-a, degrees);
	y += 8.0 * floatcos(-a, degrees);
	new id = ZAI_Create(x, y, z, 162, type, 100.0, 15.0, GetPlayerVirtualWorld(playerid), GetPlayerInterior(playerid));
	if(id == ZAI_INVALID) SendClientMessage(playerid, -1, "Couldn't spawn a zombie (pool full?)");
	return id;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
	if(!strcmp(cmdtext, "/zombie", true)) { SpawnInFront(playerid, ZAI_TYPE_WALKER); return 1; }
	if(!strcmp(cmdtext, "/zombierunner", true)) { SpawnInFront(playerid, ZAI_TYPE_RUNNER); return 1; }
	return 0;
}

// Zombies leave spectators/dead players alone automatically; add your own rules here.
public ZAI_OnZombieCanTarget(zombieid, playerid)
{
	return 1;
}

public ZAI_OnZombieAttack(zombieid, playerid, Float:damage)
{
	GameTextForPlayer(playerid, "~r~bitten!", 1000, 4);
	return 1; // let the include apply the damage
}

public ZAI_OnZombieDeath(zombieid, killerid, weaponid)
{
	new Float:x, Float:y, Float:z;
	FCNPC_GetPosition(ZAI_GetNPC(zombieid), x, y, z);
	SetTimerEx("RespawnZombie", 30000, false, "fffd", x, y, z, ZAI_GetType(zombieid));
	return 1;
}

forward RespawnZombie(Float:x, Float:y, Float:z, type);
public RespawnZombie(Float:x, Float:y, Float:z, type)
{
	ZAI_Create(x, y, z, 162, type);
	return 1;
}
