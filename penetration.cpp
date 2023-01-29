#include "includes.h"

float penetration::scale( Player* player, float damage, float armor_ratio, int hitgroup ) {
	bool has_heavy_armor = player->m_bHasHeavyArmor( );
	int armor = player->m_ArmorValue( );

	auto is_armored = [ ]( Player* p, int armor, int hg ) {
		return armor > 0 && ( hg == HITGROUP_HEAD && p->m_bHasHelmet( ) || ( hg >= HITGROUP_CHEST && hg <= HITGROUP_RIGHTARM ) );
	};

	switch ( hitgroup ) {
	case HITGROUP_HEAD:
		damage *= has_heavy_armor ? 2.f : 4.f;
		break;
	case HITGROUP_STOMACH:
		damage *= 1.25f;
		break;
	case HITGROUP_LEFTLEG:
	case HITGROUP_RIGHTLEG:
		damage *= 0.75f;
		break;
	}

	if ( is_armored( player, armor, hitgroup ) ) {
		float ratio = armor_ratio * ( has_heavy_armor ? 0.25f : 0.5f );
		float new_damage = damage * ratio;
		if ( ( ( damage - new_damage ) * ( has_heavy_armor ? 0.33f * 0.5f : 0.5f ) ) > armor )
			new_damage = damage - ( armor / ( has_heavy_armor ? 0.33f : 0.5f ) );
		damage = new_damage;
	}

	return std::floor( damage );
}

bool penetration::TraceToExit( const vec3_t& start, const vec3_t& dir, vec3_t& out, CGameTrace* enter_trace, CGameTrace* exit_trace )
{
	static CTraceFilterSimple_game filter{};
	float dist = 0;
	vec3_t new_end;
	int contents, first_contents = 0;
	while ( dist <= 90.f )
	{
		dist += 4.f;
		out = start + ( dir * dist );
		if ( !first_contents )
			first_contents = g_csgo.m_engine_trace->GetPointContents( out, MASK_SHOT, nullptr );
		contents = g_csgo.m_engine_trace->GetPointContents( out, MASK_SHOT, nullptr );
		if ( ( contents & MASK_SHOT_HULL ) && ( !( contents & CONTENTS_HITBOX ) || ( contents == first_contents ) ) )
			continue;
		new_end = out - ( dir * 4.f );
		g_csgo.m_engine_trace->TraceRay( Ray( out, new_end ), MASK_SHOT, nullptr, exit_trace );
		if ( g_csgo.sv_clip_penetration_traces_to_players->GetInt( ) )
			game::UTIL_ClipTraceToPlayers( out, new_end, MASK_SHOT, nullptr, exit_trace, -60.f );
		if ( exit_trace->m_startsolid && ( exit_trace->m_surface.m_flags & SURF_HITBOX ) )
		{
			filter.SetPassEntity( exit_trace->m_entity );
			g_csgo.m_engine_trace->TraceRay( Ray( out, start ), MASK_SHOT_HULL, ( ITraceFilter* )&filter, exit_trace );
			if ( exit_trace->hit( ) && !exit_trace->m_startsolid )
			{
				out = exit_trace->m_endpos;
				return true;
			}
			continue;
		}
		if ( !exit_trace->hit( ) || exit_trace->m_startsolid )
		{
			if ( game::IsBreakable( enter_trace->m_entity ) )
			{
				*exit_trace = *enter_trace;
				exit_trace->m_endpos = start + dir;
				return true;
			}
			continue;
		}
		if ( exit_trace->m_surface.m_flags & SURF_NODRAW )
		{
			if ( game::IsBreakable( exit_trace->m_entity ) && game::IsBreakable( enter_trace->m_entity ) )
			{
				out = exit_trace->m_endpos;
				return true;
			}
			if ( !( enter_trace->m_surface.m_flags & SURF_NODRAW ) )
				continue;
		}
		if ( exit_trace->m_plane.m_normal.dot( dir ) <= 1.f )
		{
			out -= ( dir * ( exit_trace->m_fraction * 4.f ) );
			return true;
		}
	}
}


void penetration::ClipTraceToPlayer( const vec3_t& start, const vec3_t& end, uint32_t mask, CGameTrace* tr, Player* player, float min ) {
	vec3_t pos = player->m_vecOrigin( ) + ( player->m_vecMins( ) + player->m_vecMaxs( ) ) * 0.5f;
	vec3_t to = pos - start;
	vec3_t dir = start - end;
	float len = dir.normalize( );
	float range_along = dir.dot( to );
	float range;

	if ( range_along < 0 )
		range = -to.length( );
	else if ( range_along > len )
		range = -( pos - end ).length( );
	else {
		vec3_t on_ray = start + ( dir * range_along );
		range = ( pos - on_ray ).length( );
	}

	if ( range <= 60 ) {
		CGameTrace new_trace;
		g_csgo.m_engine_trace->ClipRayToEntity( Ray( start, end ), mask, player, &new_trace );
		if ( tr->m_fraction > new_trace.m_fraction )
			*tr = new_trace;
	}
}

bool penetration::run( PenetrationInput_t* in, PenetrationOutput_t* out ) {
    static CTraceFilterSkipTwoEntities_game filter{};

	int			  pen{ 4 }, enter_material, exit_material;
	float		  damage, penetration, penetration_mod, player_damage, remaining, trace_len{}, total_pen_mod, damage_mod, modifier, damage_lost;
	surfacedata_t *enter_surface, *exit_surface;
	bool		  nodraw, grate;
	vec3_t		  start, dir, end, pen_end;
	CGameTrace	  trace, exit_trace;
	Weapon		  *weapon;
	WeaponInfo    *weapon_info;

	// Check if tracing is from local player's perspective.
	if ( in->m_from->m_bIsLocalPlayer( ) ) {
		weapon = g_cl.m_weapon;
		weapon_info = g_cl.m_weapon_info;
		start = g_cl.m_shoot_pos;
	}
	// If not local player
	else {
		weapon = in->m_from->GetActiveWeapon( );
		if ( !weapon )
			return false;
		weapon_info = weapon->GetWpnData( );
		if ( !weapon_info )
			return false;
		start = in->m_from->GetShootPosition( );
	}

	// Get weapon data
	damage = ( float )weapon_info->m_damage;
	penetration = weapon_info->m_penetration;

	// Calculate penetration mod
	penetration_mod = std::max( 0.f, ( 3.f / penetration ) * 1.25f );

	// Get direction to end point
	dir = ( in->m_pos - start ).normalized( );

	// Set up trace filter
	filter.SetPassEntity( in->m_from );
	filter.SetPassEntity2( nullptr );

    while( damage > 0.f ) {
		// calculating remaining len.
		remaining = weapon_info->m_range - trace_len;

		// set trace end.
		end = start + ( dir * remaining );

		// setup ray and trace.
		// TODO; use UTIL_TraceLineIgnoreTwoEntities?
		g_csgo.m_engine_trace->TraceRay( Ray( start, end ), MASK_SHOT, (ITraceFilter *)&filter, &trace );

		// check for player hitboxes extending outside their collision bounds.
		// if no target is passed we clip the trace to a specific player, otherwise we clip the trace to any player.
		if( in->m_target )
			ClipTraceToPlayer( start, end + ( dir * 40.f ), MASK_SHOT, &trace, in->m_target, -60.f );

		else
			game::UTIL_ClipTraceToPlayers( start, end + ( dir * 40.f ), MASK_SHOT, (ITraceFilter *)&filter, &trace, -60.f );

		// we didn't hit anything.
		if( trace.m_fraction == 1.f )
			return false;

		// calculate damage based on the distance the bullet traveled.
		trace_len += trace.m_fraction * remaining;
		damage    *= std::pow( weapon_info->m_range_modifier, trace_len / 500.f );

		// if a target was passed.
		if( in->m_target ) {

			// validate that we hit the target we aimed for.
			if( trace.m_entity && trace.m_entity == in->m_target && game::IsValidHitgroup( trace.m_hitgroup ) ) {
				int group = ( weapon->m_iItemDefinitionIndex( ) == ZEUS ) ? HITGROUP_GENERIC : trace.m_hitgroup;

				// scale damage based on the hitgroup we hit.
				player_damage = scale( in->m_target, damage, weapon_info->m_armor_ratio, group );

				// set result data for when we hit a player.
			    out->m_pen      = pen != 4;
			    out->m_hitgroup = group;
			    out->m_damage   = player_damage;
			    out->m_target   = in->m_target;

				// non-penetrate damage.
				if( pen == 4 )
					return player_damage >= in->m_damage;
					
				// penetration damage.
				return player_damage >= in->m_damage_pen;
			}
		}

		// no target was passed, check for any player hit or just get final damage done.
		else {
			out->m_pen = pen != 4;

			// todo - dex; team checks / other checks / etc.
			if( trace.m_entity && trace.m_entity->IsPlayer( ) && game::IsValidHitgroup( trace.m_hitgroup ) ) {
				int group = ( weapon->m_iItemDefinitionIndex( ) == ZEUS ) ? HITGROUP_GENERIC : trace.m_hitgroup;

				player_damage = scale( trace.m_entity->as< Player* >( ), damage, weapon_info->m_armor_ratio, group );

				// set result data for when we hit a player.
				out->m_hitgroup = group;
				out->m_damage   = player_damage;
				out->m_target   = trace.m_entity->as< Player* >( );

				// non-penetrate damage.
				if( pen == 4 )
					return player_damage >= in->m_damage;

				// penetration damage.
				return player_damage >= in->m_damage_pen;
			}

            // if we've reached here then we didn't hit a player yet, set damage and hitgroup.
            out->m_damage = damage;
		}

		// don't run pen code if it's not wanted.
		if( !in->m_can_pen )
			return false;

		// get surface at entry point.
		enter_surface = g_csgo.m_phys_props->GetSurfaceData( trace.m_surface.m_surface_props );

		// this happens when we're too far away from a surface and can penetrate walls or the surface's pen modifier is too low.
		if( ( trace_len > 3000.f && penetration ) || enter_surface->m_game.m_penetration_modifier < 0.1f )
			return false;

		// store data about surface flags / contents.
		nodraw = ( trace.m_surface.m_flags & SURF_NODRAW );
		grate  = ( trace.m_contents & CONTENTS_GRATE );

		// get material at entry point.
		enter_material = enter_surface->m_game.m_material;

		// note - dex; some extra stuff the game does.
		if( !pen && !nodraw && !grate && enter_material != CHAR_TEX_GRATE && enter_material != CHAR_TEX_GLASS )
			return false;

		// no more pen.
		if( penetration <= 0.f || pen <= 0 )
			return false;

		// try to penetrate object.
		if( !TraceToExit( trace.m_endpos, dir, pen_end, &trace, &exit_trace ) ) {
			if( !( g_csgo.m_engine_trace->GetPointContents( pen_end, MASK_SHOT_HULL ) & MASK_SHOT_HULL ) )
				return false;
		}

		// get surface / material at exit point.
		exit_surface  = g_csgo.m_phys_props->GetSurfaceData( exit_trace.m_surface.m_surface_props );
        exit_material = exit_surface->m_game.m_material;

        // todo - dex; check for CHAR_TEX_FLESH and ff_damage_bullet_penetration / ff_damage_reduction_bullets convars?
        //             also need to check !isbasecombatweapon too.
		if( enter_material == CHAR_TEX_GRATE || enter_material == CHAR_TEX_GLASS ) {
			total_pen_mod = 3.f;
			damage_mod    = 0.05f;
		}

		else if( nodraw || grate ) {
			total_pen_mod = 1.f;
			damage_mod    = 0.16f;
		}

		else {
			total_pen_mod = ( enter_surface->m_game.m_penetration_modifier + exit_surface->m_game.m_penetration_modifier ) * 0.5f;
			damage_mod    = 0.16f;
		}

		// thin metals, wood and plastic get a penetration bonus.
		if( enter_material == exit_material ) {
			if( exit_material == CHAR_TEX_CARDBOARD || exit_material == CHAR_TEX_WOOD )
				total_pen_mod = 3.f;

			else if( exit_material == CHAR_TEX_PLASTIC )
				total_pen_mod = 2.f;
		}

		// set some local vars.
		trace_len   = ( exit_trace.m_endpos - trace.m_endpos ).length( );
		modifier    = std::max( 0.f, 1.f / total_pen_mod );
		damage_lost = ( ( modifier * 3.f ) * penetration_mod + ( damage * damage_mod ) ) + ( ( ( trace_len * trace_len ) * modifier ) / 24.f );

		// subtract from damage.
		damage -= std::max( 0.f, damage_lost );
		if( damage < 1.f )
			return false;

		// set new start pos for successive trace.
		start = exit_trace.m_endpos;

		// decrement pen.
		--pen;
	}

	return false;
}