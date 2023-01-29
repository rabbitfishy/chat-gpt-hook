#include "includes.h"

Resolver g_resolver{};;

LagRecord* Resolver::FindIdealRecord( AimPlayer* data ) {
	LagRecord* first_valid, * current;

	if ( data->m_records.empty( ) )
		return nullptr;

	first_valid = nullptr;

	// iterate records.
	for ( const auto& it : data->m_records ) {
		if ( it->dormant( ) || it->immune( ) || !it->valid( ) )
			continue;

		// get current record.
		current = it.get( );

		// first record that was valid, store it for later.
		if ( !first_valid )
			first_valid = current;

		// try to find a record with a shot, lby update, walking or no anti-aim.
		if ( it->m_shot || it->m_mode == Modes::RESOLVE_BODY || it->m_mode == Modes::RESOLVE_WALK || it->m_mode == Modes::RESOLVE_NONE )
			return current;
	}

	// none found above, return the first valid record if possible.
	return ( first_valid ) ? first_valid : nullptr;
}

LagRecord* Resolver::FindLastRecord( AimPlayer* data ) {
	LagRecord* current;

	if ( data->m_records.empty( ) )
		return nullptr;

	// iterate records in reverse.
	for ( auto it = data->m_records.crbegin( ); it != data->m_records.crend( ); ++it ) {
		current = it->get( );

		// if this record is valid.
		// we are done since we iterated in reverse.
		if ( current->valid( ) && !current->immune( ) && !current->dormant( ) )
			return current;
	}

	return nullptr;
}

void Resolver::OnBodyUpdate( Player* player, float value ) {
	if ( !player ) return;
	int idx = player->index( ) - 1;
	if ( idx < 0 || idx >= g_aimbot.m_players.size( ) ) return;

	AimPlayer* data = &g_aimbot.m_players[ idx ];

	// set data only if it's different
	if ( data->m_body != value ) {
		data->m_old_body = data->m_body;
		data->m_body = value;
	}
}

float Resolver::GetAwayAngle( LagRecord* record ) {
	vec3_t delta = record->m_pred_origin - g_cl.m_local->m_vecOrigin( );
	delta.normalize( );

	ang_t angles;
	math::VectorAngles( delta, angles );
	return angles.y;
}

void Resolver::MatchShot( AimPlayer* data, LagRecord* record ) {
	// do not attempt to do this in nospread mode.
	if( g_menu.main.config.mode.get( ) == 1 )
		return;

	float shoot_time = -1.f;

	Weapon* weapon = data->m_player->GetActiveWeapon( );
	if( weapon ) {
		// with logging this time was always one tick behind.
		// so add one tick to the last shoot time.
		shoot_time = weapon->m_fLastShotTime( ) + g_csgo.m_globals->m_interval;
	}

	// this record has a shot on it.
	if( game::TIME_TO_TICKS( shoot_time ) == game::TIME_TO_TICKS( record->m_sim_time ) ) {
		if( record->m_lag <= 2 )
			record->m_shot = true;
		
		// more then 1 choke, cant hit pitch, apply prev pitch.
		else if( data->m_records.size( ) >= 2 ) {
			LagRecord* previous = data->m_records[ 1 ].get( );

			if( previous && !previous->dormant( ) )
				record->m_eye_angles.x = previous->m_eye_angles.x;
		}
	}
}

void Resolver::SetMode( LagRecord* record ) {
	// the resolver has 3 modes to chose from.
	// these modes will vary more under the hood depending on what data we have about the player
	// and what kind of hack vs. hack we are playing (mm/nospread).

	float speed = record->m_anim_velocity.length( );

	// if on ground, moving, and not fakewalking.
	if( ( record->m_flags & FL_ONGROUND ) && speed > 0.1f && !record->m_fake_walk )
		record->m_mode = Modes::RESOLVE_WALK;

	// if on ground, not moving or fakewalking.
	if( ( record->m_flags & FL_ONGROUND ) && ( speed <= 0.1f || record->m_fake_walk ) )
		record->m_mode = Modes::RESOLVE_STAND;

	// if not on ground.
	else if( !( record->m_flags & FL_ONGROUND ) )
		record->m_mode = Modes::RESOLVE_AIR;
}

void Resolver::ResolveAngles( Player* player, LagRecord* record ) {
	AimPlayer* data = &g_aimbot.m_players[ player->index( ) - 1 ];

	// mark this record if it contains a shot.
	MatchShot( data, record );

	// next up mark this record with a resolver mode that will be used.
	SetMode( record );

	// if we are in nospread mode, force all players pitches to down.
	// TODO; we should check thei actual pitch and up too, since those are the other 2 possible angles.
	// this should be somehow combined into some iteration that matches with the air angle iteration.
	if( g_menu.main.config.mode.get( ) == 1 )
		record->m_eye_angles.x = 90.f;

	// we arrived here we can do the acutal resolve.
	if( record->m_mode == Modes::RESOLVE_WALK ) 
		ResolveWalk( data, record );

	else if( record->m_mode == Modes::RESOLVE_STAND )
		ResolveStand( data, record );

	else if( record->m_mode == Modes::RESOLVE_AIR )
		ResolveAir( data, record );

	// normalize the eye angles, doesn't really matter but its clean.
	math::NormalizeAngle( record->m_eye_angles.y );
}

void Resolver::ResolveWalk( AimPlayer* data, LagRecord* record ) {
	// apply lby to eyeangles.
	record->m_eye_angles.y = record->m_body;

	// delay body update.
	data->m_body_update = record->m_anim_time + 0.22f;

	// reset stand and body index.
	data->m_stand_index  = 0;
	data->m_stand_index2 = 0;
	data->m_body_index   = 0;

	// copy the last record that this player was walking
	// we need it later on because it gives us crucial data.
	std::memcpy( &data->m_walk_record, record, sizeof( LagRecord ) );
}

void Resolver::ResolveStand( AimPlayer* data, LagRecord* record ) {
	// get predicted away angle for the player.
	float away = GetAwayAngle( record );

	// pointer for easy access.
	LagRecord* move = &data->m_walk_record;
	if( move->m_sim_time > 0.f ) {
		vec3_t delta = move->m_origin - record->m_origin;

		// check if moving record is close.
		if( delta.length( ) <= 128.f ) {
			// indicate that we are using the moving lby.
			data->m_moved = true;
		}
	}

	float diff = math::NormalizedAngle( record->m_body - move->m_body );
	float delta = record->m_anim_time - move->m_anim_time;

	// Check if delta is within a valid range
	if ( delta >= 0.22f && delta <= 0.3f ) {
		// Check if record's animation time is greater than or equal to the body update time
		if ( record->m_anim_time >= data->m_body_update ) {
			// Check if body index is less than or equal to 3
			if ( data->m_body_index <= 3 ) {
				record->m_eye_angles.y = record->m_body;
				data->m_body_update = record->m_anim_time + 1.1f;
				record->m_mode = Modes::RESOLVE_BODY;
			}
			else {
				record->m_mode = Modes::RESOLVE_STAND1;
				C_AnimationLayer* curr = &record->m_layers[ 3 ];
				int act = data->m_player->GetSequenceActivity( curr->m_sequence );

				record->m_eye_angles.y = move->m_body;

				// Check if stand index is divisible by 3 with no remainder
				if ( !( data->m_stand_index % 3 ) ) {
					record->m_eye_angles.y += g_csgo.RandomFloat( -35.f, 35.f );
				}

				// Check if stand index is greater than 6 and act is not equal to 980
				if ( data->m_stand_index > 6 && act != 980 ) {
					record->m_eye_angles.y = move->m_body + 180.f;
				}
				else if ( data->m_stand_index > 4 && act != 980 ) {
					record->m_eye_angles.y = away + 180.f;
				}
			}
		}
		else {
			record->m_eye_angles.y = move->m_body;
			record->m_mode = Modes::RESOLVE_STOPPED_MOVING;
		}
	}
}

void Resolver::ResolveAir( AimPlayer* data, LagRecord* record ) {
	// Player speed is too low, assume they're standing.
	if ( record->m_velocity.length_2d( ) < 60.f ) {
		// Set mode for completion.
		record->m_mode = Modes::RESOLVE_STAND;

		// Invoke stand resolver.
		ResolveStand( data, record );
		return;
	}

	// Predict player direction based on velocity.
	float velocity_yaw = math::rad_to_deg( std::atan2( record->m_velocity.y, record->m_velocity.x ) );

	switch ( data->m_shots % 3 ) {
	case 0:
		record->m_eye_angles.y = velocity_yaw + 180.f;
		break;
	case 1:
		record->m_eye_angles.y = velocity_yaw - 90.f;
		break;
	case 2:
		record->m_eye_angles.y = velocity_yaw + 90.f;
		break;
	}
}

void Resolver::ResolvePoses( Player* player, LagRecord* record ) {
	AimPlayer* data = &g_aimbot.m_players[ player->index( ) - 1 ];

	// only do this bs when in air.
	if( record->m_mode == Modes::RESOLVE_AIR ) {
		// ang = pose min + pose val x ( pose range )

		// lean_yaw
		player->m_flPoseParameter( )[ 2 ]  = g_csgo.RandomInt( 0, 4 ) * 0.25f;   

		// body_yaw
		player->m_flPoseParameter( )[ 11 ] = g_csgo.RandomInt( 1, 3 ) * 0.25f;
	}
}