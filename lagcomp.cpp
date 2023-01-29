#include "includes.h"

LagCompensation g_lagcomp{};;

bool LagCompensation::StartPrediction( AimPlayer* data ) {
	// Check if data and player are valid
	if ( data->m_records.empty( ) || data->m_player->dormant( ) ) {
		return false;
	}

	// Count number of non-dormant records
	size_t size = 0;
	for ( const auto& it : data->m_records ) {
		if ( it->dormant( ) ) {
			break;
		}
		size++;
	}

	// Get first record
	LagRecord* record = data->m_records[ 0 ].get( );

	// Check if record is valid
	if ( record == nullptr ) {
		return false;
	}

	// Reset prediction related variables
	record->predict( );

	// Check if LC (Lag Compensation) is broken
	if ( size > 1 && ( ( record->m_origin - data->m_records[ 1 ]->m_origin ).length_sqr( ) > 4096.f ||
		( size > 2 && ( data->m_records[ 1 ]->m_origin - data->m_records[ 2 ]->m_origin ).length_sqr( ) > 4096.f ) ) ) {
		record->m_broke_lc = true;
	}

	if ( !record->m_broke_lc )
		return false;

	int simulation = game::TIME_TO_TICKS( record->m_sim_time );

	if ( std::abs( g_cl.m_arrival_tick - simulation ) >= 128 )
		return true;

	int lag;
	if ( size <= 2 )
		lag = game::TIME_TO_TICKS( record->m_sim_time - data->m_records[ 1 ]->m_sim_time );
	else
		lag = game::TIME_TO_TICKS( data->m_records[ 1 ]->m_sim_time - data->m_records[ 2 ]->m_sim_time );

	// Clamp lag to ensure that it is within a reasonable range.
	lag = std::clamp( lag, 1, 15 );

	int updatedelta = g_cl.m_server_tick - record->m_tick;
	if ( g_cl.m_latency_ticks <= lag - updatedelta )
		return true;

	int next = record->m_tick + 1;
	if ( next + lag >= g_cl.m_arrival_tick )
		return true;

	float change = 0.f, dir = 0.f;
	if ( record->m_velocity.y != 0.f || record->m_velocity.x != 0.f )
		dir = std::atan2( record->m_velocity.y, record->m_velocity.x );

	// Convert the direction to degrees.
	dir = math::rad_to_deg( dir );

	if ( size > 1 ) {
		// get the delta time between the 2 most recent records.
		float dt = record->m_sim_time - data->m_records[ 1 ]->m_sim_time;

		// check for division by zero
		if ( dt <= 0.f )
			return false;

		float prevdir = 0.f;

		// get the direction of the previous velocity.
		if ( data->m_records[ 1 ]->m_velocity.y != 0.f || data->m_records[ 1 ]->m_velocity.x != 0.f )
			prevdir = math::rad_to_deg( std::atan2( data->m_records[ 1 ]->m_velocity.y, data->m_records[ 1 ]->m_velocity.x ) );

		// compute the direction change per tick.
		change = ( math::NormalizedAngle( prevdir - dir ) / dt ) * g_csgo.m_globals->m_interval;

		// clamp the change to a valid range
		change = std::clamp( change, -6.f, 6.f );
	}

	// get the pointer to the players animation state.
	CCSGOPlayerAnimState* state = data->m_player->m_PlayerAnimState( );

	// backup the animation state.
	CCSGOPlayerAnimState backup{};
	if( state )
		std::memcpy( &backup, state, sizeof( CCSGOPlayerAnimState ) );

	// add in the shot prediction here.
	int shot = 0;
	int pred = 0;

	// start our predicton loop.
	while( true ) {
		// see if by predicting this amount of lag
		// we do not break stuff.
		next += lag;
		if( next >= g_cl.m_arrival_tick )
			break;

		// predict lag.
		for( int sim{}; sim < lag; ++sim ) {
			const int numberOfRecords = data->m_records.size( );

			if ( numberOfRecords > 1 ) {
				const float deltaTime = record->m_sim_time - data->m_records[ 1 ]->m_sim_time;

				float previousDirection = math::rad_to_deg( std::atan2( data->m_records[ 1 ]->m_velocity.y, data->m_records[ 1 ]->m_velocity.x ) );

				change = ( math::NormalizedAngle( dir - previousDirection ) / deltaTime ) * g_csgo.m_globals->m_interval;
			}

			if ( std::abs( change ) > 6.f ) {
				change = 0.f;
			}

			dir = math::NormalizedAngle( dir + change );

			const float hyp = record->m_pred_velocity.length_2d( );

			record->m_pred_velocity.x = std::cos( math::deg_to_rad( dir ) ) * hyp;
			record->m_pred_velocity.y = std::sin( math::deg_to_rad( dir ) ) * hyp;

			if ( record->m_pred_flags & FL_ONGROUND ) {
				const int bunnyhoppingEnabled = g_csgo.sv_enablebunnyhopping->GetInt( );
				if ( !bunnyhoppingEnabled ) {
					const float max = data->m_player->m_flMaxspeed( ) * 1.1f;
					const float speed = record->m_pred_velocity.length( );
					if ( max > 0.f && speed > max ) {
						record->m_pred_velocity *= ( max / speed );
					}
				}
				record->m_pred_velocity.z = g_csgo.sv_jump_impulse->GetFloat( );
			}

			// we are not on the ground
			// apply gravity and airaccel.
			else {
				// apply one tick of gravity.
				record->m_pred_velocity.z -= g_csgo.sv_gravity->GetFloat( ) * g_csgo.m_globals->m_interval;

				// compute the ideal strafe angle for this velocity.
				float speed2d = record->m_pred_velocity.length_2d( );
				float ideal   = ( speed2d > 0.f ) ? math::rad_to_deg( std::asin( 15.f / speed2d ) ) : 90.f;
				math::clamp( ideal, 0.f, 90.f );

				float smove = 0.f;
				float abschange = std::abs( change );

				if( abschange <= ideal || abschange >= 30.f ) {
					static float mod{ 1.f };

					dir  += ( ideal * mod );
					smove = 450.f * mod;
					mod  *= -1.f;
				}

				else if( change > 0.f )
					smove = -450.f;

				else
					smove = 450.f;

				// apply air accel.
				AirAccelerate( record, ang_t{ 0.f, dir, 0.f }, 0.f, smove );
			}

			// predict player.
			// convert newly computed velocity
			// to origin and flags.
			PlayerMove( record );

			// move time forward by one.
			record->m_pred_time += g_csgo.m_globals->m_interval;

			// increment total amt of predicted ticks.
			++pred;

			// the server animates every first choked command.
			// therefore we should do that too.
			if( sim == 0 && state )
				PredictAnimations( state, record );
		}
	}

	// restore state.
	if( state )
		std::memcpy( state, &backup, sizeof( CCSGOPlayerAnimState ) );

	if( pred <= 0 )
		return true;

	// lagcomp broken, invalidate bones.
	record->invalidate( );

	// re-setup bones for this record.
	g_bones.setup( data->m_player, nullptr, record );

	return true;
}

void LagCompensation::PlayerMove( LagRecord* record ) {
	vec3_t                start, end, normal;
	CGameTrace            trace;
	CTraceFilterWorldOnly filter;

	start = record->m_pred_origin;
	end = start + ( record->m_pred_velocity * g_csgo.m_globals->m_interval );

	g_csgo.m_engine_trace->TraceRay( Ray( start, end, record->m_mins, record->m_maxs ), CONTENTS_SOLID, &filter, &trace );

	if ( trace.m_fraction != 1.f ) {
		for ( int i = 0; i < 2; ++i ) {
			record->m_pred_velocity -= trace.m_plane.m_normal * record->m_pred_velocity.dot( trace.m_plane.m_normal );

			float adjust = record->m_pred_velocity.dot( trace.m_plane.m_normal );
			if ( adjust < 0.f )
				record->m_pred_velocity -= ( trace.m_plane.m_normal * adjust );

			start = trace.m_endpos;
			end = start + ( record->m_pred_velocity * ( g_csgo.m_globals->m_interval * ( 1.f - trace.m_fraction ) ) );

			g_csgo.m_engine_trace->TraceRay( Ray( start, end, record->m_mins, record->m_maxs ), CONTENTS_SOLID, &filter, &trace );
			if ( trace.m_fraction == 1.f )
				break;
		}
	}

	record->m_pred_origin = trace.m_endpos;
	end = trace.m_endpos;
	end.z -= 2.f;

	g_csgo.m_engine_trace->TraceRay( Ray( record->m_pred_origin, end, record->m_mins, record->m_maxs ), CONTENTS_SOLID, &filter, &trace );

	record->m_pred_flags &= ~FL_ONGROUND;
	if ( trace.m_fraction != 1.f && trace.m_plane.m_normal.z > 0.7f )
		record->m_pred_flags |= FL_ONGROUND;

}

void LagCompensation::AirAccelerate( LagRecord* record, ang_t angle, float fmove, float smove ) {
	vec3_t fwd, right, wishvel, wishdir;
	float  maxspeed, wishspd, wishspeed, currentspeed, addspeed, accelspeed;

	// Determine movement angles.
	math::AngleVectors( angle, &fwd, &right );

	// Zero out z components of movement vectors.
	fwd.z = 0.f;
	right.z = 0.f;

	// Normalize the remainder of vectors.
	fwd.normalize( );
	right.normalize( );

	// Determine x and y parts of velocity.
	wishvel[ 0 ] = fwd[ 0 ] * fmove + right[ 0 ] * smove;
	wishvel[ 1 ] = fwd[ 1 ] * fmove + right[ 1 ] * smove;

	// Zero out z part of velocity.
	wishvel.z = 0.f;

	// Determine magnitude of speed of move.
	wishdir = wishvel;
	wishspeed = wishdir.normalize( );

	// Get maxspeed.
	maxspeed = record->m_player->m_flMaxspeed( );

	// Clamp to server-defined max speed.
	if ( wishspeed != 0.f && wishspeed > maxspeed ) {
		wishspeed = maxspeed;
	}

	// Make a copy to preserve original variable.
	wishspd = wishspeed;

	// Cap speed.
	if ( wishspd > 30.f ) {
		wishspd = 30.f;
	}

	// Determine veer amount.
	currentspeed = record->m_pred_velocity.dot( wishdir );

	// See how much to add.
	addspeed = wishspd - currentspeed;

	// If not adding any, done.
	if ( addspeed <= 0.f ) {
		return;
	}

	// Determine acceleration speed after acceleration.
	accelspeed = g_csgo.sv_airaccelerate->GetFloat( ) * wishspeed * g_csgo.m_globals->m_interval;

	// Cap it.
	if ( accelspeed > addspeed ) {
		accelspeed = addspeed;
	}

	// Add acceleration.
	record->m_pred_velocity += wishdir * accelspeed;
}

void LagCompensation::PredictAnimations( CCSGOPlayerAnimState* state, LagRecord* record ) {
	struct AnimBackup_t {
		float  curtime;
		float  frametime;
		int    flags;
		int    eflags;
		vec3_t velocity;
	};

	// get player ptr.
	Player* player = record->m_player;

	// backup data.
	AnimBackup_t backup;
	backup.curtime   = g_csgo.m_globals->m_curtime;
	backup.frametime = g_csgo.m_globals->m_frametime;
	backup.flags     = player->m_fFlags( );
	backup.eflags    = player->m_iEFlags( );
	backup.velocity  = player->m_vecAbsVelocity( );

	// set globals appropriately for animation.
	g_csgo.m_globals->m_curtime   = record->m_pred_time;
	g_csgo.m_globals->m_frametime = g_csgo.m_globals->m_interval;

	// EFL_DIRTY_ABSVELOCITY
	// skip call to C_BaseEntity::CalcAbsoluteVelocity
	player->m_iEFlags( ) &= ~0x1000;

	// set predicted flags and velocity.
	player->m_fFlags( )         = record->m_pred_flags;
	player->m_vecAbsVelocity( ) = record->m_pred_velocity;

	// enable re-animation in the same frame if animated already.
	if( state->m_frame >= g_csgo.m_globals->m_frame )
		state->m_frame = g_csgo.m_globals->m_frame - 1;

	bool fake = g_menu.main.aimbot.correct.get( );

	// rerun the resolver since we edited the origin.
	if( fake )
		g_resolver.ResolveAngles( player, record );

	// update animations.
	game::UpdateAnimationState( state, record->m_eye_angles );

	// rerun the pose correction cuz we are re-setupping them.
	if( fake )
		g_resolver.ResolvePoses( player, record );

	// get new rotation poses and layers.
	player->GetPoseParameters( record->m_poses );
	player->GetAnimLayers( record->m_layers );
	record->m_abs_ang = player->GetAbsAngles( );

	// restore globals.
	g_csgo.m_globals->m_curtime   = backup.curtime;
	g_csgo.m_globals->m_frametime = backup.frametime;

	// restore player data.
	player->m_fFlags( )         = backup.flags;
	player->m_iEFlags( )        = backup.eflags;
	player->m_vecAbsVelocity( ) = backup.velocity;
}